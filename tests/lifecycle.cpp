#include "../src/audio_lifecycle.h"
#include "../src/timing_stats.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <initializer_list>

namespace {
std::atomic<bool> entered{false}, leaveCallback{false};
std::atomic<unsigned> rendered{0};
int *state = nullptr;
void held(int16_t *out, int n) {
    entered.store(true, std::memory_order_release);
    while (!leaveCallback.load(std::memory_order_acquire)) std::this_thread::yield();
    for (int i = 0; i < n; ++i) out[i] = *state;
}
void quick(int16_t *out, int n) {
    for (int i = 0; i < n; ++i) out[i] = *state;
    rendered.fetch_add(1, std::memory_order_relaxed);
}
}
int main() {
    audio_lifecycle::Callbacks gate;
    auto yield = [] { std::this_thread::yield(); };
    for (bool overlay : {false, true}) {
        entered = false; leaveCallback = false; state = new int(27);
        if (overlay) gate.setOverlay(held, yield); else gate.setMain(held, yield);
        std::thread audio([&] {
            audio_lifecycle::Callback a, b;
            int16_t out[32];
            assert(gate.acquire(a, b));
            (overlay ? b : a)(out, 32);
            assert(out[31] == 27);
            gate.release();
        });
        while (!entered.load(std::memory_order_acquire)) yield();
        std::atomic<bool> detached{false}, writerWaiting{false}, letWriterProceed{false};
        std::thread ui([&] {
            auto heldWriter = [&] {
                // Observe a real failed lock attempt, then deliberately keep
                // the producer descheduled while testing reader admission.
                writerWaiting.store(true, std::memory_order_release);
                while (!letWriterProceed.load(std::memory_order_acquire)) yield();
            };
            if (overlay) gate.setOverlay(nullptr, heldWriter); else gate.setMain(nullptr, heldWriter);
            delete state; state = nullptr;
            detached.store(true, std::memory_order_release);
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!writerWaiting.load(std::memory_order_acquire)) {
            assert(std::chrono::steady_clock::now() < deadline);
            yield();
        }
        assert(!detached.load(std::memory_order_acquire));
        leaveCallback.store(true, std::memory_order_release);
        audio.join();
        // The old callback has finished and the underlying gate is now free.
        // A pending writer must prevent a fast audio loop from reacquiring it
        // before that writer is scheduled again. This is deterministic: the
        // writer is held in its yield function throughout all 100000 attempts.
        for (int i = 0; i < 100000; ++i) {
            audio_lifecycle::Callback a, b;
            assert(!gate.acquire(a, b));
        }
        assert(!detached.load(std::memory_order_acquire));
        letWriterProceed.store(true, std::memory_order_release);
        ui.join();
        assert(detached);
        audio_lifecycle::Callback a, b;
        assert(gate.acquire(a, b));
        assert(a == nullptr && b == nullptr);
        gate.release();
    }
    std::atomic<bool> run{true};
    std::thread audio([&] {
        int16_t out[128];
        while (run.load(std::memory_order_acquire)) {
            audio_lifecycle::Callback a, b;
            if (!gate.acquire(a, b)) continue;
            if (a) a(out, 128);
            if (b) b(out, 128);
            gate.release();
        }
    });
    for (int i = 0; i < 20000; ++i) {
        state = new int(i % 32000);
        gate.setMain(quick, yield);
        gate.setMain(nullptr, yield);
        delete state; state = nullptr;
    }
    run.store(false, std::memory_order_release); audio.join();
    diagnostics::TimingStats stats;
    for (int i = 1; i <= 100; ++i) stats.record(i, 1, 24000);
    auto s = stats.snapshot();
    assert(s.count == 100 && s.maxUs == 100 && s.overDeadline == 59);
    assert(s.percentileUpper(50) == 64 && s.percentileUpper(99) == 128);
    printf("PASS lifetime: main+overlay in-flight detach and 200000 pending-writer admission attempts; 20000 allocate/render/detach cycles; %u rendered blocks; timing quantiles/deadlines\n", rendered.load());
}
