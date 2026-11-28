/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "gain_diagnostics.h"
enum { METAL_MAX_SAMPLES = 16380, METAL_APERTURE_BYTES = 65536 };
typedef struct {
    unsigned mhz, gain;
    int offset_khz;
    bool ready;
} metal_rx_config_t;
typedef struct {
    uint32_t requested_samples, nominal_rate_hz, control, status;
    uint32_t owner_before, owner_after, lanes_before, lanes_after;
    uint32_t cycles_mod32, unchanged_sentinel_words;
    int64_t trigger_before_us, done_observed_us;
    bool hardware_done, tail_canary_ok, registers_restored;
    const char *reason;
} metal_snapshot_t;
extern metal_rx_config_t metal_rx_config;
typedef struct {
    bool registers_valid;
    const char *stage;
    unsigned requested_gain, maximum_gain;
    uint32_t syscon_bb_cfg, syscon_wifi_clock, syscon_wifi_reset, agc_raw;
    metal_gain_diagnostic_t gain;
} metal_rx_diagnostic_t;
void metal_rx_diagnostic(bool phy_enabled, metal_rx_diagnostic_t *out);
bool metal_rx_tune(unsigned mhz, int offset_khz, unsigned gain);
bool metal_rx_prepare(void);
bool metal_snapshot(unsigned count, unsigned rate_hz, uint32_t timeout_us, metal_snapshot_t *);
const uint32_t *metal_snapshot_words(void);
