#ifdef GRIM_PERF_AUDIO_MVP
#include "perf_audio_mvp.h"
#include "perf_audio_core.h"
#include "perf_output.h"
#ifdef GRIM_PERF_LINK
#include "perf_link.h"
#endif
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <esp_system.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <cctype>

namespace perf_audio_mvp {
namespace {
namespace core = perf_audio_core;
constexpr unsigned RxDepth = 8, RxBatch = 8, Channel = 6, DelayBlocks = 2;
constexpr uint64_t StartupUs = 200000, DrainUs = 200000, SetupTimeoutUs = 5000000;
constexpr uint8_t Broadcast[6] = {255,255,255,255,255,255};
enum class Role { Idle, Tx, Rx };
struct RxPacket { uint64_t arrival; uint16_t bytes; uint8_t data[core::MaxPacketBytes]; };
struct Timings { uint32_t calls=0,max=0; uint64_t sum=0; void add(uint64_t n) { ++calls; sum+=n; max=std::max(max,uint32_t(n)); } };
struct Stats {
    uint32_t sourceBlocks=0,skippedSourceBlocks=0,expiredPreparedBlocks=0,encoderResets=0;
    uint32_t txAttempts=0,txAccepted=0,txNoMem=0,txErrors=0,txBytes=0;
    uint32_t callbackExpired=0,afterWindow=0,rendered=0,concealed=0;
    uint32_t firstSequence=0,lastSequence=0;
    uint64_t queueAgeMax=0,slotDwellMax=0;
    Timings encode,receive,render,quality,send;
    uint64_t qualitySamples[core::MaxChannels]{},referenceEnergy[core::MaxChannels]{},errorEnergy[core::MaxChannels]{};
    uint32_t errorAbsMax[core::MaxChannels]{};
    uint32_t heapMin=UINT32_MAX,largestMin=UINT32_MAX;
} stats;
SemaphoreHandle_t control=nullptr;
QueueHandle_t received=nullptr;
TaskHandle_t workerHandle=nullptr;
esp_timer_handle_t timer=nullptr;
core::Config config;
core::Source source,reference;
core::PdmEncoder encoder;
core::Receiver receiver;
int16_t input[core::MaxSamples]{},output[core::MaxSamples]{},expected[core::MaxSamples]{};
uint8_t encoded[core::MaxBlockBytes]{},packet[core::MaxPacketBytes]{};
std::atomic<bool> radioOwned{false},active{false},receiving{false},inflight{false};
std::atomic<uint32_t> queueDrops{0},wrongMac{0},sendCallbacks{0},callbackFailures{0},sendAt32{0},callbackLatencyMax{0};
bool configured=false,authorized=false,complete=false,timedOut=false,nowInitialized=false;
Role role=Role::Idle;
uint8_t peerMac[6]{};
uint32_t session=0,seed=23063,phy=6,durationMs=0,totalBlocks=0,nextSequence=0,preparedSequence=0;
unsigned nextFragment=0;
bool prepared=false;
uint64_t startAt=0,endAt=0,hardDeadline=0;
char runId[32]="none";
int lastError=0,rateStatus=ESP_ERR_INVALID_STATE;
const char *lastOperation="none";
const char *roleName() { return role==Role::Tx?"tx":role==Role::Rx?"rx":"idle"; }
const char *codecName() { return config.kind==core::Kind::Pdm?"pdm":"pcm16"; }
uint64_t atUs() { return uint64_t(esp_timer_get_time()); }
uint64_t blockOffset(uint64_t n) { return (n * config.frames * 1000000ull + config.rate-1) / config.rate; }
uint64_t periodCeil() { return (uint64_t(config.frames)*1000000+config.rate-1)/config.rate; }
bool checked(int code,const char *operation) { if(code!=ESP_OK) { lastError=code;lastOperation=operation;return false; }return true; }
void ack(const char *action,bool okay) {
    perf_output::Line line(384);
    line.printf("PERF {\"schema\":1,\"type\":\"audio_ack\",\"action\":\"%s\",\"ok\":%s,\"code\":%d,\"operation\":\"%s\"}\n",action,okay?"true":"false",okay?0:lastError,okay?"none":lastOperation);
    line.send();
}
void notify(void *) { if(workerHandle)xTaskNotifyGive(workerHandle); }
void wakeAt(uint64_t deadline) {
    esp_timer_stop(timer); // ESP_ERR_INVALID_STATE means the one-shot already fired.
    const uint64_t now=atUs();
    if(!checked(esp_timer_start_once(timer,deadline>now?deadline-now:1),"timer_start_once")) {
        active.store(false);complete=false;
    }
}
void finish(bool okay) { active.store(false);complete=okay;prepared=false;esp_timer_stop(timer); }
void rxCallback(const uint8_t *mac,const uint8_t *bytes,int length) {
    if(!active.load(std::memory_order_acquire)||!receiving.load(std::memory_order_relaxed))return;
    if(memcmp(mac,peerMac,6)) { wrongMac.fetch_add(1);return; }
    if(length<int(core::HeaderBytes)||length>int(core::MaxPacketBytes))return;
    RxPacket item;item.arrival=atUs();item.bytes=uint16_t(length);memcpy(item.data,bytes,length);
    if(xQueueSend(received,&item,0)!=pdTRUE)queueDrops.fetch_add(1);
    if(workerHandle)xTaskNotifyGive(workerHandle);
}
void txCallback(const uint8_t *mac,esp_now_send_status_t result) {
    if(!active.load(std::memory_order_acquire)||receiving.load()||memcmp(mac,Broadcast,6))return;
    const uint32_t elapsed=uint32_t(atUs())-sendAt32.load();
    uint32_t previous=callbackLatencyMax.load();
    while(elapsed>previous&&!callbackLatencyMax.compare_exchange_weak(previous,elapsed)){}
    sendCallbacks.fetch_add(1);if(result!=ESP_NOW_SEND_SUCCESS)callbackFailures.fetch_add(1);
    inflight.store(false,std::memory_order_release);if(workerHandle)xTaskNotifyGive(workerHandle);
}
bool stopRadio() {
    active.store(false);receiving.store(false);esp_timer_stop(timer);prepared=false;
    if(!radioOwned.load()&&!nowInitialized) { inflight.store(false);return true; }
    bool okay=true;
    if(nowInitialized) {
        okay=checked(esp_now_unregister_recv_cb(),"unregister_rx")&&okay;
        okay=checked(esp_now_unregister_send_cb(),"unregister_tx")&&okay;
        okay=checked(esp_now_deinit(),"espnow_deinit")&&okay;
        nowInitialized=false;
    }
    okay=checked(WiFi.mode(WIFI_OFF)?ESP_OK:ESP_FAIL,"wifi_off")&&okay;
    inflight.store(false);radioOwned.store(!okay);return okay;
}
bool startRadio() {
    if(radioOwned.load()&&!stopRadio())return false;
#ifdef GRIM_PERF_LINK
    perf_link::command("perf link stop");
    perf_link::command("perf link mode off 0");
#endif
    if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_IDLE||esp_bluedroid_get_status()!=ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        lastError=ESP_ERR_INVALID_STATE;lastOperation="existing_Bluetooth_owner";return false;
    }
    esp_log_level_set("*",ESP_LOG_NONE);
    WiFi.persistent(false);WiFi.setAutoReconnect(false);
    if(!checked(WiFi.mode(WIFI_STA)?ESP_OK:ESP_FAIL,"wifi_STA"))return false;
    radioOwned.store(true);
    if(!checked(esp_wifi_set_storage(WIFI_STORAGE_RAM),"storage_RAM")||
       !checked(esp_wifi_set_ps(WIFI_PS_NONE),"power_save_none")||
       !checked(esp_wifi_set_channel(Channel,WIFI_SECOND_CHAN_NONE),"channel_6"))return false;
    rateStatus=esp_wifi_config_espnow_rate(WIFI_IF_STA,phy==6?WIFI_PHY_RATE_6M:WIFI_PHY_RATE_1M_L);
    if(!checked(rateStatus,"espnow_PHY_rate")||!checked(esp_now_init(),"espnow_init"))return false;
    nowInitialized=true;
    if(!checked(esp_now_register_recv_cb(rxCallback),"register_rx")||!checked(esp_now_register_send_cb(txCallback),"register_tx"))return false;
    esp_now_peer_info_t broadcast{};memcpy(broadcast.peer_addr,Broadcast,6);broadcast.channel=Channel;broadcast.ifidx=WIFI_IF_STA;broadcast.encrypt=false;
    return checked(esp_now_add_peer(&broadcast),"broadcast_peer");
}
void resetStats() {
    stats={};queueDrops=wrongMac=sendCallbacks=callbackFailures=callbackLatencyMax=0;inflight=false;
    xQueueReset(received);source.reset(seed);reference.reset(seed);encoder.reset();receiver.reset(config,session,DelayBlocks);
    prepared=false;nextSequence=nextFragment=0;complete=timedOut=false;
}
void transmit(uint64_t now) {
    if(now<startAt) { wakeAt(startAt);return; }
    if(now>=endAt) {
        prepared=false;
        if(!inflight.load()||now>=endAt+DrainUs)finish(true);else wakeAt(endAt+DrainUs);
        return;
    }
    const uint32_t current=uint32_t((now-startAt)*config.rate/(uint64_t(config.frames)*1000000));
    if(prepared&&current>preparedSequence) { prepared=false;++stats.expiredPreparedBlocks; }
    if(!prepared&&nextSequence<=current&&nextSequence<totalBlocks) {
        if(current>nextSequence) {
            stats.skippedSourceBlocks+=current-nextSequence;
            source.seek(config,seed,uint64_t(current)*config.frames);
            if(config.kind==core::Kind::Pdm) { encoder.reset();++stats.encoderResets; }
            nextSequence=current;
        }
        const uint64_t before=atUs();source.fill(config,input);
        if(config.kind==core::Kind::Pdm)encoder.encode(config,input,encoded,sizeof(encoded));
        else core::encodePcm(config,input,encoded,sizeof(encoded));
        stats.encode.add(atUs()-before);++stats.sourceBlocks;
        preparedSequence=nextSequence++;nextFragment=0;prepared=true;
    }
    if(prepared&&!inflight.load(std::memory_order_acquire)) {
        const uint64_t deadline=startAt+blockOffset(uint64_t(preparedSequence)+1);
        if(atUs()>=deadline) { prepared=false;++stats.expiredPreparedBlocks; }
        else {
            const size_t n=core::packetize(config,session,preparedSequence,nextFragment,encoded,packet,sizeof(packet));
            if(!n) { lastError=EINVAL;lastOperation="packetize";finish(false);return; }
            sendAt32.store(uint32_t(atUs()));inflight.store(true,std::memory_order_release);
            const uint64_t before=atUs();const esp_err_t code=esp_now_send(Broadcast,packet,n);stats.send.add(atUs()-before);++stats.txAttempts;
            if(code==ESP_OK) { ++stats.txAccepted;stats.txBytes+=n; }
            else { inflight.store(false);if(code==ESP_ERR_ESPNOW_NO_MEM)++stats.txNoMem;else ++stats.txErrors;lastError=code;lastOperation="broadcast_send"; }
            // Admission failures discard this fragment: no retry/ARQ path.
            if(++nextFragment>=core::fragmentCount(config))prepared=false;
        }
    }
    wakeAt(nextSequence>=totalBlocks?endAt:startAt+blockOffset(nextSequence));
}
void quality(const core::OutputInfo &info) {
    reference.seek(config,seed,uint64_t(info.sequence)*config.frames);reference.fill(config,expected);
    for(unsigned f=0;f<config.frames;++f)for(unsigned c=0;c<config.channels;++c) {
        const unsigned i=f*config.channels+c;const int64_t referenceValue=expected[i],error=int64_t(output[i])-referenceValue;
        ++stats.qualitySamples[c];stats.referenceEnergy[c]+=uint64_t(referenceValue*referenceValue);stats.errorEnergy[c]+=uint64_t(error*error);
        stats.errorAbsMax[c]=std::max(stats.errorAbsMax[c],uint32_t(error<0?-error:error));
    }
    if(!stats.rendered)stats.firstSequence=info.sequence;
    stats.lastSequence=info.sequence;++stats.rendered;if(info.concealed)++stats.concealed;
    stats.slotDwellMax=std::max(stats.slotDwellMax,info.slot_dwell_us);
}
void receive(uint64_t now) {
    RxPacket item;
    for(unsigned n=0;n<RxBatch&&xQueueReceive(received,&item,0)==pdTRUE;++n) {
        now=atUs();const uint64_t age=now-item.arrival;stats.queueAgeMax=std::max(stats.queueAgeMax,age);
        if(age>2*periodCeil()) { ++stats.callbackExpired;continue; }
        core::PacketView view;
        if(core::parse(item.data,item.bytes,view)==core::ParseResult::Okay&&view.session==session&&core::same(view.config,config)&&view.sequence>=totalBlocks) { ++stats.afterWindow;continue; }
        // Actual worker-admission time enforces the core's playout deadline.
        // Callback arrival time is retained separately for local queue age.
        const uint64_t before=atUs();receiver.receive(item.data,item.bytes,before);stats.receive.add(atUs()-before);
    }
    core::OutputInfo info;
    const uint64_t before=atUs();const bool ready=receiver.render(before,output,&info);stats.render.add(atUs()-before);
    if(ready) {
        if(info.sequence<totalBlocks) { const uint64_t qualityAt=atUs();quality(info);stats.quality.add(atUs()-qualityAt); }
        else ++stats.afterWindow;
        if(info.sequence+1>=totalBlocks) { finish(true);return; }
    }
    now=atUs();
    if(now>=hardDeadline) { timedOut=true;finish(false);return; }
    const uint64_t next=receiver.nextDeadlineUs();wakeAt(next?std::min(next,hardDeadline):hardDeadline);
    if(uxQueueMessagesWaiting(received))xTaskNotifyGive(workerHandle);
}
void worker(void *) {
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        if(xSemaphoreTake(control,pdMS_TO_TICKS(20))!=pdTRUE)continue;
        if(active.load()) {
            const uint64_t now=atUs();if(role==Role::Tx)transmit(now);else if(role==Role::Rx)receive(now);
            stats.heapMin=std::min(stats.heapMin,uint32_t(ESP.getFreeHeap()));stats.largestMin=std::min(stats.largestMin,uint32_t(ESP.getMaxAllocHeap()));
        }
        xSemaphoreGive(control);
    }
}
void configFields(perf_output::Line &line) {
    line.printf("\"codec\":\"%s\",\"channels\":%u,\"sample_rate_hz\":%u,\"frames_per_block\":%u,\"pdm_osr\":%u,\"phy_mbps_requested\":%u,\"phy_config_status\":%d,\"channel\":6,\"seed\":%u,\"block_payload_bytes\":%u,\"fragments_per_block\":%u,\"packet_header_bytes\":20,\"max_packet_bytes\":250,\"playout_delay_blocks\":2,\"rx_slots\":8,\"rx_callback_queue_depth\":8,\"tx_inflight_limit\":1,\"worker_core\":1,\"worker_priority\":3,\"pacing\":\"esp_timer_one_shot_notification_rational_deadlines\",\"transport\":\"ESP_NOW_broadcast_packets\",\"mac_ack_required\":false,\"application_retransmission\":false,\"sink\":\"digital_reconstruction_only\",\"i2s_output\":false,\"raw_continuous_RF\":false,\"clock_scope\":\"receiver_worker_admission_relative; absolute_oneway_age_unknown\"",
                codecName(),config.channels,config.rate,config.frames,config.oversample,phy,rateStatus,seed,unsigned(core::blockBytes(config)),unsigned(core::fragmentCount(config)));
}
void info() {
    uint8_t mac[6]{};esp_read_mac(mac,ESP_MAC_WIFI_STA);uint8_t channel=0;wifi_second_chan_t second;wifi_ps_type_t ps=WIFI_PS_NONE;
    const int channelStatus=esp_wifi_get_channel(&channel,&second),psStatus=esp_wifi_get_ps(&ps);
    perf_output::Line line(2048);line.printf("PERF {\"schema\":1,\"type\":\"audio_info\",\"configured\":%s,\"session_authorized\":%s,\"role\":\"%s\",\"active\":%s,\"complete\":%s,\"radio_initialized\":%s,\"sta_mac\":\"%02x%02x%02x%02x%02x%02x\",\"actual_channel\":%u,\"channel_query_status\":%d,\"power_save_enum\":%u,\"power_save_status\":%d,\"heap_free_bytes\":%u,\"worker_stack_min_bytes\":%u,",
        configured?"true":"false",authorized?"true":"false",roleName(),active.load()?"true":"false",complete?"true":"false",nowInitialized?"true":"false",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],channel,channelStatus,unsigned(ps),psStatus,ESP.getFreeHeap(),workerHandle?uxTaskGetStackHighWaterMark(workerHandle):0);
    configFields(line);line.println("}");line.send();
}
void exportStats() {
    const auto &s=receiver.stats();perf_output::Line line(4096);
    line.printf("PERF {\"schema\":1,\"type\":\"audio_summary\",\"run_id\":\"%s\",\"role\":\"%s\",\"complete\":%s,\"timed_out\":%s,\"duration_us\":%llu,\"target_start_us\":%llu,\"target_end_us\":%llu,\"source_total_blocks\":%u,",
        runId,roleName(),complete?"true":"false",timedOut?"true":"false",static_cast<unsigned long long>(uint64_t(durationMs)*1000),static_cast<unsigned long long>(startAt),static_cast<unsigned long long>(endAt),totalBlocks);
    configFields(line);
    line.printf(",\"source_encoded_blocks\":%u,\"skipped_source_blocks\":%u,\"expired_prepared_blocks\":%u,\"encoder_reset_discontinuities\":%u,\"tx_packet_attempts\":%u,\"tx_api_accepted_packets\":%u,\"tx_api_no_mem\":%u,\"tx_api_other_errors\":%u,\"tx_wire_bytes_accepted\":%u,\"broadcast_completion_callbacks\":%u,\"broadcast_completion_failures\":%u,\"broadcast_callback_latency_max_us\":%u,\"packet_inflight_at_export\":%s,\"rx_callback_queue_drops\":%u,\"rx_wrong_mac\":%u,\"rx_expired_callback_packets\":%u,\"rx_callback_age_max_us\":%llu,\"after_source_window\":%u,\"rendered_quality_blocks\":%u,\"concealed_quality_blocks\":%u,\"first_rendered_sequence\":%u,\"last_rendered_sequence\":%u,\"slot_dwell_max_us\":%llu,",
        stats.sourceBlocks,stats.skippedSourceBlocks,stats.expiredPreparedBlocks,stats.encoderResets,stats.txAttempts,stats.txAccepted,stats.txNoMem,stats.txErrors,stats.txBytes,sendCallbacks.load(),callbackFailures.load(),callbackLatencyMax.load(),inflight.load()?"true":"false",queueDrops.load(),wrongMac.load(),stats.callbackExpired,static_cast<unsigned long long>(stats.queueAgeMax),stats.afterWindow,stats.rendered,stats.concealed,stats.firstSequence,stats.lastSequence,static_cast<unsigned long long>(stats.slotDwellMax));
    line.printf("\"rx_accepted_packets\":%u,\"rx_completed_blocks\":%u,\"rx_invalid_packets\":%u,\"rx_crc_errors\":%u,\"rx_wrong_session\":%u,\"rx_wrong_config\":%u,\"rx_duplicate_packets\":%u,\"rx_stale_packets\":%u,\"rx_future_packets\":%u,\"rx_concealed_frames\":%u,\"rx_incomplete_blocks\":%u,\"rx_skipped_playout_blocks\":%u,\"rx_late_playout_blocks\":%u,\"rx_render_lateness_max_us\":%llu,\"heap_free_min_bytes\":%u,\"heap_largest_min_bytes\":%u,\"last_error\":%d,\"last_operation\":\"%s\",",
        s.accepted_packets,s.completed_blocks,s.invalid_packets,s.crc_errors,s.wrong_session,s.wrong_config,s.duplicate_packets,s.stale_packets,s.future_packets,s.concealed_frames,s.incomplete_blocks,s.skipped_blocks,s.late_playout_blocks,static_cast<unsigned long long>(s.max_render_lateness_us),stats.heapMin==UINT32_MAX?0:stats.heapMin,stats.largestMin==UINT32_MAX?0:stats.largestMin,lastError,lastOperation);
    line.printf("\"encode_calls\":%u,\"encode_elapsed_us_sum\":%llu,\"encode_elapsed_us_max\":%u,\"receive_calls\":%u,\"receive_elapsed_us_sum\":%llu,\"receive_elapsed_us_max\":%u,\"render_calls\":%u,\"render_elapsed_us_sum\":%llu,\"render_elapsed_us_max\":%u,\"quality_observer_calls\":%u,\"quality_observer_elapsed_us_sum\":%llu,\"quality_observer_elapsed_us_max\":%u,\"send_calls\":%u,\"send_elapsed_us_sum\":%llu,\"send_elapsed_us_max\":%u,\"timing_scope\":\"elapsed_wall_including_preemption; not_cycle_CPU_time\",\"analog_latency_us\":null,\"absolute_oneway_age_us\":null,\"quality_reference\":\"same_seed_source_sequence_unshifted; no_delay_alignment\",\"quality\":[",
        stats.encode.calls,static_cast<unsigned long long>(stats.encode.sum),stats.encode.max,stats.receive.calls,static_cast<unsigned long long>(stats.receive.sum),stats.receive.max,stats.render.calls,static_cast<unsigned long long>(stats.render.sum),stats.render.max,stats.quality.calls,static_cast<unsigned long long>(stats.quality.sum),stats.quality.max,stats.send.calls,static_cast<unsigned long long>(stats.send.sum),stats.send.max);
    for(unsigned c=0;c<config.channels;++c)line.printf(c?",{\"channel\":%u,\"samples\":%llu,\"reference_energy\":%llu,\"error_energy\":%llu,\"error_abs_max\":%u}":"{\"channel\":%u,\"samples\":%llu,\"reference_energy\":%llu,\"error_energy\":%llu,\"error_abs_max\":%u}",c,static_cast<unsigned long long>(stats.qualitySamples[c]),static_cast<unsigned long long>(stats.referenceEnergy[c]),static_cast<unsigned long long>(stats.errorEnergy[c]),stats.errorAbsMax[c]);
    line.println("]}");line.send();
}
bool validId(const char *s) { if(!*s)return false;for(;*s;++s)if(!isalnum(uint8_t(*s))&&*s!='_'&&*s!='-')return false;return true; }
int hex(char c) { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
}
void begin() {
    if(control)return;control=xSemaphoreCreateMutex();received=xQueueCreate(RxDepth,sizeof(RxPacket));
    esp_timer_create_args_t args{};args.callback=notify;args.dispatch_method=ESP_TIMER_TASK;args.name="audio-mvp";
    if(control&&received&&checked(esp_timer_create(&args,&timer),"timer_create"))xTaskCreatePinnedToCore(worker,"audio-mvp",5120,nullptr,3,&workerHandle,1);
}
void poll() {}
bool ownsRadio() { return radioOwned.load(); }
bool command(const char *line) {
    if(ownsRadio()&&(!strncmp(line,"perf link ",10)||!strncmp(line,"perf radio ",11))) { lastError=EBUSY;lastOperation="audio_owns_radio_stop_first";ack("radio_conflict",false);return true; }
    if(strncmp(line,"perf audio ",11))return false;
    begin();if(!control||!received||!timer||!workerHandle) { lastError=ENOMEM;lastOperation="worker_resources";ack("setup",false);return true; }
    if(xSemaphoreTake(control,pdMS_TO_TICKS(1000))!=pdTRUE) { lastError=EBUSY;lastOperation="control_busy";ack("setup",false);return true; }
    if(!strcmp(line,"perf audio info")) { info();xSemaphoreGive(control);return true; }
    if(!strcmp(line,"perf audio export")) {
        if(active.load()) { lastError=EBUSY;lastOperation="run_active";xSemaphoreGive(control);ack("export",false);return true; }
        exportStats();xSemaphoreGive(control);return true;
    }
    char a[32]{},b[16]{},id[32]{};unsigned channels=0,rate=0,frames=0,osr=0,speed=0,requestedSeed=0,duration=0;
    bool okay=false;const char *action="unknown";lastError=0;lastOperation="none";
    if(!strcmp(line,"perf audio stop")) { action="stop";okay=stopRadio(); }
    else if(ownsRadio()) { lastError=EBUSY;lastOperation="stop_before_reconfigure"; }
    else if(sscanf(line,"perf audio config %31s %u %u %u %u %u %u",a,&channels,&rate,&frames,&osr,&speed,&requestedSeed)==7) {
        action="config";core::Config next;next.rate=rate;next.channels=channels;next.frames=frames;next.oversample=osr;
        next.kind=!strcmp(a,"pdm")?core::Kind::Pdm:core::Kind::Pcm16;
        okay=(!strcmp(a,"pcm16")||!strcmp(a,"pdm"))&&channels<=core::MaxChannels&&frames<=core::MaxFrames&&osr<=32&&core::valid(next)&&(speed==1||speed==6);
        if(okay) { config=next;phy=speed;seed=requestedSeed;configured=true;rateStatus=ESP_ERR_INVALID_STATE; }
    } else if(sscanf(line,"perf audio allow %31s %15s",a,b)==2) {
        action="allow";okay=strlen(a)==12&&strlen(b)==8;uint32_t key=0;uint8_t mac[6]{};
        for(unsigned i=0;okay&&i<12;++i) { int n=hex(a[i]);if(n<0)okay=false;else mac[i/2]|=uint8_t(n<<(i%2?0:4)); }
        for(unsigned i=0;okay&&i<8;++i) { int n=hex(b[i]);if(n<0)okay=false;else key=(key<<4)|unsigned(n); }
        okay=okay&&key&&!(mac[0]&1);if(okay) { memcpy(peerMac,mac,6);session=key;authorized=true; }
        memset(a,0,sizeof(a));memset(b,0,sizeof(b));
    } else {
        const bool tx=sscanf(line,"perf audio run %31s %u",id,&duration)==2;
        const bool rx=!tx&&sscanf(line,"perf audio serve %31s %u",id,&duration)==2;
        if(tx||rx) {
            action=tx?"run":"serve";totalBlocks=uint32_t(uint64_t(config.rate)*duration/(1000*config.frames));
            okay=configured&&authorized&&validId(id)&&duration>0&&duration<=60000&&totalBlocks;
            if(okay)okay=startRadio();
            if(okay) {
                resetStats();strncpy(runId,id,sizeof(runId)-1);durationMs=duration;role=tx?Role::Tx:Role::Rx;
                startAt=tx?atUs()+StartupUs:0;endAt=tx?startAt+uint64_t(duration)*1000:0;hardDeadline=atUs()+uint64_t(duration)*1000+SetupTimeoutUs;
                receiving.store(rx);active.store(true,std::memory_order_release);xTaskNotifyGive(workerHandle);
            }
        }
    }
    if(!okay&&!lastError) { lastError=EINVAL;lastOperation="invalid_configuration"; }
    xSemaphoreGive(control);ack(action,okay);
    if(okay&&(!strcmp(action,"run")||!strcmp(action,"serve"))) {
        perf_output::Line record(512);
        record.printf("PERF {\"schema\":1,\"type\":\"audio_run\",\"run_id\":\"%s\",\"role\":\"%s\",\"source_total_blocks\":%u,\"duration_us\":%llu,\"target_start_us\":%llu,\"target_end_us\":%llu,\"window_scope\":\"tx_source_schedule; RX_worker_admission_relative\"}\n",
            runId,roleName(),totalBlocks,static_cast<unsigned long long>(uint64_t(durationMs)*1000),static_cast<unsigned long long>(startAt),static_cast<unsigned long long>(endAt));
        record.send();
    }
    return true;
}
}
#endif
