// Explicit, allowlisted board-to-board transport measurements. Never auto-join,
// scan, print credentials, persist configuration, or emit worker UART output.
#ifdef GRIM_PERF_LINK
#include "perf_link.h"
#include "perf_ble.h"
#include "perf_output.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_rom_crc.h>
#include <esp_system.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <cctype>

namespace perf_link {
namespace {
constexpr uint32_t Magic = 0x314c5644; // DVL1; all fields little-endian.
constexpr unsigned MaxPayload = 1024, MaxFrame = 1064, RxDepth = 8, Batch = 8;
constexpr uint64_t ReplyDrainUs = 500000;
#ifdef GRIM_PERF_COLOR
constexpr size_t LinkInfoLineCapacity = 4096;
constexpr size_t LinkSummaryLineCapacity = 3072;
#else
constexpr size_t LinkInfoLineCapacity = 6144;
constexpr size_t LinkSummaryLineCapacity = 6144;
#endif
enum class Transport { None, Espnow, Udp, Tcp, Ble };
enum class Role { Idle, Initiator, Responder };
enum class Mode { Off, Espnow, Ap, Sta, Ble };
#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    uint8_t kind, flags; // 1=data, 2=echo; flag bit 0=warmup.
    uint16_t payload;
    uint8_t token[16];
    uint32_t sequence;
    uint64_t sentUs;
    uint32_t crc;
};
#pragma pack(pop)
static_assert(sizeof(Header) == 40);
struct Rx { uint16_t size; uint8_t bytes[250]; };
struct Stats {
    uint32_t txAttempts=0, txPackets=0, sendErrors=0, pacingLate=0;
    uint32_t rxValid=0, rxUnique=0, duplicates=0, reordered=0, tooOld=0;
    uint64_t txPayload=0, rxPayload=0;
    uint32_t invalid=0, crcErrors=0, wrongToken=0, wrongPeer=0;
    uint32_t highSequence=0; uint64_t seen=0;
    uint32_t rttCount=0, rttMin=0, rttMax=0, rttBins[32]{};
    uint64_t rttSum=0, jitterSum=0;
    uint32_t previousRtt=0, jitterCount=0, jitterMax=0;
    uint64_t firstRxUs=0, lastRxUs=0;
    uint32_t minHeap=UINT32_MAX, minLargest=UINT32_MAX;
};
SemaphoreHandle_t control = nullptr;
TaskHandle_t workerHandle = nullptr;
QueueHandle_t rxQueue = nullptr;
portMUX_TYPE countersMux = portMUX_INITIALIZER_UNLOCKED;
Stats stats;
std::atomic<bool> callbacksEnabled{false};
std::atomic<uint32_t> callbackDrops{0}, macSuccess{0}, macFailure{0};
std::atomic<uint32_t> disconnectReason{0}, disconnectCount{0};
uint64_t callbackBegin=0, callbackEnd=0;
Mode mode=Mode::Off;
Transport transport=Transport::None;
Role role=Role::Idle;
Role reportedRole=Role::Idle;
bool nowReady=false, configured=false, armed=false, complete=false, echo=false;
std::atomic<bool> started{false};
bool tcpConnected=false, pendingSteadyReset=false;
uint8_t peerMac[6]{}, token[16]{};
IPAddress peerIp;
uint16_t port=41832;
char ssid[33]{}, password[65]{}, runId[32]="none";
unsigned payload=64, pps=50;
uint32_t nextSequence=0;
uint64_t steadyBegin=0, endAt=0, nextSend=0;
int socketFd=-1, acceptedFd=-1;
uint8_t txBuffer[MaxFrame], rxBuffer[MaxFrame];
unsigned tcpRxBytes=0, tcpExpected=sizeof(Header), tcpTxBytes=0, tcpTxPosition=0;
int lastError=0;
const char *lastOperation="none";
int stopNowRx=INT32_MIN,stopNowTx=INT32_MIN,stopNow=INT32_MIN,stopHostDisable=INT32_MIN,stopHostDeinit=INT32_MIN,stopControllerDisable=INT32_MIN,stopControllerDeinit=INT32_MIN,stopWifi=INT32_MIN;

const char *transportName() { return transport==Transport::Espnow?"espnow":transport==Transport::Udp?"udp":transport==Transport::Tcp?"tcp":transport==Transport::Ble?"ble":"none"; }
const char *modeName() { return mode==Mode::Espnow?"espnow":mode==Mode::Ap?"ap":mode==Mode::Sta?"sta":mode==Mode::Ble?"ble":"off"; }
const char *roleName() { return role==Role::Initiator?"initiator":role==Role::Responder?"responder":"idle"; }
bool check(int code, const char *operation) { if (code!=ESP_OK) { lastError=code; lastOperation=operation; return false; } return true; }
void ack(const char *action, bool okay) { perf_output::Line line(384); line.printf("PERF {\"schema\":1,\"type\":\"link_ack\",\"action\":\"%s\",\"ok\":%s,\"code\":%d,\"operation\":\"%s\"}\n",action,okay?"true":"false",okay?0:lastError,okay?"none":lastOperation); line.send(); }
void closeFd(int &fd) { if(fd>=0) { shutdown(fd,SHUT_RDWR); close(fd); fd=-1; } }
void stopService() {
    armed=started=false; role=Role::Idle;
    callbacksEnabled.store(false);
    // The adapter owns callbacks/host state and must shut down before direct
    // Bluedroid/controller fallback cleanup in setMode().
    if(mode==Mode::Ble)perf_ble::stop();
    closeFd(acceptedFd); closeFd(socketFd);
    tcpConnected=false; tcpRxBytes=tcpTxBytes=tcpTxPosition=0; tcpExpected=sizeof(Header);
    if(rxQueue)xQueueReset(rxQueue);
    portENTER_CRITICAL(&countersMux); callbackBegin=callbackEnd=0; portEXIT_CRITICAL(&countersMux);
}
void resetStats(bool resetCallbacks=true) {
    portENTER_CRITICAL(&countersMux); stats={}; portEXIT_CRITICAL(&countersMux);
    if(resetCallbacks) { callbackDrops.store(0); macSuccess.store(0); macFailure.store(0); }
}
bool decodeHex(const char *s, uint8_t *out, size_t capacity, size_t &written) {
    const size_t n=strlen(s); written=0;
    if(n%2 || n/2>capacity) return false;
    auto nibble=[](char c)->int { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; };
    for(size_t i=0;i<n;i+=2) { int a=nibble(s[i]),b=nibble(s[i+1]); if(a<0||b<0)return false; out[written++]=uint8_t(a*16+b); }
    return true;
}
bool validId(const char *s) { for(;*s;++s)if(!isalnum(uint8_t(*s))&&*s!='_'&&*s!='-')return false; return true; }
uint32_t packetCrc(uint8_t *bytes, unsigned size) {
    Header h; memcpy(&h,bytes,sizeof(h)); const uint32_t old=h.crc; h.crc=0; memcpy(bytes,&h,sizeof(h));
    uint32_t crc=esp_rom_crc32_le(0,bytes,size); h.crc=old; memcpy(bytes,&h,sizeof(h)); return crc;
}
bool unique(Stats &s,uint32_t sequence) {
    if(!s.seen) { s.highSequence=sequence; s.seen=1; return true; }
    const int32_t difference=int32_t(sequence-s.highSequence);
    if(difference>0) { s.seen= difference>=64?1:(s.seen<<difference)|1; s.highSequence=sequence; return true; }
    const uint32_t age=s.highSequence-sequence;
    if(age>=64) { ++s.tooOld; return false; }
    const uint64_t bit=uint64_t(1)<<age;
    if(s.seen&bit) { ++s.duplicates; return false; }
    s.seen|=bit; ++s.reordered; return true;
}
void sent(const Header &h,bool okay) {
    if(h.flags&1)return;
    portENTER_CRITICAL(&countersMux);
    ++stats.txAttempts;
    if(okay) { ++stats.txPackets; stats.txPayload+=h.payload; } else ++stats.sendErrors;
    portEXIT_CRITICAL(&countersMux);
}
void noteReceive(uint8_t *bytes,unsigned size,bool &respond) {
    respond=false;
    if(size<sizeof(Header)) { portENTER_CRITICAL(&countersMux); ++stats.invalid; portEXIT_CRITICAL(&countersMux); return; }
    Header h; memcpy(&h,bytes,sizeof(h));
    if(h.magic!=Magic || h.payload>MaxPayload || size!=sizeof(h)+h.payload ||
       (role==Role::Initiator?h.kind!=2:h.kind!=1)) { portENTER_CRITICAL(&countersMux); ++stats.invalid; portEXIT_CRITICAL(&countersMux); return; }
    if(memcmp(h.token,token,sizeof(token))) { portENTER_CRITICAL(&countersMux); ++stats.wrongToken; portEXIT_CRITICAL(&countersMux); return; }
    if(packetCrc(bytes,size)!=h.crc) { portENTER_CRITICAL(&countersMux); ++stats.crcErrors; portEXIT_CRITICAL(&countersMux); return; }
    // Warmup payloads are echoed but never enter measured receiver counters.
    respond=role==Role::Responder&&h.kind==1&&(h.flags&2);
    if(h.flags&1)return;
    const uint64_t at=esp_timer_get_time();
    if(role==Role::Responder&&h.kind==1) { payload=h.payload; echo=(h.flags&2)!=0; }
    if(role==Role::Initiator&&(!started||at>endAt+ReplyDrainUs))return;
    portENTER_CRITICAL(&countersMux);
    ++stats.rxValid;
    if(unique(stats,h.sequence)) {
        ++stats.rxUnique; stats.rxPayload+=h.payload;
        if(!stats.firstRxUs)stats.firstRxUs=at;
        stats.lastRxUs=at;
        if(role==Role::Initiator && h.kind==2 && h.sentUs<=at && h.sentUs>=steadyBegin && h.sentUs<endAt) {
            const uint32_t rtt=uint32_t(at-h.sentUs);
            if(!stats.rttCount||rtt<stats.rttMin)stats.rttMin=rtt;
            stats.rttMax=std::max(stats.rttMax,rtt); stats.rttSum+=rtt;
            if(stats.rttCount) { uint32_t difference=rtt>stats.previousRtt?rtt-stats.previousRtt:stats.previousRtt-rtt; stats.jitterSum+=difference; stats.jitterMax=std::max(stats.jitterMax,difference); ++stats.jitterCount; }
            stats.previousRtt=rtt; ++stats.rttCount;
            unsigned bin=rtt>1?std::min<unsigned>(31,32-__builtin_clz(rtt-1)):0; ++stats.rttBins[bin];
        }
    }
    portEXIT_CRITICAL(&countersMux);
}
void nowReceive(const uint8_t *mac,const uint8_t *data,int length) {
    if(!callbacksEnabled.load(std::memory_order_relaxed)||length<40||length>250)return;
    bool allowed;
    portENTER_CRITICAL(&countersMux); allowed=!memcmp(mac,peerMac,6); portEXIT_CRITICAL(&countersMux);
    if(!allowed)return;
    Rx item; item.size=uint16_t(length); memcpy(item.bytes,data,length);
    if(rxQueue&&xQueueSend(rxQueue,&item,0)!=pdTRUE)callbackDrops.fetch_add(1,std::memory_order_relaxed);
}
void nowSent(const uint8_t *mac,esp_now_send_status_t result) {
    if(!callbacksEnabled.load(std::memory_order_relaxed))return;
    const uint64_t at=esp_timer_get_time();
    portENTER_CRITICAL(&countersMux);
    bool count=!memcmp(mac,peerMac,6)&&at>=callbackBegin&&at<callbackEnd;
    portEXIT_CRITICAL(&countersMux);
    if(count) { if(result==ESP_NOW_SEND_SUCCESS)macSuccess.fetch_add(1); else macFailure.fetch_add(1); }
}
sockaddr_in address(IPAddress ip) { sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(port); a.sin_addr.s_addr=uint32_t(ip); return a; }
bool nonblocking(int fd) { return fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK)>=0; }
bool openSocket(bool server) {
    closeFd(acceptedFd); closeFd(socketFd); tcpConnected=false; tcpRxBytes=tcpTxBytes=tcpTxPosition=0; tcpExpected=sizeof(Header);
    socketFd=socket(AF_INET,transport==Transport::Tcp?SOCK_STREAM:SOCK_DGRAM,0);
    if(socketFd<0) { lastError=errno; lastOperation="socket"; return false; }
    if(!nonblocking(socketFd)) { lastError=errno; lastOperation="nonblocking"; closeFd(socketFd); return false; }
    if(transport==Transport::Udp||server) {
        int yes=1; setsockopt(socketFd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
        sockaddr_in local=address(IPAddress(uint32_t(INADDR_ANY)));
        if(bind(socketFd,reinterpret_cast<sockaddr*>(&local),sizeof(local))) { lastError=errno; lastOperation="bind"; closeFd(socketFd); return false; }
    }
    if(transport==Transport::Tcp) {
        int yes=1; setsockopt(socketFd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
        if(server) { if(listen(socketFd,1)) { lastError=errno; lastOperation="listen"; closeFd(socketFd); return false; } }
        else {
            sockaddr_in remote=address(peerIp);
            if(connect(socketFd,reinterpret_cast<sockaddr*>(&remote),sizeof(remote))==0)tcpConnected=true;
            else if(errno!=EINPROGRESS&&errno!=EWOULDBLOCK) { lastError=errno; lastOperation="connect"; closeFd(socketFd); return false; }
        }
    }
    return true;
}
bool sendFrame(uint8_t *bytes,unsigned size) {
    Header h; memcpy(&h,bytes,sizeof(h));
    bool okay=false;
    if(transport==Transport::Ble)okay=perf_ble::send(bytes,size);
    else if(transport==Transport::Espnow)okay=esp_now_send(peerMac,bytes,size)==ESP_OK;
    else if(transport==Transport::Udp) { sockaddr_in a=address(peerIp); okay=sendto(socketFd,bytes,size,MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&a),sizeof(a))==int(size); }
    else {
        const int fd=role==Role::Responder?acceptedFd:socketFd;
        if(fd>=0&&tcpConnected&&tcpTxBytes==0) {
            if(bytes!=txBuffer)memcpy(txBuffer,bytes,size);
            tcpTxBytes=size; tcpTxPosition=0;
            return true; // Count only when the complete application frame is accepted.
        }
        return false;
    }
    sent(h,okay); return okay;
}
void receiveOne(uint8_t *bytes,unsigned size) {
    bool respond; noteReceive(bytes,size,respond);
    if(respond) {
        Header h; memcpy(&h,bytes,sizeof(h)); h.kind=2; h.crc=0; memcpy(bytes,&h,sizeof(h));
        h.crc=packetCrc(bytes,size); memcpy(bytes,&h,sizeof(h)); sendFrame(bytes,size);
    }
}
void bleReceive(const uint8_t *bytes,size_t size) {
    if(size>sizeof(rxBuffer))return;
    memcpy(rxBuffer,bytes,size); receiveOne(rxBuffer,unsigned(size));
}
void serviceTcp() {
    if(role==Role::Responder&&acceptedFd<0) {
        sockaddr_in remote{}; socklen_t size=sizeof(remote);
        int fd=accept(socketFd,reinterpret_cast<sockaddr*>(&remote),&size);
        if(fd>=0) {
            if(remote.sin_addr.s_addr!=uint32_t(peerIp)) { close(fd); portENTER_CRITICAL(&countersMux); ++stats.wrongPeer; portEXIT_CRITICAL(&countersMux); }
            else if(!nonblocking(fd)) { lastError=errno; lastOperation="accepted_nonblocking"; close(fd); }
            else { acceptedFd=fd; tcpConnected=true; int yes=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes)); }
        }
    } else if(role==Role::Initiator&&!tcpConnected&&socketFd>=0) {
        fd_set writeSet; FD_ZERO(&writeSet); FD_SET(socketFd,&writeSet); timeval timeout{};
        if(select(socketFd+1,nullptr,&writeSet,nullptr,&timeout)>0) {
            int error=0; socklen_t n=sizeof(error);
            if(getsockopt(socketFd,SOL_SOCKET,SO_ERROR,&error,&n)<0)error=errno;
            if(!error)tcpConnected=true;
            else { lastError=error; lastOperation="tcp_connect"; closeFd(socketFd); }
        }
    }
    const int fd=role==Role::Responder?acceptedFd:socketFd;
    if(fd<0||!tcpConnected)return;
    if(tcpTxBytes) {
        int n=send(fd,txBuffer+tcpTxPosition,tcpTxBytes-tcpTxPosition,MSG_DONTWAIT);
        if(n>0)tcpTxPosition+=unsigned(n);
        else if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK) { lastError=errno; lastOperation="tcp_send"; Header h; memcpy(&h,txBuffer,sizeof(h)); sent(h,false); tcpTxBytes=0; tcpConnected=false; }
        if(tcpTxBytes&&tcpTxPosition==tcpTxBytes) { Header h; memcpy(&h,txBuffer,sizeof(h)); sent(h,true); tcpTxBytes=tcpTxPosition=0; }
    }
    for(unsigned i=0;i<Batch;++i) {
        int n=recv(fd,rxBuffer+tcpRxBytes,tcpExpected-tcpRxBytes,MSG_DONTWAIT);
        if(n<=0) { if(n==0)tcpConnected=false; break; }
        tcpRxBytes+=unsigned(n);
        if(tcpRxBytes==sizeof(Header)&&tcpExpected==sizeof(Header)) {
            Header h; memcpy(&h,rxBuffer,sizeof(h));
            if(h.magic!=Magic||h.payload>MaxPayload) { tcpConnected=false; lastError=EINVAL; lastOperation="tcp_frame"; break; }
            tcpExpected=sizeof(Header)+h.payload;
        }
        if(tcpRxBytes==tcpExpected) { receiveOne(rxBuffer,tcpRxBytes); tcpRxBytes=0; tcpExpected=sizeof(Header); }
    }
}
void serviceReceive() {
    if(transport==Transport::Ble) { perf_ble::poll(); return; }
    if(transport==Transport::Tcp) { serviceTcp(); return; }
    for(unsigned i=0;i<Batch;++i) {
        if(transport==Transport::Espnow) { Rx item; if(!rxQueue||xQueueReceive(rxQueue,&item,0)!=pdTRUE)break; receiveOne(item.bytes,item.size); }
        else {
            sockaddr_in remote{}; socklen_t n=sizeof(remote);
            int size=recvfrom(socketFd,rxBuffer,sizeof(rxBuffer),MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&remote),&n);
            if(size<0)break;
            if(remote.sin_addr.s_addr!=uint32_t(peerIp)||ntohs(remote.sin_port)!=port) { portENTER_CRITICAL(&countersMux); ++stats.wrongPeer; portEXIT_CRITICAL(&countersMux); continue; }
            receiveOne(rxBuffer,unsigned(size));
        }
    }
}
void makePacket(bool warm) {
    Header h{}; h.magic=Magic; h.kind=1; h.flags=(warm?1:0)|(echo?2:0); h.payload=payload;
    memcpy(h.token,token,16); h.sequence=nextSequence++; h.sentUs=esp_timer_get_time();
    memcpy(txBuffer,&h,sizeof(h));
    for(unsigned i=0;i<payload;++i)txBuffer[sizeof(h)+i]=uint8_t((h.sequence*31+i)^0x5a);
    h.crc=packetCrc(txBuffer,sizeof(h)+payload); memcpy(txBuffer,&h,sizeof(h));
}
void worker(void *) {
    for(;;) {
        if(control&&xSemaphoreTake(control,0)==pdTRUE) {
            if(role!=Role::Idle) {
                uint64_t now=esp_timer_get_time();
                if(role==Role::Initiator&&started&&now>=endAt+ReplyDrainUs) {
                    // Bound application traffic and tail processing even when a
                    // TCP peer stops draining. No frame is retried indefinitely.
                    if(tcpTxBytes) { Header h; memcpy(&h,txBuffer,sizeof(h)); sent(h,false); tcpTxBytes=tcpTxPosition=0; }
                    if(transport==Transport::Tcp) { closeFd(socketFd); tcpConnected=false; }
                    started=false; armed=false; complete=true;
                }
                if(role==Role::Responder||!complete)serviceReceive();
                now=esp_timer_get_time();
                if(role==Role::Initiator&&started) {
                    if(pendingSteadyReset&&now>=steadyBegin) { resetStats(false); nextSequence=0; nextSend=now; pendingSteadyReset=false; }
                    if(now<endAt) {
                        for(unsigned i=0;i<Batch;++i) {
                            now=esp_timer_get_time();
                            if(now>=endAt||(pps&&now<nextSend)||(transport==Transport::Tcp&&(!tcpConnected||tcpTxBytes)))break;
                            const bool warm=now<steadyBegin;
                            if(!warm&&pps&&now>nextSend+1000000/pps) { portENTER_CRITICAL(&countersMux); ++stats.pacingLate; portEXIT_CRITICAL(&countersMux); }
                            makePacket(warm); sendFrame(txBuffer,sizeof(Header)+payload);
                            if(pps)nextSend=now+1000000/pps;
                            if(transport==Transport::Tcp)serviceTcp();
                        }
                    }
                }
                const uint32_t heap=ESP.getFreeHeap(), largest=ESP.getMaxAllocHeap();
                portENTER_CRITICAL(&countersMux); stats.minHeap=std::min(stats.minHeap,heap); stats.minLargest=std::min(stats.minLargest,largest); portEXIT_CRITICAL(&countersMux);
            }
            xSemaphoreGive(control);
        }
        vTaskDelay(1);
    }
}
Transport parseTransport(const char *s) { return !strcmp(s,"espnow")?Transport::Espnow:!strcmp(s,"udp")?Transport::Udp:!strcmp(s,"tcp")?Transport::Tcp:!strcmp(s,"ble")?Transport::Ble:Transport::None; }
bool setMode(const char *requested,unsigned channel) {
    stopService(); callbacksEnabled.store(false);
    bool stopped=true;
    stopNowRx=stopNowTx=stopNow=stopHostDisable=stopHostDeinit=stopControllerDisable=stopControllerDeinit=stopWifi=INT32_MIN;
    if(nowReady) {
        stopNowRx=esp_now_unregister_recv_cb(); stopped=check(stopNowRx,"espnow_unregister_recv")&&stopped;
        stopNowTx=esp_now_unregister_send_cb(); stopped=check(stopNowTx,"espnow_unregister_send")&&stopped;
        stopNow=esp_now_deinit(); stopped=check(stopNow,"espnow_deinit")&&stopped;
        nowReady=false;
    }
    if(rxQueue)xQueueReset(rxQueue);
    // Suppress SDK association logs, which can contain SSIDs. Safe summaries
    // contain checked API status codes instead. No worker logs are emitted.
    esp_log_level_set("*",ESP_LOG_NONE);
    if(esp_bluedroid_get_status()==ESP_BLUEDROID_STATUS_ENABLED) { stopHostDisable=esp_bluedroid_disable(); stopped=check(stopHostDisable,"bluedroid_disable")&&stopped; }
    if(esp_bluedroid_get_status()==ESP_BLUEDROID_STATUS_INITIALIZED) { stopHostDeinit=esp_bluedroid_deinit(); stopped=check(stopHostDeinit,"bluedroid_deinit")&&stopped; }
    if(esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_ENABLED) { stopControllerDisable=esp_bt_controller_disable(); stopped=check(stopControllerDisable,"bt_controller_disable")&&stopped; }
    if(esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_INITED) { stopControllerDeinit=esp_bt_controller_deinit(); stopped=check(stopControllerDeinit,"bt_controller_deinit")&&stopped; }
    WiFi.persistent(false); WiFi.setAutoReconnect(false); WiFi.disconnect(false,false);
    stopWifi=WiFi.mode(WIFI_OFF)?ESP_OK:ESP_FAIL; stopped=check(stopWifi,"wifi_off")&&stopped;
    if(!stopped)return false;
    mode=Mode::Off;
    disconnectReason.store(0); disconnectCount.store(0);
    if(!strcmp(requested,"off"))return true;
    if(!strcmp(requested,"ble")) { mode=Mode::Ble; return true; }
    if(channel>13) { lastError=EINVAL; lastOperation="channel"; return false; }
    if(strcmp(requested,"espnow")&&strcmp(requested,"ap")&&strcmp(requested,"sta"))return false;
    if(!strcmp(requested,"ap")&&!ssid[0]) { lastError=EINVAL; lastOperation="provision_required"; return false; }
    if(!WiFi.mode(!strcmp(requested,"ap")?WIFI_AP:WIFI_STA)) { lastError=ESP_FAIL; lastOperation="wifi_mode"; return false; }
    if(!check(esp_wifi_set_storage(WIFI_STORAGE_RAM),"storage_RAM"))return false;
    if(strcmp(requested,"ap")) {
        if(!check(esp_wifi_set_ps(WIFI_PS_NONE),"power_save_none"))return false;
        WiFi.setSleep(false);
    }
    if(!strcmp(requested,"espnow")) {
        if(!channel||!rxQueue) { lastError=EINVAL; lastOperation="espnow_channel_or_queue"; return false; }
        if(!check(esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE),"set_channel")||!check(esp_now_init(),"espnow_init"))return false;
        nowReady=true;
        if(!check(esp_now_register_recv_cb(nowReceive),"espnow_recv_callback")||!check(esp_now_register_send_cb(nowSent),"espnow_send_callback"))return false;
        mode=Mode::Espnow;
    } else if(!strcmp(requested,"ap")) {
        if(!WiFi.softAP(ssid,password[0]?password:nullptr,channel?channel:1,false,1)) { lastError=ESP_FAIL; lastOperation="softap_start"; return false; }
        mode=Mode::Ap;
    } else {
        if(!ssid[0]) { lastError=EINVAL; lastOperation="provision_required"; return false; }
        mode=Mode::Sta;
        if(WiFi.begin(ssid,password,channel)==WL_CONNECT_FAILED) { lastError=ESP_FAIL; lastOperation="sta_begin"; return false; }
    }
    return true;
}
bool prepareService(Transport t,bool server) {
    if(!configured||t==Transport::None) { lastError=EINVAL; lastOperation="allowlist_required"; return false; }
    transport=t;
    if(t==Transport::Ble) {
        if(mode!=Mode::Ble) { lastError=EINVAL; lastOperation="ble_mode_required"; return false; }
        if(!perf_ble::begin(server,peerMac,bleReceive)) { const auto state=perf_ble::info(); lastError=state.last_error; lastOperation=state.stage; return false; }
    } else if(t==Transport::Espnow) {
        if(!nowReady||mode!=Mode::Espnow) { lastError=EINVAL; lastOperation="espnow_mode_required"; return false; }
        if(esp_now_is_peer_exist(peerMac))esp_now_del_peer(peerMac);
        esp_now_peer_info_t peer{}; memcpy(peer.peer_addr,peerMac,6); peer.channel=0; peer.ifidx=WIFI_IF_STA; peer.encrypt=false;
        if(!check(esp_now_add_peer(&peer),"espnow_add_known_peer"))return false;
        callbacksEnabled.store(true);
    } else {
        if(mode!=Mode::Ap&&WiFi.status()!=WL_CONNECTED) { lastError=ENOTCONN; lastOperation="association_required"; return false; }
        if(!openSocket(server))return false;
    }
    return true;
}
void info() {
    uint8_t sta[6],ap[6],bt[6]; esp_read_mac(sta,ESP_MAC_WIFI_STA); esp_read_mac(ap,ESP_MAC_WIFI_SOFTAP); esp_read_mac(bt,ESP_MAC_BT);
    const auto ble=perf_ble::info();
    uint8_t channel=0; wifi_second_chan_t second; wifi_ps_type_t ps=WIFI_PS_NONE;
    int channelCode=esp_wifi_get_channel(&channel,&second),psCode=esp_wifi_get_ps(&ps);
    bool connected=WiFi.status()==WL_CONNECTED;
    wifi_mode_t actualMode=WIFI_MODE_NULL; int modeCode=esp_wifi_get_mode(&actualMode);
    bool ready=mode==Mode::Ble?ble.ready:mode==Mode::Espnow?(nowReady&&channelCode==ESP_OK&&actualMode==WIFI_MODE_STA):mode==Mode::Ap?(modeCode==ESP_OK&&actualMode==WIFI_MODE_AP):connected;
    char rssi[16]; if(connected)snprintf(rssi,sizeof(rssi),"%d",WiFi.RSSI()); else strcpy(rssi,"null");
    String ip=(mode==Mode::Ap?WiFi.softAPIP():WiFi.localIP()).toString();
    perf_output::Line line(LinkInfoLineCapacity);
    line.printf("PERF {\"schema\":1,\"type\":\"link_info\",\"mode\":\"%s\",\"mode_ready\":%s,\"connected\":%s,\"wifi_status\":%u,\"disconnect_reason\":%u,\"disconnect_count\":%u,\"active\":%s,\"role\":\"%s\",\"protocol\":\"%s\",\"tcp_connected\":%s,\"sta_mac\":\"%02x%02x%02x%02x%02x%02x\",\"ap_mac\":\"%02x%02x%02x%02x%02x%02x\",\"bt_mac\":\"%02x%02x%02x%02x%02x%02x\",\"ip\":\"%s\",\"actual_channel\":%u,\"channel_status\":%d,\"power_save_enum\":%u,\"power_save_status\":%d,\"rssi_dbm\":%s,\"heap_free_bytes\":%u,\"heap_largest_bytes\":%u,\"worker_stack_min_bytes\":%u,\"ble_compiled\":true,\"ble_initialized\":%s,\"ble_ready\":%s,\"ble_connected\":%s,\"ble_mtu\":%u,\"ble_actual_payload_limit\":%u,\"ble_congested\":%s,\"ble_notifications_enabled\":%s,\"ble_requested_mtu\":%u,\"ble_rx_queue_drops\":%u,\"ble_event_queue_drops\":%u,\"ble_tx_failures\":%u,\"ble_last_error\":%d,\"ble_stage\":\"%s\",\"ble_conn_params_valid\":%s,\"ble_actual_interval_units_1_25ms\":%u,\"ble_actual_latency\":%u,\"ble_actual_timeout_units_10ms\":%u,\"worker_core\":1,\"worker_priority\":1,\"rx_queue_depth\":%u,\"max_batch\":%u,\"max_payload_espnow\":200,\"max_payload_socket\":1024,\"bt_controller_status\":%u,\"bluedroid_status\":%u,\"stop_status_not_called\":-2147483648,\"stop_espnow_rx_status\":%d,\"stop_espnow_tx_status\":%d,\"stop_espnow_deinit_status\":%d,\"stop_bluedroid_disable_status\":%d,\"stop_bluedroid_deinit_status\":%d,\"stop_bt_disable_status\":%d,\"stop_bt_deinit_status\":%d,\"stop_wifi_status\":%d,\"header_bytes\":40,\"wire_endian\":\"little\",\"checksum\":\"esp_rom_crc32_le_seed0_header_crc_zero_plus_payload\"}\n",
        modeName(),ready?"true":"false",connected?"true":"false",unsigned(WiFi.status()),disconnectReason.load(),disconnectCount.load(),(started||role==Role::Responder)?"true":"false",roleName(),transportName(),tcpConnected?"true":"false",sta[0],sta[1],sta[2],sta[3],sta[4],sta[5],ap[0],ap[1],ap[2],ap[3],ap[4],ap[5],bt[0],bt[1],bt[2],bt[3],bt[4],bt[5],ip.c_str(),channel,channelCode,unsigned(ps),psCode,rssi,ESP.getFreeHeap(),ESP.getMaxAllocHeap(),workerHandle?uxTaskGetStackHighWaterMark(workerHandle):0,ble.initialized?"true":"false",ble.ready?"true":"false",ble.connected?"true":"false",ble.mtu,std::min<unsigned>(200,ble.mtu>43?ble.mtu-43:0),ble.congested?"true":"false",ble.notifications_enabled?"true":"false",ble.requested_mtu,ble.rx_drops,ble.event_drops,ble.tx_failures,ble.last_error,ble.stage,ble.connection_params_valid?"true":"false",ble.actual_interval,ble.actual_latency,ble.actual_timeout,RxDepth,Batch,unsigned(esp_bt_controller_get_status()),unsigned(esp_bluedroid_get_status()),stopNowRx,stopNowTx,stopNow,stopHostDisable,stopHostDeinit,stopControllerDisable,stopControllerDeinit,stopWifi);
    line.send();
}
void exportStats() {
    Stats s; portENTER_CRITICAL(&countersMux); s=stats; portEXIT_CRITICAL(&countersMux);
    perf_output::Line line(LinkSummaryLineCapacity);
    line.printf("PERF {\"schema\":1,\"type\":\"link_summary\",\"run_id\":\"%s\",\"role\":\"%s\",\"protocol\":\"%s\",\"pattern\":\"%s\",\"complete\":%s,\"steady_begin_us\":%llu,\"end_us\":%llu,\"duration_us\":%llu,\"payload_bytes\":%u,\"offered_pps\":%u,\"tx_attempts\":%u,\"tx_api_accepted_packets\":%u,\"tx_payload_bytes\":%llu,\"send_errors\":%u,\"pacing_late\":%u,\"rx_valid_packets\":%u,\"rx_unique_packets\":%u,\"rx_payload_bytes\":%llu,\"duplicates\":%u,\"reordered\":%u,\"too_old\":%u,\"invalid_packets\":%u,\"crc_errors\":%u,\"wrong_token\":%u,\"wrong_peer\":%u,\"rx_span_us\":%llu,\"rtt_count\":%u,\"rtt_min_us\":%u,\"rtt_max_us\":%u,\"rtt_sum_us\":%llu,\"rtt_adjacent_abs_difference_count\":%u,\"rtt_adjacent_abs_difference_sum_us\":%llu,\"rtt_adjacent_abs_difference_max_us\":%u,\"mac_success_callback_time\":%u,\"mac_failure_callback_time\":%u,\"rx_callback_queue_drops_session_including_warmup\":%u,\"heap_free_min_bytes\":%u,\"heap_largest_min_bytes\":%u,\"last_error\":%d,\"last_operation\":\"%s\",\"sequence_window_packets\":64,\"mac_callback_scope\":\"initiator_steady_callback_time_responder_session_including_warmup\",\"rtt_scope\":\"packet_create_to_peer_validation_echo_to_local_validation\",\"reply_drain_us\":500000,\"tcp_retransmissions\":null,\"cpu_utilization\":null,\"receiver_window_assignment\":\"initiator_packet_warmup_flag_excluded_plus_reply_drain\",\"rtt_histogram_method\":\"ceil_log2_upper_us\",\"rtt_histogram_counts\":[",
        runId,(reportedRole==Role::Initiator?"initiator":reportedRole==Role::Responder?"responder":"idle"),transportName(),echo?"rtt":"oneway",complete?"true":"false",(unsigned long long)steadyBegin,(unsigned long long)endAt,(unsigned long long)(endAt>steadyBegin?endAt-steadyBegin:0),payload,pps,s.txAttempts,s.txPackets,(unsigned long long)s.txPayload,s.sendErrors,s.pacingLate,s.rxValid,s.rxUnique,(unsigned long long)s.rxPayload,s.duplicates,s.reordered,s.tooOld,s.invalid,s.crcErrors,s.wrongToken,s.wrongPeer,(unsigned long long)(s.lastRxUs>s.firstRxUs?s.lastRxUs-s.firstRxUs:0),s.rttCount,s.rttCount?s.rttMin:0,s.rttMax,(unsigned long long)s.rttSum,s.jitterCount,(unsigned long long)s.jitterSum,s.jitterMax,macSuccess.load(),macFailure.load(),callbackDrops.load(),s.minHeap==UINT32_MAX?0:s.minHeap,s.minLargest==UINT32_MAX?0:s.minLargest,lastError,lastOperation);
    for(unsigned i=0;i<32;++i)line.printf(i?",%u":"%u",s.rttBins[i]);
    line.println("]}");
    line.send();
}
}

void begin() {
    if(control)return;
    control=xSemaphoreCreateMutex(); rxQueue=xQueueCreate(RxDepth,sizeof(Rx));
    WiFi.onEvent([](WiFiEvent_t event,WiFiEventInfo_t info) {
        if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            disconnectReason.store(info.wifi_sta_disconnected.reason,std::memory_order_relaxed);
            disconnectCount.fetch_add(1,std::memory_order_relaxed);
        }
    });
    if(control)xTaskCreatePinnedToCore(worker,"perf-link",4096,nullptr,1,&workerHandle,1);
}
void poll() {}
bool active() { return started; }
void onRun(const char *id,uint64_t beginUs,uint64_t untilUs) {
    if(!control||xSemaphoreTake(control,pdMS_TO_TICKS(100))!=pdTRUE)return;
    if(armed&&role==Role::Initiator) {
        strncpy(runId,id,sizeof(runId)-1); steadyBegin=beginUs; endAt=untilUs; nextSend=esp_timer_get_time(); nextSequence=0;
        resetStats(); complete=false; started=true; pendingSteadyReset=true;
        portENTER_CRITICAL(&countersMux); callbackBegin=beginUs; callbackEnd=untilUs; portEXIT_CRITICAL(&countersMux);
    }
    xSemaphoreGive(control);
}
bool command(const char *line) {
    if(strncmp(line,"perf link ",10))return false;
    begin();
    if(!control||!workerHandle) { lastError=ENOMEM; lastOperation="worker_allocation"; ack("setup",false); return true; }
    if(xSemaphoreTake(control,pdMS_TO_TICKS(1000))!=pdTRUE) { lastError=EBUSY; lastOperation="control_busy"; ack("setup",false); return true; }
    char a[130]{},b[130]{},c[65]{},id[32]{}; unsigned first=0,second=0;
    bool okay=false; const char *action="unknown";
    if(!strcmp(line,"perf link info")) { info(); xSemaphoreGive(control); return true; }
    if(!strcmp(line,"perf link export")) {
        if(started||role==Role::Responder) { lastError=EBUSY; lastOperation="stop_or_finish_before_export"; xSemaphoreGive(control); ack("export",false); return true; }
        exportStats(); xSemaphoreGive(control); return true;
    }
    if(strcmp(line,"perf link stop")) { lastError=0; lastOperation="none"; }
    if(sscanf(line,"perf link provision %129s %129s",a,b)==2) {
        action="provision"; size_t n=0,m=0; uint8_t s[32]{},p[64]{};
        okay=decodeHex(a,s,sizeof(s),n)&&n&&(!strcmp(b,"-")||decodeHex(b,p,sizeof(p),m));
        if(okay&&memchr(s,0,n)==nullptr&&memchr(p,0,m)==nullptr) { memset(ssid,0,sizeof(ssid)); memset(password,0,sizeof(password)); memcpy(ssid,s,n); memcpy(password,p,m); }
        else okay=false;
        volatile uint8_t *wipe=s; for(auto &unused:s) { (void)unused; *wipe++=0; } wipe=p; for(auto &unused:p) { (void)unused; *wipe++=0; }
        memset(a,0,sizeof(a)); memset(b,0,sizeof(b));
    } else if(sscanf(line,"perf link mode %129s %u",a,&first)==2) { action="mode"; okay=setMode(a,first); }
    else if(sscanf(line,"perf link allow %129s %129s %u %64s",a,b,&first,c)==4) {
        action="allow"; size_t n=0,m=0; uint8_t mac[6],key[16]; IPAddress ip;
        okay=first>0&&first<65536&&decodeHex(a,mac,6,n)&&n==6&&decodeHex(c,key,16,m)&&m==16&&ip.fromString(b)&&!(mac[0]&1);
        if(okay) { portENTER_CRITICAL(&countersMux); memcpy(peerMac,mac,6); portEXIT_CRITICAL(&countersMux); memcpy(token,key,16); peerIp=ip; port=uint16_t(first); configured=true; }
        memset(c,0,sizeof(c)); memset(key,0,sizeof(key));
    } else if(sscanf(line,"perf link serve %31s %129s",id,a)==2) {
        action="serve"; stopService(); resetStats(); transport=parseTransport(a); role=reportedRole=Role::Responder; steadyBegin=endAt=0; payload=pps=0; echo=true;
        okay=validId(id)&&prepareService(transport,true);
        if(okay) {
            strncpy(runId,id,31); complete=false;
            portENTER_CRITICAL(&countersMux); callbackBegin=esp_timer_get_time(); callbackEnd=UINT64_MAX; portEXIT_CRITICAL(&countersMux);
        } else role=Role::Idle;
    } else if(sscanf(line,"perf link arm %129s %129s %u %u",a,b,&first,&second)==4) {
        action="arm"; stopService(); resetStats(); transport=parseTransport(a); payload=first; pps=second; echo=!strcmp(b,"rtt");
        okay=(!strcmp(b,"oneway")||echo)&&payload>0&&payload<=((transport==Transport::Espnow||transport==Transport::Ble)?200:1024)&&pps<=2000&&(pps||transport==Transport::Tcp);
        if(okay) { role=reportedRole=Role::Initiator; okay=prepareService(transport,false); armed=okay; } if(!okay)role=Role::Idle;
    } else if(sscanf(line,"perf link run %31s %u %u",id,&first,&second)==3) {
        action="run"; okay=armed&&validId(id)&&first<=60000&&second>0&&second<=600000;
        xSemaphoreGive(control);
        if(okay) { uint64_t at=esp_timer_get_time(); onRun(id,at+uint64_t(first)*1000,at+uint64_t(first+second)*1000); }
        ack(action,okay); return true;
    } else if(!strcmp(line,"perf link idle")) {
        action="idle";
        okay=!started&&!tcpTxBytes;
        if(okay) { armed=false; complete=false; portENTER_CRITICAL(&countersMux); callbackBegin=callbackEnd=0; portEXIT_CRITICAL(&countersMux); }
        else { lastError=EBUSY; lastOperation="finish_before_idle"; }
    } else if(!strcmp(line,"perf link stop")) { action="stop"; bool finished=complete||role==Role::Responder; stopService(); complete=finished; okay=true; }
    if(!okay&&!lastError) { lastError=EINVAL; lastOperation="invalid_configuration"; }
    xSemaphoreGive(control); ack(action,okay); return true;
}
}
#endif
