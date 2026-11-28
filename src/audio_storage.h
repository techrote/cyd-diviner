// Audio state must remain in CPU-internal memory on ESP32 targets. Besides
// avoiding external-memory stalls, this is the precondition for the bounded
// S32C1I atomic path on the ESP32-S3 IDF 5.5 PSRAM-workaround toolchain.
#pragma once
#include <cstddef>
#include <new>

#if defined(GRIM_DEVICE)
#include <esp_heap_caps.h>
#endif

namespace audio_exchange {
// Empty allocation-policy base: no vtable, payload or callback-time work.
// Native builds deliberately keep global new/delete, including the existing
// allocation-failure test seam. Device failure remains a null nothrow result.
struct InternalState {
#if defined(GRIM_DEVICE)
    static void *operator new(std::size_t bytes, const std::nothrow_t &) noexcept {
        return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    static void operator delete(void *p) noexcept { heap_caps_free(p); }
    static void operator delete(void *p, const std::nothrow_t &) noexcept { heap_caps_free(p); }
#endif
};
} // namespace audio_exchange
