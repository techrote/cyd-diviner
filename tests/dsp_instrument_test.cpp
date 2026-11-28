// Compiles the actual three instrument source files. The only substituted
// seam is graphics/HAL; these tests do not qualify LCD or physical audio I/O.
#include "../src/rite.h"
#include "../src/theme.h"
#include "../src/dsp.h"
#include "../src/vox.h"
#include "../src/dsp_score.h"
#include <atomic>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

thread_local bool audioThread = false;
std::atomic<uint64_t> audioAllocations{0};
std::atomic<bool> failNextState{false};
void *operator new(std::size_t n) {
    if (audioThread) audioAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void *p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void *operator new(std::size_t n, const std::nothrow_t&) noexcept {
    if (failNextState.exchange(false)) return nullptr;
    try { return ::operator new(n); } catch (...) { return nullptr; }
}
// Keep the standard replacement-delete allocation shim outside optimizer
// inlining; GCC otherwise misdiagnoses this legal malloc/free pair.
__attribute__((noinline)) static void freeAllocation(void *p) { std::free(p); }
void operator delete(void *p) noexcept { freeAllocation(p); }
void operator delete(void *p, std::size_t) noexcept { freeAllocation(p); }
void operator delete(void *p, const std::nothrow_t&) noexcept { freeAllocation(p); }

namespace hal {
static std::mutex callbackLock;
static AudioFn callback = nullptr;
static std::atomic<uint32_t> clockMs{0};
Canvas &canvas() { static Canvas c; return c; }
uint32_t ms() { return clockMs.load(std::memory_order_relaxed); }
void setAudio(AudioFn fn) { std::lock_guard<std::mutex> lock(callbackLock); callback = fn; }
static bool enabled() { std::lock_guard<std::mutex> lock(callbackLock); return callback != nullptr; }
static void render(int16_t *out, int n) {
    std::lock_guard<std::mutex> lock(callbackLock);
    audioThread = true;
    if (callback) callback(out,n); else std::memset(out,0,n*sizeof(*out));
    audioThread = false;
}
}
namespace theme {
namespace detail { static Palette colours{}; const Palette *activePalette = &colours; }
static int starvedCount = 0;
uint16_t blend(uint16_t fg,uint16_t,uint8_t) { return fg; }
void header(const char*,const char*,int) {}
void footer(const char*,const char*) {}
void starved(const char*,const char*) { ++starvedCount; }
void diamond(int,int,int,uint16_t,bool) {}
void frame(int,int,int,int,uint16_t,uint16_t) {}
void text(const char*,int,int,uint16_t,int) {}
void textCentre(const char*,int,int,uint16_t,int) {}
void textRight(const char*,int,int,uint16_t,int) {}
void meter(const char*,int,int,int,int,float) {}
const char *noteName(int midi) { static const char* notes[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}; return notes[(midi%12+12)%12]; }
int glyphWidth(const DaedricFont&,const char *s,int) { return std::strlen(s)*8; }
int glyphs(const DaedricFont&,const char*,int,int,uint16_t,int) { return 0; }
void glyphsCentre(const DaedricFont&,const char*,int,int,uint16_t) {}
}
// Including within distinct namespaces keeps the original anonymous state
// private to each instrument while allowing white-box ownership assertions.
namespace tested_canon {
#include "../src/apps/canon.cpp"
}
namespace tested_drone {
#include "../src/apps/drone.cpp"
}
namespace tested_glyphsong {
#include "../src/apps/glyphsong.cpp"
}

static void key(const Rite &rite,char ch,uint8_t nav=hal::N_NONE,bool fn=false,bool ctrl=false) {
    assert(rite.key({ch,nav,fn,ctrl}));
}
static void pointer(const Rite &rite,int x,int y,bool start=true,bool down=true,uint32_t ms=1) {
    assert(rite.pointer && rite.pointer({x,y,start,down,ms}));
}
static void little(FILE *f,uint32_t value,int bytes) { for(int i=0;i<bytes;++i) std::fputc((value>>(i*8))&255,f); }
static void wav(const std::string &path,const std::vector<int16_t> &samples) {
    FILE *f=std::fopen(path.c_str(),"wb"); assert(f);
    const uint32_t bytes=uint32_t(samples.size()*2);
    std::fwrite("RIFF",1,4,f);little(f,36+bytes,4);std::fwrite("WAVEfmt ",1,8,f);little(f,16,4);little(f,1,2);little(f,1,2);
    little(f,hal::RATE,4);little(f,hal::RATE*2,4);little(f,2,2);little(f,16,2);std::fwrite("data",1,4,f);little(f,bytes,4);
    for(int16_t x:samples)little(f,uint16_t(x),2);
    assert(std::fclose(f)==0);
}
static double percent(const std::vector<double> &values,double p) { return values[std::size_t(std::ceil(p*values.size()))-1]; }
static void specimen(const Rite &rite,const std::string &name,const std::string &directory,int seconds=5) {
    std::vector<int16_t> samples(std::size_t(seconds)*hal::RATE);
    std::vector<double> timings;timings.reserve((samples.size()+127)/128);
    int peak=0;double sum=0;
    for(std::size_t start=0;start<samples.size();start+=128) {
        const int n=int(std::min<std::size_t>(128,samples.size()-start));
        const auto t=std::chrono::steady_clock::now();
        hal::render(samples.data()+start,n);
        const auto u=std::chrono::steady_clock::now();
        timings.push_back(std::chrono::duration<double,std::micro>(u-t).count());
        for(int i=0;i<n;++i) { int x=samples[start+i];peak=std::max(peak,std::abs(x));sum+=double(x)*x; }
        hal::clockMs.store(uint32_t((start+n)*1000/hal::RATE));
        if(start%2048==0) rite.frame(hal::ms());
    }
    assert(peak>10 && peak<=30000);
    std::sort(timings.begin(),timings.end());
    const double p50=percent(timings,.5),p95=percent(timings,.95),p99=percent(timings,.99),worst=timings.back();
    const double deadline=128.0*1e6/hal::RATE;
    std::printf("HOST %s blocks=%zu rms_pcm=%.3f peak_pcm=%d median_us=%.3f p95_us=%.3f p99_us=%.3f worst_us=%.3f p99_block_budget_percent=%.3f\n",name.c_str(),timings.size(),std::sqrt(sum/samples.size()),peak,p50,p95,p99,worst,p99/deadline*100);
    if(!directory.empty()) wav(directory+"/"+name+".wav",samples);
}

int main(int argc,char **argv) {
    const std::string output=argc>1?argv[1]:"";
    if(!output.empty()) std::filesystem::create_directories(output);
    const Rite *rites[]={&tested_canon::RITE_CANON,&tested_drone::RITE_DRONE,&tested_glyphsong::RITE_GLYPHSONG};
    int16_t block[128]{};
    for(const Rite *r:rites) {
        const int before=theme::starvedCount;
        failNextState.store(true);r->enter();assert(!hal::enabled());r->frame(0);assert(theme::starvedCount==before+1);r->leave();
    }
    const Rite &canon=*rites[0];canon.enter();
    pointer(canon,234,77);pointer(canon,234,77,false,false);
    pointer(canon,234,90);pointer(canon,234,90,false,false);
    hal::render(block,128);
    for(int k=0;k<4;++k) assert(tested_canon::S->L[k].sps>=uint32_t(.299*hal::RATE));
    assert(tested_canon::S->L[0].sps<tested_canon::S->L[3].sps);
    specimen(canon,"canon-fast-spread",output,8);
    key(canon,0,hal::N_ENTER);hal::render(block,1);
    for(int k=0;k<4;++k) assert(tested_canon::S->L[k].idx==0 && tested_canon::S->L[k].count==0);
    key(canon,' ');hal::render(block,128);
    for(int k=0;k<4;++k) assert(tested_canon::S->L[k].count==0);
    pointer(canon,140,40,true,true,100);pointer(canon,140,40,false,false,700);
    assert(tested_canon::S->phrase[0]==tested_canon::REST);
    key(canon,'a');assert(tested_canon::S->phrase[0]==0);
    pointer(canon,140,40,true,true,800);pointer(canon,140,10,false,true,820);pointer(canon,140,10,false,false,850);
    assert(tested_canon::S->phrase[0]==6);
    // Mixed physical/scripted input can erase/reset a cell while its drag
    // remains captured. No stale index may survive those UI operations.
    pointer(canon,140,40,true,true,1000);
    for(int i=0;i<8;++i) key(canon,0,hal::N_BACK);
    assert(tested_canon::S->len==0);
    assert(!canon.pointer({140,10,false,true,1100}));
    pointer(canon,140,40,false,false,1700);
    pointer(canon,220,60,true,true,1800);
    key(canon,'r');
    int8_t afterNew[8];std::memcpy(afterNew,tested_canon::S->phrase,sizeof afterNew);
    assert(!canon.pointer({220,20,false,true,1850}));
    pointer(canon,220,60,false,false,2400);
    assert(std::memcmp(afterNew,tested_canon::S->phrase,sizeof afterNew)==0);
    canon.leave();assert(!hal::enabled());

    const Rite &drone=*rites[1];drone.enter();
    pointer(drone,126,23);pointer(drone,126,23,false,false);
    for(int style=0;style<6;++style) {
        specimen(drone,std::string("drone-")+tested_drone::STYLES[style].name,output);
        key(drone,'p');
    }
    drone.leave();assert(!hal::enabled());

    const Rite &glyph=*rites[2];glyph.enter();
    for(char c:std::string("day3_ time5?")) key(glyph,c);
    key(glyph,0,hal::N_ENTER);
    for(int style=0;style<4;++style) {
        specimen(glyph,std::string("glyphsong-")+tested_glyphsong::STYLE_NAME[style],output);
        key(glyph,0,hal::N_TAB);
    }
    key(glyph,0,hal::N_LEFT,false,true);key(glyph,'!');
    assert(std::strcmp(tested_glyphsong::S->text,"day3_ time5!?")==0);
    glyph.leave();assert(!hal::enabled());

    // Live concurrent control changes and draw telemetry against rendering.
    for(const Rite *r:rites) {
        r->enter();std::atomic<bool> done{false};
        std::thread worker([&] { int16_t out[128];while(!done.load(std::memory_order_acquire))hal::render(out,128); });
        for(uint32_t i=0;i<5000;++i) {
            hal::clockMs.store(i*16);
            if(r==&glyph) {
                key(*r,char('a'+i%26));
                if(i%5==0)key(*r,0,hal::N_ENTER);
                if(i%11==0)key(*r,0,hal::N_TAB);
                key(*r,0,i%2?hal::N_UP:hal::N_DOWN,true);
            } else {
                key(*r,char("awsedfr1234 "[i%11]));
                key(*r,0,i%2?hal::N_LEFT:hal::N_RIGHT);
            }
            r->frame(i*16);
        }
        done.store(true,std::memory_order_release);worker.join();r->leave();
    }
    for(int repeat=0;repeat<100;++repeat)for(const Rite *r:rites){r->enter();hal::render(block,128);r->frame(repeat);r->leave();}
    assert(audioAllocations.load()==0);
    std::printf("PASS real instrument kernels: 3 allocation failures, 11 audio specimens, 15000 concurrent UI iterations, 300 lifecycle repeats, callback_allocations=0; state_bytes canon=%zu drone=%zu glyphsong=%zu\n",sizeof(tested_canon::State),sizeof(tested_drone::State),sizeof(tested_glyphsong::State));
}
