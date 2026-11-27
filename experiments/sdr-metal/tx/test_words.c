#include <assert.h>
#include <stdio.h>
#include "main/metal_words.h"
int main(void) {
    uint32_t state=UINT32_C(0x519a2b43);unsigned checks=0;
    for(unsigned i=0;i<200000;++i) {
        state=state*UINT32_C(1664525)+UINT32_C(1013904223);uint32_t initialized=state;
        state=state*UINT32_C(1664525)+UINT32_C(1013904223);uint32_t observed=state;
        for(unsigned level=0;level<2;++level) {
            uint32_t got=metal_gate_word(initialized,level);
            assert((got&~METAL_ENABLE_MASK)==(initialized&~METAL_ENABLE_MASK));
            assert(!!(got&METAL_ENABLE_MASK)==level);checks+=2;
        }
        uint32_t got=metal_amplitude_word(initialized,observed);
        assert((got&~METAL_AMPLITUDE_MASK)==(initialized&~METAL_AMPLITUDE_MASK));
        assert((got&METAL_AMPLITUDE_MASK)==(observed&METAL_AMPLITUDE_MASK));checks+=2;
    }
    printf("PASS %u field-preservation checks\n",checks);return 0;
}
