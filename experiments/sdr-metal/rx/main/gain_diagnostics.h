/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original read-only visibility for the separately attributed gain helper.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool hook_ready, code_valid, analog_matches;
    uint16_t code, expected_rf, expected_bb;
    uint32_t pbus_rf_raw, pbus_bb_raw;
} metal_gain_diagnostic_t;
/* Only call while the PHY is enabled, outside all timed capture windows. */
void metal_gain_diagnostic(unsigned index, metal_gain_diagnostic_t *out);
