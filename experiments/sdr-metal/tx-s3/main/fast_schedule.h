/* Original portable rational-clock schedule. No SDK, allocation or MMIO. */
#ifndef CYD_METAL_FAST_SCHEDULE_H
#define CYD_METAL_FAST_SCHEDULE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t cpu_hz,baud,length,ring_remainder,min_delta_cycles,max_delta_cycles;
    uint64_t ring_cycles_floor;
} metal_fast_schedule_t;

/* Build one phase-zero ring outside RF. Repeated rings add the fractional
 * carry at their boundary. Every ring start equals floor(global_index*C/B).
 * Within a ring, the start is either that exact floor or one cycle earlier;
 * relative to the real rational clock it is less than two cycles early.
 * No long-term rounding drift accumulates. C>=B makes every delta positive. */
static inline bool metal_fast_schedule_build(metal_fast_schedule_t *out,
        uint32_t cpu_hz,uint32_t baud,uint32_t length,uint32_t *deltas) {
    if(!out||!deltas||!cpu_hz||!baud||baud>cpu_hz||!length)return false;
    uint32_t base=cpu_hz/baud,remainder=cpu_hz%baud,phase=0;bool any_carry=false;
    /* Use subtraction rather than phase+remainder: valid even for a portable
     * caller whose clock is near UINT32_MAX rather than this target's240MHz. */
    for(uint32_t i=0;i<length;++i) {
        bool carry=phase>=baud-remainder;
        any_carry|=carry;
        phase=carry?phase-(baud-remainder):phase+remainder;
        deltas[i]=base+(carry?1u:0u);
    }
    *out=(metal_fast_schedule_t){cpu_hz,baud,length,phase,base,base+(any_carry?1u:0u),
        (uint64_t)length*cpu_hz/baud};
    return true;
}

/* One call per ring, not per symbol. Header-only and forced inline so no
 * flash call is introduced when used from an IRAM target loop. */
static inline __attribute__((always_inline)) uint32_t metal_fast_ring_carry(
        uint32_t *phase,uint32_t ring_remainder,uint32_t baud) {
    bool carry=*phase>=baud-ring_remainder;
    *phase=carry?*phase-(baud-ring_remainder):*phase+ring_remainder;
    return carry?1u:0u;
}
#endif
