#pragma once
#include <stdint.h>
#define METAL_ENABLE_MASK (UINT32_C(1) << 18)
#define METAL_AMPLITUDE_MASK (UINT32_C(255) << 10)
/* Inferred only from the pinned S3 vendor object's own tone-step implementation. */
static inline uint32_t metal_gate_word(uint32_t initialized, unsigned level) {
    return (initialized & ~METAL_ENABLE_MASK) | (level ? METAL_ENABLE_MASK : 0);
}
static inline uint32_t metal_amplitude_word(uint32_t initialized, uint32_t observed) {
    return (initialized & ~METAL_AMPLITUDE_MASK) | (observed & METAL_AMPLITUDE_MASK);
}
