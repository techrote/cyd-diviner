/* Original finite SDR carrier-keying experiment. No third-party TX source copied.
 * The experimental RAM entry point is inferred from this SDK's own RF-test ABI;
 * an independent IQ receiver must verify that its requested levels radiate.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_cpu.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_phy_cert_test.h"
#include "esp_private/esp_clk.h"
#include "esp_system.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if !CONFIG_IDF_TARGET_ESP32
#error This probe's experimental RAM ABI is scoped to classic ESP32 only.
#endif
#if !CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0 || CONFIG_PM_ENABLE
#error This cycle-counter probe requires main pinned to CPU0 and power management disabled.
#endif
extern void ram_start_tx_tone(unsigned enable, int offset, unsigned backoff,
                              unsigned arg3, int offset2, unsigned arg5);
extern uint8_t temprature_sens_read(void); /* Pinned RTC library; undocumented. */
/* Stock RF-test archive requires a shell callback; none is exposed. */
int cmd_parse(char *cmd, char *name, int *argc, char **argv) {
    (void)cmd; (void)name; (void)argc; (void)argv; return -1;
}

enum { CAPACITY = 32768, CHANNEL = 6, TRACE_CAPACITY = 128 };
static uint8_t bits[CAPACITY];
static uint32_t bit_count, permit;
static bool radio_ready;

static void reply_error(const char *what) {
    printf("SDR {\"type\":\"error\",\"message\":\"%s\"}\n", what);
}
static void info(void) {
    uint8_t mac[6]; esp_chip_info_t chip; esp_chip_info(&chip);
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    printf("SDR {\"type\":\"info\",\"protocol\":1,\"profile\":\"classic-sdr-tx-probe\","
           "\"source_sha\":\"%s\",\"tx_source_digest\":\"%s\",\"sdk\":\"%s\","
           "\"mac\":\"%02x%02x%02x%02x%02x%02x\",\"chip_revision\":%u,"
           "\"cpu_mhz\":%u,\"main_core\":%u,\"power_management_enabled\":false,\"wifi_nvs_enabled\":false,\"channel\":6,\"carrier_mhz_requested\":2437,"
           "\"capacity_bytes\":%u,\"loaded_bits\":%" PRIu32 ",\"radio_ready\":%s,"
           "\"ram_abi\":\"experimental_sdk_specific_unverified_until_iq\","
           "\"heap_free\":%u,\"temperature_raw\":%u,\"temperature_method\":\"RTC_library_raw_undocumented_uncalibrated\",\"physical_rf_verified\":false}\n",
           SDR_SOURCE_SHA, SDR_SOURCE_DIGEST, esp_get_idf_version(),
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],chip.revision,(unsigned)(esp_clk_cpu_freq()/1000000),xPortGetCoreID(),CAPACITY,
           bit_count,radio_ready?"true":"false",(unsigned)esp_get_free_heap_size(),(unsigned)temprature_sens_read());
}
static esp_err_t start_radio(void) {
    if (radio_ready) return ESP_OK;
    if (esp_clk_cpu_freq()!=240000000 || xPortGetCoreID()!=0) return ESP_ERR_INVALID_STATE;
    esp_err_t e = esp_event_loop_create_default(); if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable=0; /* Do not initialize/read/write owner NVS for this probe. */
    e=esp_wifi_init(&cfg); if(e!=ESP_OK)return e;
    e=esp_wifi_set_storage(WIFI_STORAGE_RAM); if(e!=ESP_OK)goto failed;
    e=esp_wifi_set_mode(WIFI_MODE_NULL); if(e!=ESP_OK)goto failed;
    e=esp_wifi_start(); if(e!=ESP_OK)goto failed;
    e=esp_wifi_set_ps(WIFI_PS_NONE); if(e!=ESP_OK)goto failed;
    radio_ready=true; return ESP_OK;
failed:
    esp_wifi_stop();esp_wifi_deinit();radio_ready=false;return e;
}
static unsigned level_at(uint32_t index) {
    return (bits[index>>3]>>(7-(index&7)))&1u;
}
static void transmit(bool ram, uint32_t baud, uint32_t backoff) {
    const unsigned temperature_before=temprature_sens_read();
    esp_err_t e=start_radio();
    if(e!=ESP_OK){printf("SDR {\"type\":\"radio_error\",\"code\":%d}\n",e);return;}
    uint32_t trace_index[TRACE_CAPACITY], trace_cycle[TRACE_CAPACITY];
    uint8_t trace_level[TRACE_CAPACITY];
    unsigned traced=0, edges=0; uint32_t maximum_call=0, maximum_late=0;
    uint64_t call_cycles=0; uint32_t done=0; bool aborted=false,deadline_abort=false;
    /* Interrupts remain enabled. Actual lateness/preemption is measured. */
    const int64_t request_us=esp_timer_get_time();
    const int64_t nominal_symbol_us=((uint64_t)bit_count*1000000u+baud-1)/baud;
    const int64_t requested_bound_us=2000000; /* Includes initial tone setup. */
    const int64_t requested_deadline_us=request_us+requested_bound_us;
    esp_phy_tx_contin_en(true);
    esp_phy_wifi_tx_tone(1,CHANNEL,backoff);
    const int64_t start_us=esp_timer_get_time();
    const uint32_t first=esp_cpu_get_cycle_count();
    unsigned previous=2;
    for(uint32_t index=0;index<bit_count;index++) {
        const uint32_t scheduled=(uint32_t)((uint64_t)index*240000000u/baud);
        uint32_t now;
        do {
            now=esp_cpu_get_cycle_count()-first;
            if(esp_timer_get_time()>=requested_deadline_us){aborted=deadline_abort=true;break;}
        } while((int32_t)(now-scheduled)<0);
        if(aborted)break;
        uint32_t late=now-scheduled;
        if(late>maximum_late)maximum_late=late;
        /* Stop instead of compressing an obsolete backlog into a later burst. */
        if((uint64_t)late*baud>4ull*240000000u){aborted=true;break;}
        unsigned level=level_at(index);
        if(level!=previous) {
            uint32_t before=esp_cpu_get_cycle_count();
            if(ram) ram_start_tx_tone(level,0,backoff,0,0,0);
            else esp_phy_wifi_tx_tone(level,CHANNEL,backoff);
            uint32_t after=esp_cpu_get_cycle_count();
            uint32_t cost=after-before;call_cycles+=cost;
            if(cost>maximum_call)maximum_call=cost;
            if(traced<TRACE_CAPACITY){trace_index[traced]=index;trace_cycle[traced]=after-first;trace_level[traced]=level;traced++;}
            edges++;previous=level;
            if(esp_timer_get_time()>=requested_deadline_us){done=index+1;aborted=deadline_abort=true;break;}
        }
        done=index+1;
    }
    /* Hold the final symbol for its whole period, then explicitly unkey. */
    const uint32_t end=(uint32_t)((uint64_t)done*240000000u/baud);
    if(!aborted)while((int32_t)((esp_cpu_get_cycle_count()-first)-end)<0){
        if(esp_timer_get_time()>=requested_deadline_us){aborted=deadline_abort=true;break;}
    }
    esp_phy_wifi_tx_tone(0,CHANNEL,backoff);
    esp_phy_tx_contin_en(false);
    const int64_t end_us=esp_timer_get_time();
    const unsigned temperature_after=temprature_sens_read();
    const esp_err_t stop_status=esp_wifi_stop();
    const esp_err_t deinit_status=esp_wifi_deinit();
    radio_ready=(deinit_status!=ESP_OK);
    printf("SDR {\"type\":\"tx_result\",\"mode\":\"%s\",\"requested_baud\":%" PRIu32
           ",\"backoff_quarter_db\":%" PRIu32 ",\"requested_bits\":%" PRIu32
           ",\"processed_bits\":%" PRIu32 ",\"aborted_late\":%s,\"aborted_deadline\":%s,\"start_us\":%" PRId64
           ",\"end_us\":%" PRId64 ",\"rf_request_us\":%" PRId64 ",\"requested_deadline_us\":%" PRId64
           ",\"requested_bound_us\":%" PRId64 ",\"nominal_symbol_duration_us\":%" PRId64 ",\"rf_bound_scope\":\"requested_software_deadline; blocking_SDK_call_or_reset_can_overrun; not_independent_hard_RF_bound\",\"switch_calls\":%u,\"switch_cycles_sum\":%" PRIu64
           ",\"switch_cycles_max\":%" PRIu32 ",\"schedule_late_cycles_max\":%" PRIu32
           ",\"trace_count\":%u,\"trace_scope\":\"call_completion_not_measured_rf_edge\","
           "\"interrupts_enabled\":true,\"payload_order\":\"MSB_first\",\"channel\":6,"
           "\"temperature_raw_before\":%u,\"temperature_raw_after\":%u,\"temperature_method\":\"RTC_library_raw_undocumented_uncalibrated\","
           "\"wifi_stop_status\":%d,\"wifi_deinit_status\":%d}\n",
           ram?"ram":"api",baud,backoff,bit_count,done,aborted&&!deadline_abort?"true":"false",deadline_abort?"true":"false",start_us,end_us,request_us,requested_deadline_us,requested_bound_us,nominal_symbol_us,
           edges,call_cycles,maximum_call,maximum_late,traced,temperature_before,temperature_after,stop_status,deinit_status);
    for(unsigned i=0;i<traced;i++)printf("SDR {\"type\":\"tx_edge\",\"index\":%" PRIu32
       ",\"completion_cycle\":%" PRIu32 ",\"requested_level\":%u}\n",trace_index[i],trace_cycle[i],trace_level[i]);
}
static void command(char *line) {
    uint32_t count,crc,token,baud,backoff; char mode[8],extra;
    if(!strcmp(line,"info")){info();return;}
    if(sscanf(line,"permit %" SCNx32 " %c",&token,&extra)==1 && token){permit=token;puts("SDR {\"type\":\"permit\",\"ok\":true}");return;}
    if(sscanf(line,"load %" SCNu32 " %" SCNx32 " %c",&count,&crc,&extra)==2 && count && count<=CAPACITY*8u) {
        bit_count=0;uint32_t bytes=(count+7)/8,got=0;puts("SDR {\"type\":\"ready\"}");fflush(stdout);
        int64_t deadline=esp_timer_get_time()+5000000;
        while(got<bytes && esp_timer_get_time()<deadline) {
            int n=uart_read_bytes(UART_NUM_0,bits+got,bytes-got,pdMS_TO_TICKS(50));if(n>0)got+=n;
        }
        if(got!=bytes || esp_rom_crc32_le(0,bits,bytes)!=crc){reply_error("upload length or CRC mismatch");return;}
        bit_count=count;printf("SDR {\"type\":\"loaded\",\"bits\":%" PRIu32 "}\n",count);return;
    }
    if(sscanf(line,"tx %7s %" SCNu32 " %" SCNu32 " %" SCNx32 " %c",mode,&baud,&backoff,&token,&extra)==4 &&
       (!strcmp(mode,"api")||!strcmp(mode,"ram")) && baud>=100 && baud<=100000 && backoff>=32 && backoff<=120 &&
       token && token==permit && bit_count && (uint64_t)bit_count*1000<=2000ull*baud) {
        permit=0;puts("SDR {\"type\":\"tx_starting\"}");fflush(stdout);
        transmit(!strcmp(mode,"ram"),baud,backoff);return;
    }
    reply_error("invalid command, permission, duration or configuration");
}
void app_main(void) {
    esp_log_level_set("*",ESP_LOG_NONE);
    uart_config_t cfg={.baud_rate=533333,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,
                      .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0,&cfg));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0,4096,0,0,NULL,0));
    setvbuf(stdout,NULL,_IONBF,0);info();
    char line[128];unsigned used=0;bool overflow=false;
    for(;;){uint8_t c;if(uart_read_bytes(UART_NUM_0,&c,1,pdMS_TO_TICKS(100))!=1)continue;
        if(c=='\r'||c=='\n'){if(overflow)reply_error("line too long");else if(used){line[used]=0;command(line);}used=0;overflow=false;}
        else if(used+1<sizeof(line))line[used++]=(char)c;else overflow=true;
    }
}
