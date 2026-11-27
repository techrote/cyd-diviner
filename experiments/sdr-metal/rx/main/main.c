/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original headless command/deferred-transfer wrapper. No Diviner dependency.
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "driver/usb_serial_jtag.h"
#include "esp_chip_info.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_phy_init.h"
#include "esp_private/esp_clk.h"
#include "esp_rom_crc.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "snapshot.h"
#include "source_identity.h"

static bool initialized, last_available;
static esp_err_t initialization_error;
static bool direct_phy_enabled, wifi_driver_initialized, default_event_loop_created;
static esp_err_t wifi_stop_status = INT_MIN, wifi_deinit_status = INT_MIN, event_loop_delete_status = INT_MIN;
static metal_snapshot_t last_snapshot;
static uint32_t last_crc, transfer_failures;
typedef struct { size_t free, largest, minimum; } heap_t;
static heap_t last_before, last_after;
static char last_source_id[32];
static uint32_t last_source_repetition;
static heap_t heap(void) {
    return (heap_t){heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)};
}
static bool send_bytes(const void *data, size_t length) {
    const uint8_t *p = data;
    int64_t deadline = esp_timer_get_time() + 5000000;
    while (length && esp_timer_get_time() < deadline) {
        size_t chunk = length > 1024 ? 1024 : length;
        int n = usb_serial_jtag_write_bytes(p, chunk, pdMS_TO_TICKS(100));
        if (n < 0 || (size_t)n > chunk) break;
        p += n; length -= n;
    }
    if (length) { ++transfer_failures; return false; }
    return true;
}
static bool line(const char *format, ...) __attribute__((format(printf, 1, 2)));
static bool line(const char *format, ...) {
    char text[2048];
    va_list args; va_start(args, format);
    int n = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(text)) { ++transfer_failures; return false; }
    return send_bytes(text, (size_t)n);
}
static void error(const char *reason) {
    (void)line("METAL {\"schema\":1,\"type\":\"error\",\"reason\":\"%s\"}\n", reason);
}
static bool flush(void) {
    if (usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(5000)) == ESP_OK) return true;
    ++transfer_failures; return false;
}
static bool valid_id(const char *id) {
    if (!*id || strlen(id) > 31) return false;
    for (; *id; ++id) if (!isalnum((unsigned char)*id) && *id != '_' && *id != '-') return false;
    return true;
}
static void wait_us(uint32_t us) {
    int64_t target = esp_timer_get_time() + us;
    while (target - esp_timer_get_time() > 2000) vTaskDelay(1);
    int64_t remaining = target - esp_timer_get_time();
    if (remaining > 0) esp_rom_delay_us((uint32_t)remaining);
}
static const char *init_mode(void) {
    return direct_phy_enabled ? "direct_PHY" : wifi_driver_initialized ? (initialized ? "WiFi_NULL" : "WiFi_driver_partial") : "none";
}
static bool initialize(bool wifi) {
    if (initialized && (wifi ? wifi_driver_initialized : direct_phy_enabled)) return true;
    initialized = false; last_available = false; last_crc = 0; metal_rx_config.ready = false;
    initialization_error = ESP_OK;
    if (wifi_driver_initialized) {
        wifi_stop_status = esp_wifi_stop();
        if (wifi_stop_status != ESP_OK && wifi_stop_status != ESP_ERR_WIFI_NOT_STARTED) { initialization_error = wifi_stop_status; return false; }
        wifi_deinit_status = esp_wifi_deinit();
        if (wifi_deinit_status != ESP_OK) { initialization_error = wifi_deinit_status; return false; }
        wifi_driver_initialized = false;
    }
    if (default_event_loop_created) {
        event_loop_delete_status = esp_event_loop_delete_default();
        if (event_loop_delete_status != ESP_OK) { initialization_error = event_loop_delete_status; return false; }
        default_event_loop_created = false;
    }
    if (direct_phy_enabled) { esp_phy_disable(PHY_MODEM_WIFI); direct_phy_enabled = false; }
    if (!wifi) {
        /* This SDK API is void. Gain verification establishes receiver-ready
         * state later; this flag records the actual initialization path. */
        esp_phy_enable(PHY_MODEM_WIFI); direct_phy_enabled = true; initialized = true;
        return true;
    }
    esp_err_t result = esp_event_loop_create_default();
    if (result == ESP_OK) {
        default_event_loop_created = true;
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); cfg.nvs_enable = 0;
        result = esp_wifi_init(&cfg);
        if (result == ESP_OK) wifi_driver_initialized = true;
        if (result == ESP_OK) result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (result == ESP_OK) result = esp_wifi_set_mode(WIFI_MODE_NULL);
        if (result == ESP_OK) result = esp_wifi_start();
        if (result == ESP_OK) result = esp_wifi_set_ps(WIFI_PS_NONE);
        if (result == ESP_OK) result = esp_wifi_set_promiscuous(true);
        if (result == ESP_OK) result = esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    }
    initialized = result == ESP_OK; initialization_error = result;
    return initialized;
}
static void info(void) {
    heap_t h = heap(); uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    esp_chip_info_t chip; esp_chip_info(&chip);
    (void)line("METAL {\"schema\":1,\"type\":\"info\",\"profile\":\"s3-metal-snapshot\","
        "\"source_sha\":\"%s\",\"source_digest\":\"%s\",\"sdk_sha\":\"%s\",\"sdk\":\"%s\","
        "\"backend_commit\":\"3ab3b65bb62f2d9427ee33c2c3b627427e729294\","
        "\"mac\":\"%02x%02x%02x%02x%02x%02x\",\"chip_revision\":%u,\"cpu_mhz\":%u,"
        "\"init_mode\":\"%s\",\"direct_enable_api_returns_status\":false,"
        "\"wifi_driver_initialized\":%s,\"default_event_loop_created\":%s,"
        "\"wifi_stop_status\":%d,\"wifi_deinit_status\":%d,\"event_loop_delete_status\":%d,"
        "\"nvs_enabled\":false,\"psram_required\":false,\"initialized\":%s,\"initialization_error_code\":%d,"
        "\"aperture_begin\":\"3fcd0000\",\"aperture_bytes\":65536,\"max_samples\":16380,"
        "\"nominal_rates_hz\":[16000000,40000000,80000000],"
        "\"max_nominal_horizon_us\":[1023.75,409.5,204.75],"
        "\"frequency_mhz\":%u,\"offset_khz\":%d,\"manual_gain_index\":%u,\"rx_ready\":%s,"
        "\"internal_8bit_free\":%u,\"internal_8bit_largest\":%u,\"internal_8bit_minimum\":%u,"
        "\"transfer_failures\":%" PRIu32 ",\"continuous_capture\":false,\"one_way_latency_measured\":false}\n",
        METAL_SOURCE_SHA, METAL_SOURCE_DIGEST, METAL_SDK_SHA, esp_get_idf_version(),
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], chip.revision, (unsigned)(esp_clk_cpu_freq() / 1000000),
        init_mode(), wifi_driver_initialized ? "true" : "false", default_event_loop_created ? "true" : "false",
        wifi_stop_status, wifi_deinit_status, event_loop_delete_status,
        initialized ? "true" : "false", initialization_error, metal_rx_config.mhz, metal_rx_config.offset_khz, metal_rx_config.gain,
        metal_rx_config.ready ? "true" : "false", (unsigned)h.free, (unsigned)h.largest, (unsigned)h.minimum, transfer_failures);
}
static bool export_snapshot(const char *id, uint32_t repetition, heap_t before, heap_t after) {
    const metal_snapshot_t *r = &last_snapshot;
    uint32_t bytes = r->requested_samples * 4;
    if (!line("METAL {\"schema\":1,\"type\":\"snapshot\",\"id\":\"%s\",\"repetition\":%" PRIu32 ","
        "\"source_id\":\"%s\",\"source_repetition\":%" PRIu32 ","
        "\"complete\":true,\"reason\":\"%s\",\"sample_count\":%" PRIu32 ",\"raw_bytes\":%" PRIu32 ","
        "\"raw_format\":\"LE32_I10_Q10_metadata12_native\",\"crc32\":\"%08" PRIx32 "\","
        "\"nominal_rate_hz\":%" PRIu32 ",\"nominal_horizon_us\":%.9g,"
        "\"trigger_before_us\":%" PRId64 ",\"done_observed_us\":%" PRId64 ",\"capture_cycles_mod32\":%" PRIu32 ","
        "\"control\":\"%08" PRIx32 "\",\"status\":\"%08" PRIx32 "\",\"hardware_done\":true,"
        "\"owner_before\":\"%08" PRIx32 "\",\"owner_after\":\"%08" PRIx32 "\","
        "\"lanes_before\":\"%08" PRIx32 "\",\"lanes_after\":\"%08" PRIx32 "\","
        "\"registers_restored\":true,\"tail_canary_ok\":true,\"unchanged_sentinel_words\":0,"
        "\"heap_scope\":\"run_prearm_to_snapshot_or_bench_loop_end\","
        "\"internal_8bit_free_before\":%u,\"internal_8bit_free_after\":%u,"
        "\"internal_8bit_largest_before\":%u,\"internal_8bit_largest_after\":%u,"
        "\"usb_during_capture\":false,\"allocation_during_capture\":false}\n",
        id, repetition, last_source_id, last_source_repetition, r->reason, r->requested_samples, bytes, last_crc,
        r->nominal_rate_hz, 1e6 * r->requested_samples / r->nominal_rate_hz,
        r->trigger_before_us, r->done_observed_us, r->cycles_mod32, r->control, r->status,
        r->owner_before, r->owner_after, r->lanes_before, r->lanes_after,
        (unsigned)before.free, (unsigned)after.free, (unsigned)before.largest, (unsigned)after.largest)) return false;
    /* No capture runs while this payload is exported. CRC covers exactly the
     * unmodified native words, including their upper metadata bits. */
    return send_bytes(metal_snapshot_words(), bytes) && send_bytes("\n", 1) && flush();
}
static void run(const char *id, bool exporting, uint32_t rate, uint32_t n, uint32_t repeats,
                uint32_t arm_delay, uint32_t gap, uint32_t timeout) {
    if (!initialized || !valid_id(id) || !n || n > METAL_MAX_SAMPLES || !repeats || !timeout ||
        (rate != 16000000 && rate != 40000000 && rate != 80000000)) { error("invalid_run_or_initialization"); return; }
    if (!metal_rx_prepare()) { error("rx_prepare_or_gain_failed"); return; }
    last_available = false; last_crc = 0;
    memset(&last_snapshot, 0, sizeof(last_snapshot));
    heap_t before = heap();
    if (!line("METAL {\"schema\":1,\"type\":\"armed\",\"id\":\"%s\",\"mode\":\"%s\","
        "\"sample_count\":%" PRIu32 ",\"nominal_rate_hz\":%" PRIu32 ",\"repeats_requested\":%" PRIu32 ","
        "\"arm_delay_us\":%" PRIu32 ",\"inter_snapshot_gap_us\":%" PRIu32 ",\"timeout_us\":%" PRIu32 "}\n",
        id, exporting ? "capture" : "bench", n, rate, repeats, arm_delay, gap, timeout) || !flush()) return;
    wait_us(arm_delay);
    int64_t begin = esp_timer_get_time(), first_trigger = 0, last_done = 0;
    uint64_t capture_sum_us = 0, cycles_sum = 0;
    uint32_t completed = 0, capture_max_us = 0, transfer_start = transfer_failures;
    const char *reason = "complete";
    for (uint32_t rep = 0; rep < repeats; ++rep) {
        if (!metal_rx_prepare()) { reason = "rx_prepare_or_gain_failed"; break; }
        last_available = false; last_crc = 0;
        if (!metal_snapshot(n, rate, timeout, &last_snapshot)) { reason = last_snapshot.reason; break; }
        ++completed;
        if (!first_trigger) first_trigger = last_snapshot.trigger_before_us;
        last_done = last_snapshot.done_observed_us;
        uint32_t duration = (uint32_t)(last_done - last_snapshot.trigger_before_us);
        capture_sum_us += duration; cycles_sum += last_snapshot.cycles_mod32;
        if (duration > capture_max_us) capture_max_us = duration;
        last_available = true;
        memcpy(last_source_id, id, strlen(id) + 1); last_source_repetition = completed;
        last_before = before;
        if (exporting) {
            last_after = heap();
            last_crc = esp_rom_crc32_le(0, (const uint8_t *)metal_snapshot_words(), n * 4);
            if (!export_snapshot(id, completed, last_before, last_after)) { reason = "deferred_usb_transfer_failed"; break; }
        }
        if (rep + 1 < repeats && gap) wait_us(gap);
    }
    int64_t finish = esp_timer_get_time();
    heap_t after = heap();
    /* In no-export mode even CRC is deferred until the capture loop ends. */
    if (!exporting && last_available) last_after = after;
    if (!exporting && last_available) last_crc = esp_rom_crc32_le(0, (const uint8_t *)metal_snapshot_words(), n * 4);
    (void)line("METAL {\"schema\":1,\"type\":\"run_end\",\"id\":\"%s\",\"mode\":\"%s\","
        "\"ok\":%s,\"reason\":\"%s\",\"repeats_requested\":%" PRIu32 ",\"captures_completed\":%" PRIu32 ","
        "\"sample_count_each\":%" PRIu32 ",\"nominal_rate_hz\":%" PRIu32 ","
        "\"loop_begin_us\":%" PRId64 ",\"loop_end_us\":%" PRId64 ",\"loop_elapsed_us\":%" PRId64 ","
        "\"first_trigger_us\":%" PRId64 ",\"last_done_us\":%" PRId64 ","
        "\"capture_observation_us_sum\":%" PRIu64 ",\"capture_observation_us_max\":%" PRIu32 ","
        "\"capture_cycles_mod32_sum\":%" PRIu64 ",\"nominal_sample_horizon_us_sum\":%.12g,"
        "\"last_raw_retained\":%s,\"last_raw_crc32\":\"%08" PRIx32 "\","
        "\"last_owner_restored\":%s,\"last_canary_ok\":%s,"
        "\"internal_8bit_free_before\":%u,\"internal_8bit_free_after\":%u,"
        "\"internal_8bit_largest_before\":%u,\"internal_8bit_largest_after\":%u,"
        "\"transfer_failures_delta\":%" PRIu32 ",\"continuous_capture\":false,\"one_way_latency_measured\":false}\n",
        id, exporting ? "capture" : "bench", completed == repeats && !strcmp(reason, "complete") ? "true" : "false",
        reason, repeats, completed, n, rate, begin, finish, finish - begin, first_trigger, last_done,
        capture_sum_us, capture_max_us, cycles_sum, completed * (1e6 * n / rate),
        last_available ? "true" : "false", last_crc,
        last_snapshot.registers_restored ? "true" : "false", last_snapshot.tail_canary_ok ? "true" : "false",
        (unsigned)before.free, (unsigned)after.free, (unsigned)before.largest, (unsigned)after.largest, transfer_failures - transfer_start);
}
static bool number_u32(const char *text, uint32_t *out) {
    if (!isdigit((unsigned char)*text)) return false;
    char *end; errno = 0; unsigned long value = strtoul(text, &end, 10);
    if (errno || *end || value > UINT32_MAX) return false;
    *out = (uint32_t)value; return true;
}
static bool number_i32(const char *text, int *out) {
    if (!isdigit((unsigned char)*text) && *text != '-') return false;
    char *end; errno = 0; long value = strtol(text, &end, 10);
    if (errno || end == text || *end || value < INT_MIN || value > INT_MAX) return false;
    *out = (int)value; return true;
}
static void command(const char *text) {
    char copy[256], *part[10], *save = NULL; unsigned count = 0;
    if (strlen(text) >= sizeof(copy)) { error("line_too_long"); return; }
    memcpy(copy, text, strlen(text) + 1);
    for (char *p = strtok_r(copy, " \t", &save); p; p = strtok_r(NULL, " \t", &save)) {
        if (count == 10) { error("command"); return; }
        part[count++] = p;
    }
    if (count < 2 || strcmp(part[0], "metal")) { error("command"); return; }
    if (count == 2 && !strcmp(part[1], "info")) { info(); return; }
    if (count == 3 && !strcmp(part[1], "init") && (!strcmp(part[2], "phy") || !strcmp(part[2], "wifi"))) {
        bool ok = initialize(!strcmp(part[2], "wifi"));
        (void)line("METAL {\"schema\":1,\"type\":\"init\",\"ok\":%s,\"requested\":\"%s\",\"actual_mode\":\"%s\",\"error_code\":%d,\"rx_ready\":%s}\n", ok ? "true" : "false", part[2], init_mode(), initialization_error, metal_rx_config.ready ? "true" : "false");
        return;
    }
    if (count == 5 && !strcmp(part[1], "tune")) {
        uint32_t mhz, gain; int offset;
        if (!number_u32(part[2], &mhz) || !number_i32(part[3], &offset) || !number_u32(part[4], &gain)) { error("numeric_argument"); return; }
        last_available = false; last_crc = 0;
        bool ok = initialized && metal_rx_tune(mhz, offset, gain);
        (void)line("METAL {\"schema\":1,\"type\":\"tune\",\"ok\":%s,\"frequency_mhz\":%" PRIu32 ",\"offset_khz\":%d,\"manual_gain_index\":%" PRIu32 "}\n", ok ? "true" : "false", mhz, offset, gain);
        return;
    }
    if (count == 9 && (!strcmp(part[1], "capture") || !strcmp(part[1], "bench"))) {
        uint32_t values[6];
        for (unsigned j = 0; j < 6; ++j) if (!number_u32(part[3+j], &values[j])) { error("numeric_argument"); return; }
        run(part[2], !strcmp(part[1], "capture"), values[0], values[1], values[2], values[3], values[4], values[5]);
        return;
    }
    if (count == 3 && !strcmp(part[1], "read") && valid_id(part[2])) {
        if (last_available) (void)export_snapshot(part[2], 0, last_before, last_after);
        else error("no_complete_snapshot_retained");
        return;
    }
    error("command");
}
void app_main(void) {
    esp_log_level_set("*", ESP_LOG_NONE);
    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size = 8192, .rx_buffer_size = 512};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    (void)initialize(false); /* Default direct PHY; Wi-Fi is an explicit fallback. */
    /* No NVS initialization/erase, no GUI/DSP/PSRAM initialization, no automatic
     * capture. Suppress PHY setup logging before exposing the protocol. */
    esp_log_level_set("*", ESP_LOG_NONE);
    char text[256]; size_t length = 0; bool overflow = false;
    for (;;) {
        uint8_t byte;
        if (usb_serial_jtag_read_bytes(&byte, 1, portMAX_DELAY) != 1) continue;
        if (byte == '\r') continue;
        if (byte == '\n') {
            text[length] = 0;
            if (overflow) error("line_too_long");
            else if (length) command(text);
            length = 0; overflow = false;
        } else if (length + 1 < sizeof(text)) text[length++] = (char)byte;
        else overflow = true;
    }
}
