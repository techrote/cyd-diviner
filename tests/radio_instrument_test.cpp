// Compiles the actual three instrument source files. The only substituted
// seam is graphics/HAL; these tests do not qualify LCD or physical audio I/O.
#include "../src/rite.h"
#include "../src/theme.h"
#include "../src/dsp.h"
#include "../src/vox.h"
#include "../src/coven_protocol.h"
#include "../src/radio_queue.h"
#include "../src/radio_sonification.h"
#include "../src/net.h"
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
uint32_t heapFree() { return 0; }
uint32_t heapLargest() { return 0; }
void debugf(const char*, ...) {}
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

namespace net {
static bool radioOn = false, failStart = false, strongVisitor = false, freezeObservation = false;
static uint32_t observationAt = 0;
static int32_t observationLeadMs = 0, receiveOffsetMs = 0;
static radio_queue::Ring<NowPkt,15> incoming;
void radioStop() { radioOn = false; incoming.clear(); }
void wifiBegin(bool connect) { assert(!connect); radioOn = true; }
void bleBegin() { radioOn = true; }
static AP accessPoint(int i) {
    AP a; a.ssid = "synthetic"; for (int j=0;j<6;++j) a.bssid[j]=uint8_t(i*7+j);
    a.rssi = int8_t(-40-i*7); a.channel=1; a.auth=0;a.known=false;
    a.lastSeen=freezeObservation?observationAt:uint32_t(int64_t(hal::ms())+observationLeadMs);return a;
}
int wifiScan(std::vector<AP> &out) {
    out.clear(); if(!radioOn)return -1;
    if(strongVisitor){auto a=accessPoint(20);a.rssi=-25;out.push_back(a);}
    for(int i=0;i<8;++i)out.push_back(accessPoint(i));
    return int(out.size());
}
int wifiScanStatus(){return 0;}
int bleScan(std::vector<BleDev> &out) {
    out.clear();if(!radioOn)return BLE_UNAVAILABLE;
    for(int i=0;i<8;++i){BleDev d; d.addr="02:00:00:00:00:0"+std::to_string(i);d.name="fixture";
        d.rssi=int8_t(-45-i*4);d.company=0xffff;d.connectable=false;d.lastSeen=uint32_t(int64_t(hal::ms())+observationLeadMs);out.push_back(d);}
    return int(out.size());
}
bool nowBegin(){radioOn=!failStart;return radioOn;}
bool nowSend(const void*,int n){return radioOn&&n>0&&n<=250;}
void nowMac(uint8_t mac[6]){const uint8_t local[]={2,0,0,0,0,30};memcpy(mac,local,6);}
NowStatus nowStatus(){NowStatus s;s.ready=radioOn;return s;}
bool nowPoll(NowPkt &p){return incoming.pop(p);}
static void member(uint8_t rank,uint32_t tick,uint8_t root=3,uint8_t kind=0){
    NowPkt p{};p.mac[0]=2;p.mac[5]=rank;p.receivedAt=uint32_t(int64_t(hal::ms())+receiveOffsetMs);p.rssi=-50;p.len=coven::WireSize;
    uint8_t b[coven::WireSize];coven::encode({kind,root,1,4,uint8_t(tick/1800),uint16_t(tick%1800)},b);
    memcpy(p.data,b,sizeof b);assert(incoming.push(p));
}
static void speech(){
    NowPkt p{};p.mac[0]=2;p.mac[5]=90;p.receivedAt=hal::ms();p.rssi=-70;
    const char *line="DY1|1|2|3|4|5|6|7|8|9|hello synthetic world";
    p.len=uint8_t(strlen(line));memcpy(p.data,line,p.len);incoming.push(p);
}
}
namespace tested_coven {
#include "../src/apps/coven.cpp"
}
namespace tested_spectra {
#include "../src/apps/spectra.cpp"
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
    const uint32_t initial = hal::ms();
    int peak=0;double sum=0;
    for(std::size_t start=0;start<samples.size();start+=128) {
        const int n=int(std::min<std::size_t>(128,samples.size()-start));
        const auto t=std::chrono::steady_clock::now();
        hal::render(samples.data()+start,n);
        const auto u=std::chrono::steady_clock::now();
        timings.push_back(std::chrono::duration<double,std::micro>(u-t).count());
        for(int i=0;i<n;++i) { int x=samples[start+i];peak=std::max(peak,std::abs(x));sum+=double(x)*x; }
        hal::clockMs.store(initial + uint32_t((start+n)*1000/hal::RATE));
        if(start%2048==0) rite.frame(hal::ms());
    }
    assert(peak>10 && peak<=30000);
    std::sort(timings.begin(),timings.end());
    const double p50=percent(timings,.5),p95=percent(timings,.95),p99=percent(timings,.99),worst=timings.back();
    const double deadline=128.0*1e6/hal::RATE;
    std::printf("HOST %s blocks=%zu rms_pcm=%.3f peak_pcm=%d median_us=%.3f p95_us=%.3f p99_us=%.3f worst_us=%.3f p99_block_budget_percent=%.3f\n",name.c_str(),timings.size(),std::sqrt(sum/samples.size()),peak,p50,p95,p99,worst,p99/deadline*100);
    if(!directory.empty()) wav(directory+"/"+name+".wav",samples);
}


int main(int argc,char**argv){
    const std::string output=argc>1?argv[1]:"";
    if(!output.empty())std::filesystem::create_directories(output);
    const Rite &spectra=tested_spectra::RITE_SPECTRA,&covenRite=tested_coven::RITE_COVEN;
    const Rite* rites[]={&spectra,&covenRite};
    int16_t block[128]{};
    for(auto r:rites){
        net::radioOn=true; // prior utilities can deliberately retain their stack
        const int before=theme::starvedCount;failNextState.store(true);r->enter();
        assert(!hal::enabled()&&!net::radioOn);r->frame(0);assert(theme::starvedCount==before+1);r->leave();assert(!net::radioOn);
    }
    net::failStart=true;covenRite.enter();assert(!tested_coven::S->ready&&hal::enabled());
    hal::render(block,128);covenRite.leave();net::failStart=false;

    // A scan/callback producer can sample its monotonic clock just after the
    // frame owner sampled now. Such fresh observations must not underflow
    // into apparently ancient timestamps.
    net::observationLeadMs=1;
    spectra.enter();spectra.frame(hal::ms());
    assert(tested_spectra::S->v[0].used&&!tested_spectra::S->v[0].leaving);
    spectra.leave();net::observationLeadMs=0;

    spectra.enter();spectra.frame(hal::ms());
    assert(tested_spectra::S->v[7].used);
    pointer(spectra,234,98);pointer(spectra,234,98,false,false);assert(tested_spectra::S->air==1.f);
    specimen(spectra,"spectra-wifi-eight",output);
    // A stronger newcomer must retire the weakest despite repeated cached
    // sightings of that same weaker identity later in the sorted scan.
    net::strongVisitor=true;specimen(spectra,"spectra-population-replacement",output,7);
    char expected[18];auto ap=net::accessPoint(20);
    snprintf(expected,sizeof expected,"%02x%02x%02x%02x%02x%02x",ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5]);
    bool found=false;for(auto &v:tested_spectra::S->v)found|=v.used&&!strcmp(v.id,expected);assert(found);
    net::strongVisitor=false;
    key(spectra,'b');assert(tested_spectra::S->band==tested_spectra::B_BLE);
    specimen(spectra,"spectra-ble-eight",output,8);
    found=false;for(auto &v:tested_spectra::S->v)found|=v.used&&strchr(v.id,':');assert(found);
    spectra.leave();assert(!net::radioOn&&!hal::enabled());
    spectra.enter();spectra.frame(hal::ms());hal::render(block,128);
    net::freezeObservation=true;net::observationAt=hal::ms();key(spectra,' ');
    hal::clockMs.fetch_add(26000);spectra.frame(hal::ms());
    for(auto &v:tested_spectra::S->v)assert(v.used&&!v.leaving);
    key(spectra,' ');specimen(spectra,"spectra-stale-departure",output,7);
    for(auto &v:tested_spectra::S->v)assert(!v.used);
    spectra.leave();net::freezeObservation=false;

    // ESP-NOW callback timestamps can likewise be one tick newer than the
    // frame clock. The packet remains fresh and must elect the remote leader.
    net::receiveOffsetMs=1;
    covenRite.enter();
    net::member(1,hal::ms()%coven::CycleMs,3);covenRite.frame(hal::ms());
    assert(!tested_coven::S->eldest&&tested_coven::S->rootPc==3);
    covenRite.leave();net::receiveOffsetMs=0;

    // Queue residence belongs to the protocol lease. A delayed packet must not
    // leave a ghost member in the musical/visual seat roster after Session
    // expires the authoritative peer.
    net::receiveOffsetMs=-1500;
    covenRite.enter();
    net::member(1,uint32_t(hal::ms()-1500)%coven::CycleMs,3);covenRite.frame(hal::ms());
    assert(tested_coven::S->session.members()==2);
    net::receiveOffsetMs=0;
    hal::clockMs.fetch_add(coven::GoneMs-1000);covenRite.frame(hal::ms());
    assert(tested_coven::S->session.members()==1);
    for(auto &x:tested_coven::S->soul)assert(!x.used||!x.member);
    covenRite.leave();

    covenRite.enter();pointer(covenRite,132,52);pointer(covenRite,132,52,false,false);
    assert(tested_coven::S->rootPc==10);
    pointer(covenRite,94,64);pointer(covenRite,94,64,false,false);
    pointer(covenRite,62,64,true,true,hal::ms());pointer(covenRite,62,64,false,false);
    specimen(covenRite,"coven-local-leader",output);
    net::member(1,2000,3);covenRite.frame(hal::ms());assert(!tested_coven::S->eldest&&tested_coven::S->rootPc==3);
    net::member(2,5000,8);covenRite.frame(hal::ms());assert(tested_coven::S->rootPc==3);
    key(covenRite,0,hal::N_RIGHT,true);assert(tested_coven::S->rootPc==3);
    net::speech();covenRite.frame(hal::ms());specimen(covenRite,"coven-following-and-voice",output);
    for(int i=0;i<100;++i){hal::clockMs.fetch_add(1000);key(covenRite,' ');}
    assert(tested_coven::S->droppedCommands>0);
    covenRite.leave();assert(!hal::enabled()&&!net::radioOn);

    // Actual app render callbacks, concurrent with their UI/key/pointer/frame
    // paths. Mock radio belongs exclusively to UI just as the real poll seam.
    for(const Rite *r:rites){
        r->enter();std::atomic<bool> done{false};
        std::thread worker([&]{int16_t out[128];while(!done.load(std::memory_order_acquire))hal::render(out,128);});
        for(uint32_t i=0;i<5000;++i){
            hal::clockMs.fetch_add(16);
            if(r==&spectra){
                key(*r,char("1234wx b"[i%8]));
                key(*r,0,i%2?hal::N_LEFT:hal::N_RIGHT);
                if(i%9==0)pointer(*r,168+i%67,98);
            }else{
                key(*r,char("awsed1234 mxz"[i%13]));
                key(*r,0,i%2?hal::N_LEFT:hal::N_RIGHT,true);
                if(i%13==0)net::member(1,hal::ms()%coven::CycleMs,uint8_t(i%12),uint8_t(i%2));
                if(i%21==0)net::speech();
            }
            r->frame(hal::ms());
        }
        // Detach and destroy while audio worker remains live; HAL fence must
        // prevent any final render dereference of the retired state.
        r->leave();done.store(true,std::memory_order_release);worker.join();
    }
    for(int repeat=0;repeat<150;++repeat)for(auto r:rites){
        r->enter();r->frame(hal::ms());hal::render(block,128);r->leave();assert(!hal::enabled()&&!net::radioOn);
    }
    assert(audioAllocations.load()==0);
    printf("PASS actual radio instruments: 2 allocation failures, startup-failure autonomous audio, 6 rendered audio specimens, future-timestamp admission, authoritative delayed-member expiry, 10000 concurrent UI iterations, 300 lifecycle repeats, bounded command overflow, stale/departing population, callback_allocations=0; state_bytes coven=%zu spectra=%zu\n",sizeof(tested_coven::State),sizeof(tested_spectra::State));
}
