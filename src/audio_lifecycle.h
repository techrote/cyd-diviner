// Callback lifetime gate. MIT; Diviner contributors.
#pragma once
#include <atomic>
#include <cstdint>

namespace audio_lifecycle {
using Callback = void (*)(int16_t *, int);

// One audio consumer, one control producer. The producer changes callbacks
// only outside a callback. A successful render admission owns BOTH pointers
// until release(); detaching either pointer waits for that admission to end.
// Audio never waits: a concurrent publication produces a silent block.
class Callbacks {
    std::atomic_flag gate_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> writerPending_{false};
    Callback main_ = nullptr, overlay_ = nullptr;
public:
    bool acquire(Callback &main, Callback &overlay) noexcept {
        if (writerPending_.load(std::memory_order_seq_cst)) return false;
        if (gate_.test_and_set(std::memory_order_acquire)) return false;
        if (writerPending_.load(std::memory_order_seq_cst)) { release(); return false; }
        main = main_; overlay = overlay_;
        return true;
    }
    void release() noexcept { gate_.clear(std::memory_order_release); }
    template<class Yield> void setMain(Callback fn, Yield yield) {
        writerPending_.store(true, std::memory_order_seq_cst);
        while (gate_.test_and_set(std::memory_order_acquire)) yield();
        main_ = fn;
        release();
        writerPending_.store(false, std::memory_order_seq_cst);
    }
    template<class Yield> void setOverlay(Callback fn, Yield yield) {
        writerPending_.store(true, std::memory_order_seq_cst);
        while (gate_.test_and_set(std::memory_order_acquire)) yield();
        overlay_ = fn;
        release();
        writerPending_.store(false, std::memory_order_seq_cst);
    }
};
} // namespace audio_lifecycle
