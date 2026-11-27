/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original small snapshot adapter. Register facts and RX preparation follow
 * ESPARGOS/esp-sdr 3ab3b65, main/targets/esp32s3/receiver.c. Exact gain/DC
 * helpers and their license are retained in ../vendor/esp-sdr.
 */
#include "snapshot.h"
#include <limits.h>
#include <string.h>
#include <sys/lock.h>
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_private/phy.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "heap_memory_layout.h"
#include "soc/soc.h"
#include "rx_recalibration.h"
#include "rx_lo.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error This snapshot register adapter is only pinned for ESP32-S3.
#endif
#define APERTURE_BEGIN 0x3fcd0000u
#define APERTURE_END   0x3fce0000u
#define OWNER_REG      0x600c101cu
#define DUMP_CTRL      0x60033d5cu
#define DUMP_LANES     0x60033d90u
#define GAIN_REG       0x6001c02cu
#define CANARY         0x96b4e20du
#define SENTINEL       0xa5a0055au
SOC_RESERVE_MEMORY_REGION(APERTURE_BEGIN, APERTURE_END, metal_snapshot_sram);
static volatile uint32_t *const aperture = (volatile uint32_t *)APERTURE_BEGIN;
static portMUX_TYPE capture_mux = portMUX_INITIALIZER_UNLOCKED;
metal_rx_config_t metal_rx_config = {.mhz = 2433, .offset_khz = -80, .gain = 40};
extern void set_chanfreq(unsigned, unsigned);
extern void set_rf_freq_offset(unsigned, unsigned, int);
extern void stop_tx_tone(unsigned);
extern void rom_pbus_workmode(void);
extern void rom_pbus_xpd_tx_off(void);
extern void rom_pbus_xpd_rx_on(unsigned);
extern void rom_set_rxclk_en(unsigned);
extern bool rx_gain_verified_apply(bool manual, unsigned index);
extern bool rx_gain_verified_check(unsigned index);

/* Required by the SDK RF-test archive. No RF-test shell is exposed. */
int cmd_parse(char *cmd, char *name, int *argc, char **argv) {
    (void)cmd; (void)name; (void)argc; (void)argv; return -1;
}

static const char *tune_stage = "not_attempted";
static unsigned tune_gain;
void metal_rx_diagnostic(bool phy_enabled, metal_rx_diagnostic_t *out) {
    *out = (metal_rx_diagnostic_t){.registers_valid = phy_enabled,
        .stage = phy_enabled ? tune_stage : "phy_not_enabled", .requested_gain = tune_gain};
    if (!phy_enabled) return;
    /* Explicit setup observation only; no capture, allocation or serial I/O. */
    out->syscon_bb_cfg = REG_READ(0x6002600cu);
    out->syscon_wifi_clock = REG_READ(0x60026014u);
    out->syscon_wifi_reset = REG_READ(0x60026018u);
    out->agc_raw = REG_READ(GAIN_REG);
    out->maximum_gain = (out->agc_raw >> 8) & 127u;
    metal_gain_diagnostic(tune_gain, &out->gain);
}

bool metal_rx_tune(unsigned mhz, int offset_khz, unsigned gain) {
    tune_gain = gain; tune_stage = "invalid_arguments";
    if (!mhz || mhz > UINT16_MAX || gain > 82) return false;
    rx_lo_plan_t plan = rx_lo_plan(mhz);
    int64_t offset = (int64_t)plan.offset_khz + offset_khz;
    if (offset < INT_MIN || offset > INT_MAX) return false;
    metal_rx_config.ready = false;
    rx_recalibrate(mhz);
    rx_lo_select(false);
    bool channel = !offset_khz && ((mhz >= 2412 && mhz <= 2472 && (mhz - 2412) % 5 == 0) || mhz == 2484);
    set_chanfreq(channel ? mhz : 2412, 0);
    if (!channel) set_rf_freq_offset(0, plan.mhz, (int)offset);
    stop_tx_tone(1);
    rom_pbus_workmode();
    rom_pbus_xpd_tx_off();
    rom_pbus_xpd_rx_on(1);
    rom_set_rxclk_en(1);
    rx_lo_select(plan.alternate);
    esp_rom_delay_us(3000);
    unsigned maximum = (REG_READ(GAIN_REG) >> 8) & 127u;
    tune_stage = "maximum_gain_out_of_range";
    if (maximum > 82) return false;
    tune_stage = "requested_gain_above_maximum";
    if (gain > maximum) return false;
    if (!rx_gain_verified_apply(true, gain)) {
        metal_gain_diagnostic_t diagnostic;
        metal_gain_diagnostic(gain, &diagnostic);
        tune_stage = !diagnostic.hook_ready || !diagnostic.code_valid ? "gain_table_code_unavailable" : "analog_gain_not_latched";
        return false;
    }
    tune_stage = "ready";
    metal_rx_config = (metal_rx_config_t){.mhz = mhz, .offset_khz = offset_khz, .gain = gain, .ready = true};
    return true;
}

bool metal_rx_prepare(void) {
    if (!metal_rx_config.ready || rx_recalibration_stale())
        return metal_rx_tune(metal_rx_config.mhz, metal_rx_config.offset_khz, metal_rx_config.gain);
    return rx_gain_verified_check(metal_rx_config.gain);
}

const uint32_t *metal_snapshot_words(void) { return (const uint32_t *)APERTURE_BEGIN; }

bool IRAM_ATTR metal_snapshot(unsigned n, unsigned rate, uint32_t timeout_us, metal_snapshot_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->reason = "invalid_arguments";
    if (!n || n > METAL_MAX_SAMPLES || !timeout_us || (rate != 16000000 && rate != 40000000 && rate != 80000000)) return false;
    out->requested_samples = n;
    out->nominal_rate_hz = rate;
    _lock_t phy_lock = phy_get_lock();
    _lock_acquire(&phy_lock);
    portENTER_CRITICAL(&capture_mux);
    uint32_t old_owner = REG_READ(OWNER_REG), old_lanes = REG_READ(DUMP_LANES), old_ctrl = REG_READ(DUMP_CTRL);
    out->owner_before = old_owner;
    out->lanes_before = old_lanes;
    /* An existing enabled dump is a collision, never silently borrowed. */
    if (!(old_ctrl & 0x80000000u)) {
        /* Fill only after rejecting an active dump collision. No USB or
         * allocator call occurs in this protected region. */
        for (unsigned j = 0; j < n; ++j) aperture[j] = SENTINEL;
        for (unsigned j = METAL_MAX_SAMPLES; j < METAL_APERTURE_BYTES / 4; ++j) aperture[j] = CANARY;
        uint32_t rate_bits = rate == 40000000 ? 1u << 15 : rate == 16000000 ? 1u << 16 : 0;
        out->control = 0x80000000u | rate_bits | n;
        REG_WRITE(DUMP_CTRL, 0);
        REG_WRITE(DUMP_LANES, (1u << 6) | (2u << 12) | (3u << 18));
        REG_WRITE(OWNER_REG, (old_owner & ~15u) | 4u);
        __asm__ __volatile__("memw" ::: "memory");
        out->trigger_before_us = esp_timer_get_time();
        uint32_t begin_cycles = esp_cpu_get_cycle_count();
        REG_WRITE(DUMP_CTRL, out->control);
        REG_WRITE(DUMP_CTRL, out->control | (1u << 19));
        REG_WRITE(DUMP_CTRL, out->control);
        do {
            out->status = REG_READ(DUMP_CTRL);
            if (out->status & (1u << 18)) break;
        } while ((uint64_t)(esp_timer_get_time() - out->trigger_before_us) < timeout_us);
        out->done_observed_us = esp_timer_get_time();
        out->cycles_mod32 = esp_cpu_get_cycle_count() - begin_cycles;
        out->hardware_done = (out->status & (1u << 18)) != 0;
        REG_WRITE(DUMP_CTRL, 0);
        out->reason = out->hardware_done ? "complete" : "hardware_done_timeout";
    } else {
        out->reason = "dump_already_active";
        out->owner_after = old_owner;
        out->lanes_after = old_lanes;
        out->registers_restored = true; /* No register or SRAM writes made. */
        portEXIT_CRITICAL(&capture_mux);
        _lock_release(&phy_lock);
        return false;
    }
    /* Restoration is unconditional after ownership was changed, including
     * timeout. Busy collisions above never mutate the active capture. */
    REG_WRITE(DUMP_LANES, old_lanes);
    REG_WRITE(OWNER_REG, old_owner);
    REG_WRITE(DUMP_CTRL, old_ctrl);
    __asm__ __volatile__("memw" ::: "memory");
    out->owner_after = REG_READ(OWNER_REG);
    out->lanes_after = REG_READ(DUMP_LANES);
    out->registers_restored = out->owner_after == old_owner && out->lanes_after == old_lanes && REG_READ(DUMP_CTRL) == old_ctrl;
    portEXIT_CRITICAL(&capture_mux);
    _lock_release(&phy_lock);
    out->tail_canary_ok = true;
    for (unsigned j = METAL_MAX_SAMPLES; j < METAL_APERTURE_BYTES / 4; ++j)
        if (aperture[j] != CANARY) out->tail_canary_ok = false;
    for (unsigned j = 0; j < n; ++j) out->unchanged_sentinel_words += aperture[j] == SENTINEL;
    if (!out->registers_restored) out->reason = "register_restore_mismatch";
    else if (!out->tail_canary_ok) out->reason = "tail_canary_changed";
    else if (out->hardware_done && out->unchanged_sentinel_words) out->reason = "unchanged_sentinel_words";
    return out->hardware_done && out->registers_restored && out->tail_canary_ok && !out->unchanged_sentinel_words;
}
