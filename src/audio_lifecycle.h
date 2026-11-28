// Callback lifetime gate. MIT; Diviner contributors.
#pragma once
#include "audio_exchange.h"
#include <atomic>
#include <cstdint>

namespace audio_lifecycle {
using Callback = void (*)(int16_t *, int);

// One audio consumer, one control producer. The producer changes callbacks
// only outside a callback. A successful render admission owns BOTH pointers
// until release(); detaching either pointer waits for that admission to end.
// Audio never waits: a concurrent publication produces a silent block.
class Callbacks {
    alignas(4) std::atomic<uint32_t> gate_{0};
    alignas(4) std::atomic<uint32_t> writerPending_{0};
    Callback main_ = nullptr, overlay_ = nullptr;
    bool tryAcquire() noexcept {
        uint32_t expected = 0;
        // One strong 32-bit CAS, including on the qualified S3 toolchain.
        // Subword atomic_flag helpers may hide a compare/exchange retry loop.
        return gate_.compare_exchange_strong(expected, 1, std::memory_order_acquire,
                                             std::memory_order_relaxed);
    }
public:
    Callbacks() {
        audio_exchange::detail::checkStorage(&gate_);
        audio_exchange::detail::checkStorage(&writerPending_);
    }
    bool acquire(Callback &main, Callback &overlay) noexcept {
        if (writerPending_.load(std::memory_order_seq_cst)) return false;
        if (!tryAcquire()) return false;
        if (writerPending_.load(std::memory_order_seq_cst)) { release(); return false; }
        main = main_; overlay = overlay_;
        return true;
    }
    void release() noexcept { gate_.store(0, std::memory_order_release); }
    template<class Yield> void setMain(Callback fn, Yield yield) {
        writerPending_.store(1, std::memory_order_seq_cst);
        while (!tryAcquire()) yield();
        main_ = fn;
        release();
        writerPending_.store(0, std::memory_order_seq_cst);
    }
    template<class Yield> void setOverlay(Callback fn, Yield yield) {
        writerPending_.store(1, std::memory_order_seq_cst);
        while (!tryAcquire()) yield();
        overlay_ = fn;
        release();
        writerPending_.store(0, std::memory_order_seq_cst);
    }
};
} // namespace audio_lifecycle
