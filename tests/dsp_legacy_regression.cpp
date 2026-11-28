// Compile the actual existing renderers twice: once with current DSP and once
// with the attributed, untouched CYD baseline formulas. This isolates the
// shared-DSP change from instrument logic. Controls run sequentially; this is
// NOT a data-race qualification of the legacy instruments' volatile controls.
#include "../src/rite.h"
#include "../src/theme.h"
#include "../src/dsp.h"
#include "dsp_legacy_reference.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <optional>

namespace hal {
Canvas &canvas() { static Canvas c; return c; }
uint32_t ms() { return 1234; }
void setAudio(AudioFn) {}
}
namespace theme {
namespace detail { static Palette colours{}; const Palette *activePalette=&colours; }
uint16_t blend(uint16_t a,uint16_t,uint8_t) { return a; }
void header(const char*,const char*,int) {}
void footer(const char*,const char*) {}
void drawBackground(Canvas&) {}
void drawSceneBackground(Canvas&,int,int,int,int,const Palette&) {}
void diamond(int,int,int,uint16_t,bool) {}
void frame(int,int,int,int,uint16_t,uint16_t) {}
void text(const char*,int,int,uint16_t,int) {}
void textCentre(const char*,int,int,uint16_t,int) {}
void textRight(const char*,int,int,uint16_t,int) {}
void meter(const char*,int,int,int,int,float) {}
const char *noteName(int) { return "C"; }
int glyphWidth(const DaedricFont&,const char *s,int) { return int(std::strlen(s))*8; }
int glyphs(Canvas&,const DaedricFont&,const char*,int,int,uint16_t,int,uint8_t,int) { return 0; }
int glyphs(const DaedricFont&,const char*,int,int,uint16_t,int) { return 0; }
void glyphsCentre(const DaedricFont&,const char*,int,int,uint16_t) {}
}

// A few CYD-only visual helper functions are compiled but unused in this
// graphics-free native test. Keep warnings enabled for all test assertions.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
namespace current_seq {
#include "../src/apps/seq.cpp"
}
namespace baseline_seq {
namespace dsp=legacy_dsp;
#include "../src/apps/seq.cpp"
}
namespace current_beat {
#include "../src/apps/beat.cpp"
}
namespace baseline_beat {
namespace dsp=legacy_dsp;
#include "../src/apps/beat.cpp"
}
namespace current_oracle {
#include "../src/apps/oracle.cpp"
}
namespace baseline_oracle {
namespace dsp=legacy_dsp;
#include "../src/apps/oracle.cpp"
}
namespace current_visions {
#include "../src/apps/visions.cpp"
}
namespace baseline_visions {
namespace dsp=legacy_dsp;
#include "../src/apps/visions.cpp"
}
#pragma GCC diagnostic pop

static uint64_t sampleCount=0;
static void compare(const char *name,hal::AudioFn current,hal::AudioFn baseline,int count,bool audible=true) {
    int16_t got[128],expected[128];
    int peak=0,clipped=0;
    double energy=0;
    uint64_t hash=14695981039346656037ull;
    for(int at=0;at<count;at+=128) {
        const int n=std::min(128,count-at);
        current(got,n);baseline(expected,n);
        for(int i=0;i<n;++i) {
            if(got[i]!=expected[i]) {
                std::fprintf(stderr,"FAIL legacy %s sample=%d current=%d baseline=%d\n",name,at+i,got[i],expected[i]);
                std::abort();
            }
            peak=std::max(peak,std::abs(int(got[i])));
            clipped+=std::abs(int(got[i]))>=30000;
            energy+=double(got[i])*got[i];
            hash=(hash^uint8_t(got[i]))*1099511628211ull;
            hash=(hash^uint8_t(uint16_t(got[i])>>8))*1099511628211ull;
        }
    }
    assert(audible?peak>10:peak==0);
    assert(peak<=30000);
    sampleCount+=count;
    std::printf("EXACT %s samples=%d rms=%.3f peak=%d clipped=%d pcm_fnv1a64=%016llx\n",name,count,std::sqrt(energy/count),peak,clipped,(unsigned long long)hash);
}
template<class T> static void finiteFilter(const T &filter) {
    assert(std::isfinite(filter.low)&&std::isfinite(filter.band));
    assert(std::isfinite(filter.f)&&std::isfinite(filter.q));
}
template<class T> static void finiteDelay(const T &delay) {
    for(float sample:delay.buf) assert(std::isfinite(sample));
}

int main() {
    dsp::prepare();legacy_dsp::sineTable();
    for(int i=0;i<=1024;++i) assert(dsp::sineTable()[i]==legacy_dsp::sineTable()[i]);
    for(float hz:{.013f,.045f,.07f,36.7f,55.f,185.f,440.f,659.3f,1318.5f,4497.f})
        assert(dsp::hzToInc(hz)==legacy_dsp::hzToInc(hz));

    current_seq::enter();baseline_seq::enter();
    compare("seq-default",current_seq::render,baseline_seq::render,hal::RATE*8);
    current_seq::S->bpm=baseline_seq::S->bpm=240;
    current_seq::S->swing=baseline_seq::S->swing=.42f;
    for(int track=0;track<4;++track) for(int step=0;step<16;++step) {
        auto &a=current_seq::S->bank[0].pat[track][step];
        auto &b=baseline_seq::S->bank[0].pat[track][step];
        a.on=b.on=true;a.accent=b.accent=true;a.slide=b.slide=step%3!=0;a.note=b.note=int8_t(step);
    }
    compare("seq-dense-accent-slide",current_seq::render,baseline_seq::render,hal::RATE*8);
    finiteFilter(current_seq::S->acidF);finiteFilter(current_seq::S->snareF);finiteFilter(current_seq::S->hatF);
    finiteDelay(current_seq::S->delay);
    assert(current_seq::S->scope.w.load()==baseline_seq::S->scope.w);
    for(int i=0;i<dsp::Scope::N;++i) assert(current_seq::S->scope.s[i].load()==baseline_seq::S->scope.s[i]);
    delete current_seq::S;current_seq::S=nullptr;delete baseline_seq::S;baseline_seq::S=nullptr;

    current_beat::enter();baseline_beat::enter();
    for(int preset=0;preset<current_beat::N_PRESETS;++preset) for(int rate=0;rate<4;++rate) {
        current_beat::load(current_beat::PRESETS[preset]);baseline_beat::load(baseline_beat::PRESETS[preset]);
        current_beat::cast();baseline_beat::cast();
        assert(!current_beat::S->err&&!baseline_beat::S->err);
        current_beat::S->rate=baseline_beat::S->rate=rate;
        char name[48];std::snprintf(name,sizeof name,"beat-preset-%d-rate-%d",preset,current_beat::RATES[rate]);
        compare(name,current_beat::render,baseline_beat::render,hal::RATE);
        assert(std::isfinite(current_beat::S->dc));
    }
    assert(current_beat::S->scope.w.load()==baseline_beat::S->scope.w);
    for(int i=0;i<dsp::Scope::N;++i) assert(current_beat::S->scope.s[i].load()==baseline_beat::S->scope.s[i]);
    delete current_beat::S;current_beat::S=nullptr;delete baseline_beat::S;baseline_beat::S=nullptr;

    current_oracle::enter();baseline_oracle::enter();
    for(int rune=0;rune<26;++rune) {
        const int voice=rune%3;
        current_oracle::S->runes[voice]=baseline_oracle::S->runes[voice]=rune;
        current_oracle::S->pendingChime=baseline_oracle::S->pendingChime=voice;
        compare("oracle-chime",current_oracle::render,baseline_oracle::render,hal::RATE/4);
    }
    finiteDelay(current_oracle::S->delay);
    delete current_oracle::S;current_oracle::S=nullptr;delete baseline_oracle::S;baseline_oracle::S=nullptr;

    current_visions::enter();baseline_visions::enter();
    current_visions::S->layered=baseline_visions::S->layered=false;
    for(int vision=0;vision<6;++vision) {
        current_visions::S->vis=static_cast<current_visions::Vision>(vision);
        baseline_visions::S->vis=static_cast<baseline_visions::Vision>(vision);
        current_visions::retone();baseline_visions::retone();
        compare(current_visions::NAMES[vision],current_visions::render,baseline_visions::render,hal::RATE*2);
        finiteFilter(current_visions::S->windF);
    }
    current_visions::S->sound=baseline_visions::S->sound=false;
    compare("visions-sound-off",current_visions::render,baseline_visions::render,128,false);
    delete current_visions::S;current_visions::S=nullptr;delete baseline_visions::S;baseline_visions::S=nullptr;
    std::printf("PASS legacy shared-DSP regression: compared_samples=%llu scope_bytes_current=%zu scope_bytes_baseline=%zu; sequential controls only\n",(unsigned long long)sampleCount,sizeof(dsp::Scope),sizeof(legacy_dsp::Scope));
}
