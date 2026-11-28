/* Original isolated ESP32-S3 transmitter. Undocumented FE fields are
 * inferred from this target's pinned vendor PHY object, not classic addresses
 * or third-party TX code.
 * Idle boot does not initialize the radio. No approved probe is modified. */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "driver/usb_serial_jtag.h"
#include "driver/temperature_sensor.h"
#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_cpu.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_memory_utils.h"
#include "esp_phy_cert_test.h"
#include "esp_phy_init.h"
#include "esp_private/wifi.h"
#include "esp_private/esp_clk.h"
#include "esp_rom_crc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "metal_words.h"
#include "fast_schedule.h"

#if !CONFIG_IDF_TARGET_ESP32S3 || !CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0 || CONFIG_PM_ENABLE
#error Pinned ESP32-S3, CPU0 main task and disabled power management required.
#endif
#if CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 240 || CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE
#error Requires 240MHz and no persistent PHY calibration storage.
#endif
extern void start_tx_tone(unsigned, int, unsigned, unsigned, int, unsigned);

/* Deferred control/result output only. The main task is the sole caller;
 * no formatter, USB write, counter update or allocation enters either loop.
 * Counts describe driver API acceptance, not independent host parsing. */
#define OUTPUT_CAPACITY 4096
#define OUTPUT_TIMEOUT_US INT64_C(5000000)
typedef struct {
    uint32_t lines_attempted,lines_accepted,lines_failed,format_failures;
    uint32_t write_calls,short_writes,zero_writes,write_errors,flush_failures;
    uint64_t bytes_requested,bytes_accepted;
} OutputStats;
static OutputStats output_stats;
static char output_buffer[OUTPUT_CAPACITY];
static bool output_resync_pending;
static void output_increment(uint32_t *value) { if(*value<UINT32_MAX)++*value; }
static bool output_write_all(const char *data,size_t length,int64_t deadline) {
    output_stats.bytes_requested+=length;
    while(length&&esp_timer_get_time()<deadline) {
        size_t chunk=length>1024?1024:length;
        output_increment(&output_stats.write_calls);
        int n=usb_serial_jtag_write_bytes(data,chunk,pdMS_TO_TICKS(100));
        if(n<0||(size_t)n>chunk) { output_increment(&output_stats.write_errors);return false; }
        if((size_t)n<chunk)output_increment(&output_stats.short_writes);
        if(!n) { output_increment(&output_stats.zero_writes);vTaskDelay(1);continue; }
        data+=n;length-=(size_t)n;output_stats.bytes_accepted+=(unsigned)n;
    }
    return length==0;
}
static int output_printf(const char *format,...) __attribute__((format(printf,1,2)));
static int output_printf(const char *format,...) {
    output_increment(&output_stats.lines_attempted);
    va_list args;va_start(args,format);
    int n=vsnprintf(output_buffer,sizeof(output_buffer),format,args);
    va_end(args);
    if(n<0||(size_t)n>=sizeof(output_buffer)) {
        output_increment(&output_stats.format_failures);output_increment(&output_stats.lines_failed);return -1;
    }
    const int64_t deadline=esp_timer_get_time()+OUTPUT_TIMEOUT_US;
    /* A previous partial line must not glue the next complete JSON to it. */
    if(output_resync_pending&&!output_write_all("\n",1,deadline)) {
        output_increment(&output_stats.lines_failed);return -1;
    }
    output_resync_pending=false;
    if(!output_write_all(output_buffer,(size_t)n,deadline)) {
        output_resync_pending=true;output_increment(&output_stats.lines_failed);return -1;
    }
    output_increment(&output_stats.lines_accepted);return n;
}
static int output_puts(const char *text) { return output_printf("%s\n",text); }
static bool output_flush(void) {
    if(usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(5000))==ESP_OK)return true;
    output_increment(&output_stats.flush_failures);return false;
}
/* Temperature is an explicit outside-window command, absent from the hot path. */
static void temperature(void) {
    temperature_sensor_handle_t sensor=NULL;
    temperature_sensor_config_t cfg=TEMPERATURE_SENSOR_CONFIG_DEFAULT(10,80);
    int install=temperature_sensor_install(&cfg,&sensor),enable=INT_MIN,read=INT_MIN,disable=INT_MIN,uninstall=INT_MIN;
    float value=0;
    if(install==ESP_OK) {
        enable=temperature_sensor_enable(sensor);
        if(enable==ESP_OK) { read=temperature_sensor_get_celsius(sensor,&value);disable=temperature_sensor_disable(sensor); }
        uninstall=temperature_sensor_uninstall(sensor);
    }
    char number[32]="null";if(read==ESP_OK&&isfinite(value))snprintf(number,sizeof(number),"%.4f",(double)value);
    output_printf("SDR {\"type\":\"temperature\",\"temperature_c_driver\":%s,\"install_status\":%d,\"enable_status\":%d,\"read_status\":%d,\"disable_status\":%d,\"uninstall_status\":%d,\"status_not_called\":-2147483648,\"scope\":\"explicit_outside_RF_window; SDK_driver_not_independently_calibrated\"}\n",number,install,enable,read,disable,uninstall);
}
int cmd_parse(char *a,char *b,int *c,char **d) { (void)a;(void)b;(void)c;(void)d;return -1; }

enum { CpuHz=240000000, Channel=6, DurationLimitUs=2000000, MaskLimitUs=20000 };
typedef enum { Api, Ram, Gate, Ask, Fast } Backend;
typedef struct { uint32_t symbol,completion_cycle,late_cycles,word; } Trace;
typedef struct { uint32_t start_cycle,end_cycle,gap_before_cycles; } Segment;
typedef struct {
    uint32_t writes,skipped_slots,late_writes,late_max,elapsed_cycles;
    uint32_t masked_segments,mask_max_cycles,gap_max_cycles,trace_count,segment_records;
    uint64_t mask_cycles,gap_cycles;
} Result;
typedef struct {
    Backend backend; uint32_t baud,duration_cycles,mask_cycles,backoff0,trace_stride;
    const uint32_t *words; uint32_t length; Trace *trace; uint32_t trace_capacity;
    Segment *segments;
} Loop;
typedef struct {
    const uint32_t *words,*deltas;uint32_t length,baud,ring_remainder,duration_cycles;
} FastLoop;
typedef struct { uint32_t writes,elapsed_cycles,next_start_cycles,ring_phase; } FastResult;
static FastResult IRAM_ATTR __attribute__((noinline)) fast_store_loop(const FastLoop *p);
_Static_assert((uint64_t)DurationLimitUs*240+CpuHz<INT32_MAX,"Fast deadlines stay below half a cycle-counter wrap");
static uint8_t *packed;
static uint32_t *words,capacity,loaded_bits,loaded_crc32,permit,trace_capacity,trace_stride;
static uint32_t *fast_deltas,fast_delta_capacity;
static metal_fast_schedule_t fast_schedule;
static Trace *traces;
static Segment *segments;
static bool minimal_phy=true,radio_ready,wifi_driver_initialized,event_loop_owned,direct_power_domain_owned;
static uint32_t saved_word,observed_word1;
static int wifi_stop_status=INT_MIN,wifi_deinit_status=INT_MIN,event_loop_create_status=INT_MIN,event_loop_delete_status=INT_MIN,teardown_status=INT_MIN;
static const char *backend_name(Backend b) { return b==Api?"api":b==Ram?"ram":b==Gate?"gate":b==Ask?"ask":"fast"; }
static void error(const char *s) { output_printf("SDR {\"type\":\"error\",\"message\":\"%s\"}\n",s); }
static unsigned level_at(uint32_t n) { return (packed[n>>3]>>(7-(n&7)))&1u; }
static inline uint32_t IRAM_ATTR cycles(void) { return esp_cpu_get_cycle_count(); }
static inline void IRAM_ATTR store_word(uint32_t word) {
    *(volatile uint32_t *)UINT32_C(0x60006040)=word;
    __asm__ __volatile__("memw" ::: "memory");
}
static bool reserve(uint32_t n,uint32_t t) {
    if(!n || n>UINT32_MAX/sizeof(uint32_t) || t>UINT32_MAX/sizeof(Trace))return false;
    /* These allocations are entirely outside any RF/timed window. Explicit
     * reserve replaces buffers; a failure leaves an unloaded, idle probe. */
    heap_caps_free(words);heap_caps_free(packed);heap_caps_free(traces);heap_caps_free(segments);heap_caps_free(fast_deltas);
    fast_deltas=NULL;fast_delta_capacity=0;memset(&fast_schedule,0,sizeof(fast_schedule));
    words=NULL;packed=NULL;traces=NULL;segments=NULL;capacity=loaded_bits=loaded_crc32=trace_capacity=0;
    words=heap_caps_malloc((size_t)n*sizeof(uint32_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    packed=heap_caps_malloc(((size_t)n+7)/8,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(t) {
        traces=heap_caps_malloc((size_t)t*sizeof(Trace),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        segments=heap_caps_malloc((size_t)t*sizeof(Segment),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    }
    if(!words||!packed||(t&&(!traces||!segments))) {
        heap_caps_free(words);heap_caps_free(packed);heap_caps_free(traces);heap_caps_free(segments);words=NULL;packed=NULL;traces=NULL;segments=NULL;return false;
    }
    capacity=n;trace_capacity=t;memset(packed,0,((size_t)n+7)/8);return true;
}
static bool prepare_fast(uint32_t baud) {
    if(!loaded_bits||!baud||baud>CpuHz)return false;
    if(fast_delta_capacity<loaded_bits) {
        heap_caps_free(fast_deltas);fast_deltas=NULL;fast_delta_capacity=0;
        fast_deltas=heap_caps_malloc((size_t)loaded_bits*sizeof(uint32_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        if(!fast_deltas)return false;
        fast_delta_capacity=loaded_bits;
    }
    /* Allocate before RF, but populate only after PHY startup calibration. */
    return esp_ptr_in_iram((const void *)fast_store_loop)&&esp_ptr_in_dram(words)&&esp_ptr_in_dram(words+loaded_bits-1)&&
        esp_ptr_in_dram(fast_deltas)&&esp_ptr_in_dram(fast_deltas+loaded_bits-1);
}
static void info(void) {
    uint8_t mac[6];esp_chip_info_t chip;esp_chip_info(&chip);esp_read_mac(mac,ESP_MAC_WIFI_STA);
    output_printf("SDR {\"type\":\"info\",\"protocol\":2,\"profile\":\"s3-sdr-metal-tx\","
        "\"source_sha\":\"%s\",\"source_dirty\":%s,\"tx_source_digest\":\"%s\",\"word_helpers_digest\":\"%s\",\"sdk\":\"%s\","
        "\"mac\":\"%02x%02x%02x%02x%02x%02x\",\"chip_revision\":%u,\"cpu_mhz\":%u,\"main_core\":%u,"
        "\"init_path\":\"%s\",\"radio_ready\":%s,\"nvs_initialized\":false,\"phy_calibration_storage\":false,"
        "\"wifi_driver_owned\":%s,\"default_event_loop_owned\":%s,\"direct_power_domain_owned\":%s,\"direct_power_domain_api_scope\":\"owned_invocation_reference; void_API_not_physical_power_attestation\","
        "\"capacity_symbols\":%u,\"loaded_bits\":%u,\"loaded_crc32\":%u,\"trace_capacity\":%u,\"trace_stride\":%u,\"segment_capacity\":%u,"
        "\"fast_paced_supported\":true,\"source_integrity_supported\":true,\"fast_schedule_bytes_reserved\":%u,\"fast_schedule_helpers_digest\":\"%s\",\"fast_constraint\":\"positive_baud; IRQ_enabled; observe0; mask0; populated_after_startup_calibration\","
        "\"waveform_memory\":\"internal_8bit_DRAM\",\"finite_duration_limit_us\":%u,\"mask_segment_limit_us\":%u,"
        "\"baud_limit_scope\":\"paced_rate_at_most_CPU_cycle_clock; zero_is_unpaced; achieved_rate_and_missed_slots_measured\","
        "\"register_address\":\"0x60006040\",\"gate_mask\":%u,\"amplitude_mask\":%u,\"mmio_semantics\":\"pinned_S3_vendor_object_inference_RF_unverified\","
        "\"heap_internal_free_bytes\":%u,\"heap_8bit_free_bytes\":%u,\"heap_8bit_largest_bytes\":%u,\"heap_32bit_free_bytes\":%u,"
        "\"temperature_raw\":null,\"temperature_method\":\"not_collected_use_explicit_temperature_command\",\"physical_rf_verified\":false,"
        "\"output_transport\":\"explicit_USB_Serial_JTAG_write_all\",\"output_line_capacity_bytes\":%u,\"output_line_timeout_us\":%u,"
        "\"output_deadline_scope\":\"checked_between_driver_calls; final_wait_up_to_100ms; flush_separate_5s\","
        "\"output_lines_attempted\":%u,\"output_lines_accepted\":%u,\"output_lines_failed\":%u,\"output_format_failures\":%u,"
        "\"output_write_calls\":%u,\"output_short_writes\":%u,\"output_zero_writes\":%u,\"output_write_errors\":%u,\"output_flush_failures\":%u,"
        "\"output_bytes_requested\":%llu,\"output_bytes_accepted\":%llu,\"output_resync_pending\":%s,"
        "\"output_counter_scope\":\"cumulative_u32_saturating_and_u64_bytes; snapshot_before_current_line; driver_acceptance_not_host_parse_proof\"}\n",
        SDR_SOURCE_SHA,SDR_SOURCE_DIRTY?"true":"false",SDR_SOURCE_DIGEST,SDR_WORDS_DIGEST,esp_get_idf_version(),
        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],(unsigned)chip.revision,(unsigned)(esp_clk_cpu_freq()/1000000),(unsigned)xPortGetCoreID(),
        minimal_phy?"experimental_direct_PHY":"WiFi_NULL",radio_ready?"true":"false",wifi_driver_initialized?"true":"false",event_loop_owned?"true":"false",direct_power_domain_owned?"true":"false",(unsigned)capacity,(unsigned)loaded_bits,(unsigned)loaded_crc32,(unsigned)trace_capacity,(unsigned)trace_stride,(unsigned)trace_capacity,(unsigned)(fast_delta_capacity*sizeof(uint32_t)),SDR_FAST_DIGEST,
        (unsigned)DurationLimitUs,(unsigned)MaskLimitUs,(unsigned)METAL_ENABLE_MASK,(unsigned)METAL_AMPLITUDE_MASK,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT),
        (unsigned)OUTPUT_CAPACITY,(unsigned)OUTPUT_TIMEOUT_US,(unsigned)output_stats.lines_attempted,(unsigned)output_stats.lines_accepted,
        (unsigned)output_stats.lines_failed,(unsigned)output_stats.format_failures,(unsigned)output_stats.write_calls,(unsigned)output_stats.short_writes,
        (unsigned)output_stats.zero_writes,(unsigned)output_stats.write_errors,(unsigned)output_stats.flush_failures,
        (unsigned long long)output_stats.bytes_requested,(unsigned long long)output_stats.bytes_accepted,output_resync_pending?"true":"false");
}
static esp_err_t cleanup_wifi(void) {
    if(wifi_driver_initialized) {
        wifi_stop_status=esp_wifi_stop();
        if(wifi_stop_status!=ESP_OK&&wifi_stop_status!=ESP_ERR_WIFI_NOT_STARTED)return wifi_stop_status;
        wifi_deinit_status=esp_wifi_deinit();
        if(wifi_deinit_status!=ESP_OK)return wifi_deinit_status;
        wifi_driver_initialized=false;
    }
    if(event_loop_owned) {
        event_loop_delete_status=esp_event_loop_delete_default();
        if(event_loop_delete_status!=ESP_OK)return event_loop_delete_status;
        event_loop_owned=false;
    }
    return ESP_OK;
}
static esp_err_t start_radio(void) {
    if(esp_clk_cpu_freq()!=CpuHz||xPortGetCoreID()!=0||radio_ready||wifi_driver_initialized||event_loop_owned||direct_power_domain_owned)return ESP_ERR_INVALID_STATE;
    wifi_stop_status=wifi_deinit_status=event_loop_create_status=event_loop_delete_status=teardown_status=INT_MIN;
    if(minimal_phy) {
        /* Match pinned SDK test_phy_rtc.c and wifi_init.c prerequisites:
         * clocks alone do not power/de-isolate the MAC/BB domain. */
        esp_wifi_power_domain_on();direct_power_domain_owned=true;
        esp_phy_enable(PHY_MODEM_WIFI);radio_ready=true;return ESP_OK;
    }
    esp_err_t e=esp_event_loop_create_default();event_loop_create_status=e;
    if(e==ESP_OK)event_loop_owned=true;else if(e!=ESP_ERR_INVALID_STATE)return e;
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();cfg.nvs_enable=0;
    e=esp_wifi_init(&cfg);if(e!=ESP_OK)goto failed;
    wifi_driver_initialized=true;
    e=esp_wifi_set_storage(WIFI_STORAGE_RAM);if(e!=ESP_OK)goto failed;
    e=esp_wifi_set_mode(WIFI_MODE_NULL);if(e!=ESP_OK)goto failed;
    e=esp_wifi_start();if(e!=ESP_OK)goto failed;
    e=esp_wifi_set_ps(WIFI_PS_NONE);if(e!=ESP_OK)goto failed;
    radio_ready=true;return ESP_OK;
failed:
    teardown_status=cleanup_wifi();
    return e; /* Preserve the original setup error separately from cleanup. */
}
static void stop_radio(void) {
    if(minimal_phy) {
        esp_phy_disable(PHY_MODEM_WIFI);
        if(direct_power_domain_owned) { esp_wifi_power_domain_off();direct_power_domain_owned=false; }
        teardown_status=ESP_OK;
    }
    else teardown_status=cleanup_wifi();
    radio_ready=wifi_driver_initialized||event_loop_owned||direct_power_domain_owned;
}
/* Separate maximum-store-loop context. No per-symbol arithmetic, trace or PHY
 * dispatch is charged to this variant; one cycle read bounds each iteration.
 * It is not an interrupt-causal A/B against the more observed masked loop. */
static Result IRAM_ATTR __attribute__((noinline)) raw_store_loop(const Loop *p) {
    Result r={0};const uint32_t *cursor=p->words,*limit=cursor+p->length;
    const uint32_t first=cycles();
    while(cycles()-first<p->duration_cycles) {
        store_word(*cursor++);++r.writes;if(cursor==limit)cursor=p->words;
    }
    r.elapsed_cycles=cycles()-first;return r;
}
/* All waveform words and output storage are internal DRAM. This function and
 * MMIO/cycle helpers reside in IRAM. The gate/ASK branch calls no SDK routine
 * while masked. Level3 masking leaves higher-priority interrupts possible. */
static Result IRAM_ATTR __attribute__((noinline)) timed_loop(const Loop *p) {
    Result r={0};uint32_t symbol=0,index=0,next=0,trace_next=0,segment=0;
    uint32_t mask_started=0,mask_closed=0;UBaseType_t old_mask=0;bool masked=false;
    const uint32_t first=cycles();
    for(;;) {
        uint32_t now=cycles()-first;
        if(now>=p->duration_cycles)break;
        if(p->mask_cycles&&!masked) {
            mask_started=cycles();old_mask=portSET_INTERRUPT_MASK_FROM_ISR();masked=true;++r.masked_segments;
            uint32_t gap=mask_closed?mask_started-mask_closed:0;
            if(mask_closed) { r.gap_cycles+=gap;if(gap>r.gap_max_cycles)r.gap_max_cycles=gap; }
            segment=mask_started-first;
            if(r.segment_records<p->trace_capacity) {
                Segment *s=&p->segments[r.segment_records++];s->start_cycle=segment;s->end_cycle=segment;s->gap_before_cycles=gap;
            }
            now=cycles()-first;
        }
        if(p->mask_cycles&&now-segment>=p->mask_cycles) {
            portCLEAR_INTERRUPT_MASK_FROM_ISR(old_mask);mask_closed=cycles();masked=false;
            uint32_t duration=mask_closed-mask_started;r.mask_cycles+=duration;if(duration>r.mask_max_cycles)r.mask_max_cycles=duration;
            if(r.segment_records)p->segments[r.segment_records-1].end_cycle=mask_closed-first;
            continue;
        }
        if(p->baud&&now<next)continue;
        uint32_t late=p->baud?now-next:0;
        if(p->baud&&late) { ++r.late_writes;if(late>r.late_max)r.late_max=late; }
        /* Do not compress missed paced slots into an obsolete catch-up burst. */
        if(p->baud&&((uint64_t)late*p->baud)>=CpuHz) {
            uint32_t current=(uint32_t)((uint64_t)now*p->baud/CpuHz);
            if(current>symbol) { r.skipped_slots+=current-symbol;symbol=current;index=symbol%p->length; }
        }
        uint32_t word=p->words[index];
        if(p->backend==Api)esp_phy_wifi_tx_tone((word&METAL_ENABLE_MASK)?1:0,Channel,p->backoff0);
        else if(p->backend==Ram)start_tx_tone((word&METAL_ENABLE_MASK)?1:0,0,p->backoff0,0,0,0);
        else store_word(word);
        ++r.writes;
        if(p->trace_stride&&r.trace_count<p->trace_capacity&&r.writes>=trace_next) {
            Trace *t=&p->trace[r.trace_count++];t->symbol=symbol;t->completion_cycle=cycles()-first;t->late_cycles=late;t->word=word;
            trace_next=p->trace_stride>UINT32_MAX-r.writes?UINT32_MAX:r.writes+p->trace_stride;
        }
        ++symbol;if(++index==p->length)index=0;
        if(p->baud)next=(uint32_t)((uint64_t)symbol*CpuHz/p->baud);
    }
    if(masked) { portCLEAR_INTERRUPT_MASK_FROM_ISR(old_mask);mask_closed=cycles();uint32_t duration=mask_closed-mask_started;r.mask_cycles+=duration;if(duration>r.mask_max_cycles)r.mask_max_cycles=duration;if(r.segment_records)p->segments[r.segment_records-1].end_cycle=mask_closed-first; }
    r.elapsed_cycles=cycles()-first;return r;
}
/* Minimal paced backend: no per-symbol division, trace, lateness histogram or
 * mask branch. Late deadlines retain every word in order; the finite end may
 * leave an explicit source tail unsent. One fractional carry is paid per ring.
 * The source and schedule buffers, this code and its literal pool are internal.
 * Deadline+one interval stays below INT32_MAX for the allowed finite duration. */
static FastResult IRAM_ATTR __attribute__((noinline)) fast_store_loop(const FastLoop *p) {
    FastResult r={0};const uint32_t *word=p->words,*limit=word+p->length,*delta=p->deltas;
    const uint32_t duration=p->duration_cycles,baud=p->baud,remainder=p->ring_remainder;
    uint32_t next=0,phase=0;const uint32_t first=cycles();
    for(;;) {
        uint32_t elapsed;
        do { elapsed=cycles()-first;if(elapsed>=duration)goto done; }while(elapsed<next);
        store_word(*word++);++r.writes;next+=*delta++;
        if(word==limit) {
            word=p->words;delta=p->deltas;
            next+=metal_fast_ring_carry(&phase,remainder,baud);
        }
    }
done:
    r.elapsed_cycles=cycles()-first;r.next_start_cycles=next;r.ring_phase=phase;return r;
}
typedef struct {
    uint32_t packed_expected,packed_before_radio,packed_after_radio,packed_after_tone,packed_before_loop,packed_after_loop,packed_after_teardown;
    uint32_t words_expected,words_before_loop,words_after_loop,words_after_teardown;
    uint32_t deltas_after_build,deltas_before_loop,deltas_after_loop,deltas_after_teardown;
    bool after_radio_checked,after_tone_checked,preloop_checked,loop_started,teardown_checked,delta_required,delta_built;
} SourceIntegrity;
static bool timed_source_ok(const SourceIntegrity *s) {
    return s->after_radio_checked&&s->after_tone_checked&&s->preloop_checked&&s->loop_started&&
        s->packed_before_radio==s->packed_expected&&s->packed_after_radio==s->packed_expected&&
        s->packed_after_tone==s->packed_expected&&
        s->packed_before_loop==s->packed_expected&&s->packed_after_loop==s->packed_expected&&
        s->words_before_loop==s->words_expected&&s->words_after_loop==s->words_expected&&
        (!s->delta_required||(s->delta_built&&s->deltas_before_loop==s->deltas_after_build&&s->deltas_after_loop==s->deltas_after_build));
}
static bool retained_source_ok(const SourceIntegrity *s) {
    return timed_source_ok(s)&&s->teardown_checked&&s->packed_after_teardown==s->packed_expected&&
        s->words_after_teardown==s->words_expected&&(!s->delta_required||s->deltas_after_teardown==s->deltas_after_build);
}
static bool emit_source_integrity(const SourceIntegrity *s) {
    return output_printf("SDR {\"type\":\"tx_source_integrity\",\"ok\":%s,\"timed_source_integrity_ok\":%s,\"retained_source_integrity_ok\":%s,"
        "\"after_radio_checked\":%s,\"after_tone_checked\":%s,\"preloop_checked\":%s,\"loop_started\":%s,\"teardown_checked\":%s,\"delta_required\":%s,"
        "\"packed_expected_crc32\":%u,\"packed_before_radio_crc32\":%u,\"packed_after_radio_crc32\":%u,\"packed_after_tone_crc32\":%u,\"packed_before_loop_crc32\":%u,\"packed_after_loop_crc32\":%u,\"packed_after_teardown_crc32\":%u,"
        "\"words_expected_crc32\":%u,\"words_before_loop_crc32\":%u,\"words_after_loop_crc32\":%u,\"words_after_teardown_crc32\":%u,"
        "\"deltas_after_build_crc32\":%u,\"deltas_before_loop_crc32\":%u,\"deltas_after_loop_crc32\":%u,\"deltas_after_teardown_crc32\":%u,"
        "\"packed_address\":%u,\"packed_bytes\":%u,\"words_address\":%u,\"words_bytes\":%u,\"deltas_address\":%u,\"deltas_bytes\":%u,"
        "\"schedule_populated_after_startup_calibration\":%s,\"schedule_population_scope\":\"after_all_SDK_startup_and_tone_setup_before_timed_loop\",\"buffer_word_byte_order\":\"native_little_endian_u32\",\"packed_bit_order\":\"MSB_first_per_byte\",\"crc_scope\":\"CRC32_LE_seed0_full_buffers_at_marked_stages; unobserved_interval_changes_and_collisions_not_excluded\","
        "\"observer_scope\":\"outside_timed_loops; adds_setup_teardown_time; register_API_and_CRC_proof_not_independent_RF_payload_attestation\"}\n",
        retained_source_ok(s)?"true":"false",timed_source_ok(s)?"true":"false",retained_source_ok(s)?"true":"false",
        s->after_radio_checked?"true":"false",s->after_tone_checked?"true":"false",s->preloop_checked?"true":"false",s->loop_started?"true":"false",s->teardown_checked?"true":"false",s->delta_required?"true":"false",
        (unsigned)s->packed_expected,(unsigned)s->packed_before_radio,(unsigned)s->packed_after_radio,(unsigned)s->packed_after_tone,(unsigned)s->packed_before_loop,(unsigned)s->packed_after_loop,(unsigned)s->packed_after_teardown,
        (unsigned)s->words_expected,(unsigned)s->words_before_loop,(unsigned)s->words_after_loop,(unsigned)s->words_after_teardown,
        (unsigned)s->deltas_after_build,(unsigned)s->deltas_before_loop,(unsigned)s->deltas_after_loop,(unsigned)s->deltas_after_teardown,
        (unsigned)(uintptr_t)packed,(unsigned)((loaded_bits+7)/8),(unsigned)(uintptr_t)words,(unsigned)(loaded_bits*sizeof(uint32_t)),
        s->delta_required?(unsigned)(uintptr_t)fast_deltas:0,s->delta_required?(unsigned)(loaded_bits*sizeof(uint32_t)):0,s->delta_built?"true":"false")>=0;
}
static void transmit(Backend backend,uint32_t baud,uint32_t b0,uint32_t b1,uint32_t duration_us,uint32_t mask_us) {
    const size_t packed_bytes=(loaded_bits+7)/8,word_bytes=(size_t)loaded_bits*sizeof(uint32_t);
    SourceIntegrity integrity={0};integrity.packed_expected=loaded_crc32;integrity.delta_required=backend==Fast;
    integrity.packed_before_radio=esp_rom_crc32_le(0,packed,packed_bytes);
    if(integrity.packed_before_radio!=loaded_crc32) {
        (void)emit_source_integrity(&integrity);error("source packed CRC changed before radio initialization");return;
    }
    const unsigned heap_before=(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    const unsigned largest_before=(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    uint64_t request=esp_timer_get_time();
    esp_err_t e=start_radio();if(e!=ESP_OK) {
        output_printf("SDR {\"type\":\"radio_error\",\"code\":%d,\"cleanup_status\":%d,\"event_loop_create_status\":%d,\"event_loop_delete_status\":%d,\"wifi_stop_status\":%d,\"wifi_deinit_status\":%d,\"wifi_driver_owned\":%s,\"default_event_loop_owned\":%s,\"status_not_called\":-2147483648}\n",
            e,teardown_status,event_loop_create_status,event_loop_delete_status,wifi_stop_status,wifi_deinit_status,wifi_driver_initialized?"true":"false",event_loop_owned?"true":"false");return;
    }
    const unsigned heap_radio=(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    const unsigned largest_radio=(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    integrity.after_radio_checked=true;integrity.packed_after_radio=esp_rom_crc32_le(0,packed,packed_bytes);
    if(integrity.packed_after_radio!=loaded_crc32) {
        stop_radio();(void)emit_source_integrity(&integrity);error("source packed CRC changed during radio initialization");return;
    }
    esp_phy_tx_contin_en(true);esp_phy_wifi_tx_tone(1,Channel,b0);
    saved_word=*(volatile uint32_t *)UINT32_C(0x60006040);observed_word1=saved_word;
    bool available=(saved_word&METAL_ENABLE_MASK)!=0;
    if(backend==Ask&&available) {
        esp_phy_wifi_tx_tone(1,Channel,b1);observed_word1=*(volatile uint32_t *)UINT32_C(0x60006040);
        available=(observed_word1&METAL_ENABLE_MASK)!=0;
    }
    Result r={0};FastResult fast={0};uint64_t begin=0,end=0;uint32_t restored_word=0;
    if(available) {
        integrity.after_tone_checked=true;integrity.packed_after_tone=esp_rom_crc32_le(0,packed,packed_bytes);
        available=integrity.packed_after_tone==loaded_crc32;
    }
    if(available) {
        const uint32_t low=backend==Ask?metal_amplitude_word(saved_word,saved_word):metal_gate_word(saved_word,0);
        const uint32_t high=backend==Ask?metal_amplitude_word(saved_word,observed_word1):metal_gate_word(saved_word,1);
        for(uint32_t i=0;i<loaded_bits;++i) {
            uint32_t intended=level_at(i)?high:low;words[i]=intended;
            integrity.words_expected=esp_rom_crc32_le(integrity.words_expected,(const uint8_t *)&intended,sizeof(intended));
        }
        if(backend==Fast) {
            integrity.delta_built=metal_fast_schedule_build(&fast_schedule,CpuHz,baud,loaded_bits,fast_deltas);
            integrity.deltas_after_build=esp_rom_crc32_le(0,(const uint8_t *)fast_deltas,word_bytes);
        }
        integrity.packed_before_loop=esp_rom_crc32_le(0,packed,packed_bytes);
        integrity.words_before_loop=esp_rom_crc32_le(0,(const uint8_t *)words,word_bytes);
        if(backend==Fast)integrity.deltas_before_loop=esp_rom_crc32_le(0,(const uint8_t *)fast_deltas,word_bytes);
        integrity.preloop_checked=true;
        available=integrity.packed_before_loop==loaded_crc32&&integrity.words_before_loop==integrity.words_expected&&
            (backend!=Fast||(integrity.delta_built&&integrity.deltas_before_loop==integrity.deltas_after_build));
    }
    if(available) {
        Loop p={backend,baud,duration_us*240,mask_us*240,b0,trace_stride,words,loaded_bits,traces,trace_capacity,segments};
        integrity.loop_started=true;begin=esp_timer_get_time();
        if(backend==Fast) {
            const FastLoop f={words,fast_deltas,loaded_bits,baud,fast_schedule.ring_remainder,duration_us*240};
            fast=fast_store_loop(&f);r.writes=fast.writes;r.elapsed_cycles=fast.elapsed_cycles;
        }
        else if(!baud&&!mask_us&&!trace_stride&&(backend==Gate||backend==Ask))r=raw_store_loop(&p);else r=timed_loop(&p);
        end=esp_timer_get_time();
        store_word(saved_word); /* Restore all saved bits before PHY teardown. */
        restored_word=*(volatile uint32_t *)UINT32_C(0x60006040);
        integrity.packed_after_loop=esp_rom_crc32_le(0,packed,packed_bytes);
        integrity.words_after_loop=esp_rom_crc32_le(0,(const uint8_t *)words,word_bytes);
        if(backend==Fast)integrity.deltas_after_loop=esp_rom_crc32_le(0,(const uint8_t *)fast_deltas,word_bytes);
    }
    esp_phy_wifi_tx_tone(0,Channel,b0);esp_phy_tx_contin_en(false);
    const uint32_t tone_disabled_word=*(volatile uint32_t *)UINT32_C(0x60006040);
    stop_radio();
    integrity.packed_after_teardown=esp_rom_crc32_le(0,packed,packed_bytes);
    integrity.words_after_teardown=esp_rom_crc32_le(0,(const uint8_t *)words,word_bytes);
    if(backend==Fast)integrity.deltas_after_teardown=esp_rom_crc32_le(0,(const uint8_t *)fast_deltas,word_bytes);
    integrity.teardown_checked=true;
    char late_count[16],late_maximum[16];
    snprintf(late_count,sizeof(late_count),"%u",(unsigned)r.late_writes);
    snprintf(late_maximum,sizeof(late_maximum),"%u",(unsigned)r.late_max);
    bool result_accepted=output_printf("SDR {\"type\":\"tx_result\",\"mode\":\"%s\",\"init_path\":\"%s\",\"backend_available\":%s,\"backend_state\":\"%s\","
        "\"requested_baud\":%u,\"unpaced\":%s,\"backoff0_quarter_db\":%u,\"backoff1_quarter_db\":%u,\"requested_duration_us\":%u,"
        "\"pattern_bits\":%u,\"processed_bits\":%u,\"skipped_paced_slots\":%u,\"late_writes\":%s,\"late_cycles_max\":%s,"
        "\"elapsed_cycles\":%u,\"start_us\":%llu,\"end_us\":%llu,\"rf_request_us\":%llu,\"teardown_complete_us\":%llu,"
        "\"achieved_writes_per_s\":%.3f,\"mean_loop_cycles_per_write\":%.3f,\"requested_mask_segment_us\":%u,\"masked_segments\":%u,"
        "\"actual_mask_cycles_sum\":%llu,\"actual_mask_cycles_max\":%u,\"intersegment_gap_cycles_sum\":%llu,\"intersegment_gap_cycles_max\":%u,"
        "\"initialized_word\":%u,\"observed_backoff1_word\":%u,\"restored_word_requested\":%u,\"restored_word_readback\":%u,\"trace_count\":%u,\"trace_stride\":%u,\"segment_records\":%u,\"segment_records_dropped\":%u,"
        "\"loop_variant\":\"%s\",\"trace_scope\":\"software_store_or_call_completion_not_RF_edges\",\"timing_scope\":\"IRAM_loop_including_requested_observers_and_interrupts\","
        "\"finite_bound_scope\":\"requested_payload_duration; SDK_setup_teardown_or_interrupt_can_overrun; not_hard_RF_bound\","
        "\"mask_scope\":\"local_CPU_RTOS_level3; higher_priority_interrupts_possible\",\"radio_stop_api\":\"%s\","
        "\"mask_time_scope\":\"cycle_brackets_include_entry_exit_and_possible_IRQ_immediately_after_restore; upper_bound_on_local_mask_time\","
        "\"wifi_stop_status\":%d,\"wifi_deinit_status\":%d,\"event_loop_create_status\":%d,\"event_loop_delete_status\":%d,\"teardown_status\":%d,\"wifi_driver_owned_after\":%s,\"default_event_loop_owned_after\":%s,\"direct_power_domain_owned_after\":%s,\"power_domain_status_scope\":\"void_on_off_API_no_return_status\",\"status_not_called\":-2147483648,\"phy_disable_status\":null,\"phy_disable_status_scope\":\"void_API_no_return_status\","
        "\"heap_8bit_before_radio_bytes\":%u,\"heap_8bit_largest_before_radio_bytes\":%u,\"heap_8bit_after_radio_bytes\":%u,\"heap_8bit_largest_after_radio_bytes\":%u,"
        "\"heap_8bit_free_bytes\":%u,\"heap_8bit_largest_bytes\":%u,\"temperature_raw_before\":null,\"temperature_raw_after\":null,"
        "\"temperature_method\":\"not_collected_use_explicit_temperature_command\",\"physical_rf_verified\":false}\n",
        backend_name(backend),minimal_phy?"experimental_direct_PHY":"WiFi_NULL",available?"true":"false",available?"expected_enable_bit_observed_RF_unverified":integrity.after_tone_checked?"source_integrity_failed_no_loop":"unexpected_enable_bit_no_MMIO_written",
        (unsigned)baud,baud?"false":"true",(unsigned)b0,(unsigned)b1,(unsigned)duration_us,(unsigned)loaded_bits,(unsigned)r.writes,(unsigned)r.skipped_slots,backend==Fast?"null":late_count,backend==Fast?"null":late_maximum,(unsigned)r.elapsed_cycles,
        (unsigned long long)begin,(unsigned long long)end,(unsigned long long)request,(unsigned long long)esp_timer_get_time(),
        r.elapsed_cycles?(double)r.writes*CpuHz/r.elapsed_cycles:0.,r.writes?(double)r.elapsed_cycles/r.writes:0.,(unsigned)mask_us,(unsigned)r.masked_segments,
        (unsigned long long)r.mask_cycles,(unsigned)r.mask_max_cycles,(unsigned long long)r.gap_cycles,(unsigned)r.gap_max_cycles,
        (unsigned)saved_word,(unsigned)observed_word1,(unsigned)saved_word,(unsigned)restored_word,(unsigned)r.trace_count,(unsigned)trace_stride,(unsigned)r.segment_records,(unsigned)(r.masked_segments-r.segment_records),
        backend==Fast?"fast_rational_DRAM_paced":!baud&&!mask_us&&!trace_stride&&(backend==Gate||backend==Ask)?"minimal_unpaced_MMIO":"observed_paced_masked_or_library",
        minimal_phy?"esp_phy_disable_void":"WiFi_stop_deinit",wifi_stop_status,wifi_deinit_status,event_loop_create_status,event_loop_delete_status,teardown_status,wifi_driver_initialized?"true":"false",event_loop_owned?"true":"false",direct_power_domain_owned?"true":"false",heap_before,largest_before,heap_radio,largest_radio,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT))>=0;
    bool source_integrity_accepted=result_accepted&&emit_source_integrity(&integrity);
    bool fast_clock_accepted=backend!=Fast;
    if(backend==Fast&&result_accepted&&source_integrity_accepted) {
        const uint64_t target_words=((uint64_t)duration_us*baud+999999)/1000000;
        const uint64_t ideal_next=(uint64_t)fast.writes*CpuHz/baud;
        const int64_t clock_error=(int64_t)fast.elapsed_cycles-(int64_t)ideal_next;
        fast_clock_accepted=output_printf("SDR {\"type\":\"tx_fast_clock\",\"requested_baud\":%u,\"target_duration_cycles\":%u,\"source_words_completed\":%u,"
            "\"ideal_words_by_target_end\":%llu,\"target_tail_words_not_transmitted\":%llu,\"target_words_ahead_due_rounding\":%llu,"
            "\"schedule_next_start_cycles\":%u,\"ideal_next_start_cycles_floor\":%llu,\"end_clock_error_cycles\":%lld,\"end_ideal_backlog_cycles\":%llu,"
            "\"ring_length_symbols\":%u,\"ring_cycles_floor\":%llu,\"ring_remainder\":%u,\"ring_phase_at_end\":%u,"
            "\"precomputed_delta_min_cycles\":%u,\"precomputed_delta_max_cycles\":%u,\"ring_boundary_extra_cycles_max\":1,"
            "\"schedule_bytes_reserved\":%u,\"schedule_memory\":\"internal_8bit_DRAM\",\"floor_reference_early_cycles_max\":1,\"real_reference_early_cycles_strict_upper_bound\":2,"
            "\"clock_reference\":\"nominal_240MHz_CPU_cycle_counter_not_independent_RF_clock\","
            "\"rounding_scope\":\"exact_floor_at_ring_starts; within_ring_floor_or_one_cycle_early; no_accumulating_rounding_drift\","
            "\"late_policy\":\"retain_every_source_word; catch_up_in_order; finite_end_leaves_unsent_tail\","
            "\"end_clock_error_scope\":\"loop_elapsed_minus_ideal_next_source_word_start; negative_can_be_normal_final_wait\","
            "\"per_word_lateness_observed\":false,\"max_transient_backlog_cycles\":null,"
            "\"timed_observers\":\"cycle_wait_and_finite_guard; MMIO_store; deadline_delta; source_count; ring_wrap_carry\",\"physical_rf_verified\":false}\n",
            (unsigned)baud,(unsigned)(duration_us*240),(unsigned)fast.writes,(unsigned long long)target_words,
            (unsigned long long)(target_words>fast.writes?target_words-fast.writes:0),(unsigned long long)(fast.writes>target_words?fast.writes-target_words:0),
            (unsigned)fast.next_start_cycles,(unsigned long long)ideal_next,(long long)clock_error,(unsigned long long)(clock_error>0?clock_error:0),
            (unsigned)loaded_bits,(unsigned long long)fast_schedule.ring_cycles_floor,(unsigned)fast_schedule.ring_remainder,(unsigned)fast.ring_phase,
            (unsigned)fast_schedule.min_delta_cycles,(unsigned)fast_schedule.max_delta_cycles,(unsigned)(fast_delta_capacity*sizeof(uint32_t)))>=0;
    }
    uint32_t traces_accepted=0,segments_accepted=0;
    if(result_accepted&&source_integrity_accepted&&fast_clock_accepted)for(uint32_t i=0;i<r.trace_count;++i) {
        if(output_printf("SDR {\"type\":\"tx_trace\",\"symbol\":%u,\"completion_cycle\":%u,\"late_cycles\":%u,\"word\":%u}\n",(unsigned)traces[i].symbol,(unsigned)traces[i].completion_cycle,(unsigned)traces[i].late_cycles,(unsigned)traces[i].word)<0)break;
        ++traces_accepted;
    }
    if(result_accepted&&source_integrity_accepted&&fast_clock_accepted&&traces_accepted==r.trace_count)for(uint32_t i=0;i<r.segment_records;++i) {
        if(output_printf("SDR {\"type\":\"tx_segment\",\"index\":%u,\"start_cycle\":%u,\"end_cycle\":%u,\"gap_before_cycles\":%u}\n",(unsigned)i,(unsigned)segments[i].start_cycle,(unsigned)segments[i].end_cycle,(unsigned)segments[i].gap_before_cycles)<0)break;
        ++segments_accepted;
    }
    /* Stop a failed deferred batch rather than waiting once per remaining
     * trace/segment. Preserve one bounded terminal/recovery attempt. */
    bool prior_drained=output_flush();
    output_printf("SDR {\"type\":\"tx_end\",\"trace_records\":%u,\"segment_records\":%u,"
        "\"tx_result_driver_accepted\":%s,\"trace_records_driver_accepted\":%u,\"segment_records_driver_accepted\":%u,"
        "\"fast_clock_required\":%s,\"fast_clock_driver_accepted\":%s,"
        "\"source_integrity_required\":true,\"source_integrity_driver_accepted\":%s,\"source_integrity_ok\":%s,"
        "\"all_prior_records_driver_accepted\":%s,\"prior_output_drained\":%s,\"backend_available\":%s,"
        "\"restored_word_requested\":%u,\"restored_word_readback\":%u,\"restored_word_matches\":%s,"
        "\"tone_disable_requested\":true,\"continuous_tone_disable_requested\":true,\"tone_disable_word_readback\":%u,\"tone_enable_bit_after_stop_request\":%s,"
        "\"teardown_status\":%d,\"radio_ready_after_teardown\":%s,\"wifi_driver_owned_after\":%s,\"default_event_loop_owned_after\":%s,\"direct_power_domain_owned_after\":%s,"
        "\"output_lines_attempted\":%u,\"output_lines_accepted\":%u,\"output_lines_failed\":%u,\"output_format_failures\":%u,"
        "\"output_write_calls\":%u,\"output_short_writes\":%u,\"output_zero_writes\":%u,\"output_write_errors\":%u,\"output_flush_failures\":%u,"
        "\"output_bytes_requested\":%llu,\"output_bytes_accepted\":%llu,\"output_resync_pending\":%s,"
        "\"output_counter_scope\":\"before_tx_end_format; driver_acceptance_and_prior_drain_not_host_parse_proof; final_flush_failure_visible_next_info\","
        "\"stop_proof_scope\":\"SDK_requests_and_register_readback_before_PHY_teardown_not_physical_RF_cessation\"}\n",
        (unsigned)r.trace_count,(unsigned)r.segment_records,result_accepted?"true":"false",(unsigned)traces_accepted,(unsigned)segments_accepted,
        backend==Fast?"true":"false",backend==Fast?(fast_clock_accepted?"true":"false"):"null",
        source_integrity_accepted?"true":"false",retained_source_ok(&integrity)?"true":"false",
        result_accepted&&source_integrity_accepted&&fast_clock_accepted&&traces_accepted==r.trace_count&&segments_accepted==r.segment_records?"true":"false",prior_drained?"true":"false",available?"true":"false",
        (unsigned)saved_word,(unsigned)restored_word,available?(saved_word==restored_word?"true":"false"):"null",(unsigned)tone_disabled_word,
        tone_disabled_word&METAL_ENABLE_MASK?"true":"false",teardown_status,radio_ready?"true":"false",wifi_driver_initialized?"true":"false",event_loop_owned?"true":"false",direct_power_domain_owned?"true":"false",
        (unsigned)output_stats.lines_attempted,(unsigned)output_stats.lines_accepted,(unsigned)output_stats.lines_failed,(unsigned)output_stats.format_failures,
        (unsigned)output_stats.write_calls,(unsigned)output_stats.short_writes,(unsigned)output_stats.zero_writes,(unsigned)output_stats.write_errors,(unsigned)output_stats.flush_failures,
        (unsigned long long)output_stats.bytes_requested,(unsigned long long)output_stats.bytes_accepted,output_resync_pending?"true":"false");
    (void)output_flush();
}
static void command(char *line) {
    uint32_t a,b,c,d,e,token;char mode[16],extra;
    if(!strcmp(line,"info")) { info();return; }
    if(!strcmp(line,"temperature")) { temperature();return; }
    if(sscanf(line,"reserve %" SCNu32 " %" SCNu32 " %c",&a,&b,&extra)==2) {
        if(!reserve(a,b))error("reserve allocation failed; probe remains unloaded and idle");else output_puts("SDR {\"type\":\"reserved\",\"ok\":true}");return;
    }
    if(sscanf(line,"observe %" SCNu32 " %c",&a,&extra)==1) { trace_stride=a;output_puts("SDR {\"type\":\"observe\",\"ok\":true}");return; }
    if(sscanf(line,"init %15s %c",mode,&extra)==1&&(!strcmp(mode,"phy")||!strcmp(mode,"wifi"))) { minimal_phy=!strcmp(mode,"phy");output_puts("SDR {\"type\":\"init_path\",\"ok\":true}");return; }
    if(sscanf(line,"permit %" SCNx32 " %c",&token,&extra)==1&&token) { permit=token;output_puts("SDR {\"type\":\"permit\",\"ok\":true}");return; }
    if(sscanf(line,"load %" SCNu32 " %" SCNx32 " %c",&a,&b,&extra)==2&&a&&a<=capacity) {
        loaded_bits=loaded_crc32=0;uint32_t n=(a+7)/8,got=0;if(output_puts("SDR {\"type\":\"ready\"}")<0||!output_flush())return;int64_t deadline=esp_timer_get_time()+5000000;
        while(got<n&&esp_timer_get_time()<deadline) { int count=usb_serial_jtag_read_bytes(packed+got,n-got,pdMS_TO_TICKS(50));if(count>0)got+=count; }
        if(got!=n||esp_rom_crc32_le(0,packed,n)!=b)error("upload length or CRC mismatch");else { loaded_bits=a;loaded_crc32=b;output_printf("SDR {\"type\":\"loaded\",\"bits\":%u}\n",(unsigned)a); }return;
    }
    if(sscanf(line,"tx %15s %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNx32 " %c",mode,&a,&b,&c,&d,&e,&token,&extra)==7&&
        (!strcmp(mode,"api")||!strcmp(mode,"ram")||!strcmp(mode,"gate")||!strcmp(mode,"ask")||!strcmp(mode,"fast"))&&a<=CpuHz&&b>=32&&b<=120&&c>=32&&c<=120&&
        d&&d<=DurationLimitUs&&e<=MaskLimitUs&&(!e||(!strcmp(mode,"gate")||!strcmp(mode,"ask")))&&
        (strcmp(mode,"fast")||(a&&!e&&!trace_stride))&&
        (!e||((uint64_t)d+e-1)/e+1<=trace_capacity)&&token&&token==permit&&loaded_bits) {
        permit=0;
        if(!strcmp(mode,"fast")&&!prepare_fast(a)) { error("fast schedule allocation or configuration failed before RF");return; }
        bool starting_accepted=output_puts("SDR {\"type\":\"tx_starting\"}")>=0;
        bool starting_drained=output_flush();
        if(!starting_accepted||!starting_drained)return; /* No RF without a drained start boundary. */
        transmit(!strcmp(mode,"api")?Api:!strcmp(mode,"ram")?Ram:!strcmp(mode,"gate")?Gate:!strcmp(mode,"ask")?Ask:Fast,a,b,c,d,e);return;
    }
    error("invalid command, permission, configuration or finite bound");
}
void app_main(void) {
    esp_log_level_set("*",ESP_LOG_NONE);
    usb_serial_jtag_driver_config_t cfg={.tx_buffer_size=8192,.rx_buffer_size=4096};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    if(!reserve(16384,512))error("default preallocation failed");
    info();
    char line[160];unsigned used=0;bool overflow=false;
    for(;;) { uint8_t ch;if(usb_serial_jtag_read_bytes(&ch,1,pdMS_TO_TICKS(100))!=1)continue;
        if(ch=='\r'||ch=='\n') { if(overflow)error("line too long");else if(used) { line[used]=0;command(line); }used=0;overflow=false; }
        else if(used+1<sizeof(line))line[used++]=(char)ch;else overflow=true;
    }
}
