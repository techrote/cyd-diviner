// Native preview script pacing and pointer edges. MIT; Diviner contributors.
#pragma once
#include <cstdint>

namespace native_script {

// Input timestamps describe planned elapsed time. If one action is late, keep
// the following inter-action intervals instead of replaying an overdue burst.
// The caller consumes at most one action per poll; unsigned elapsed arithmetic
// also handles a normal SDL millisecond-counter rollover.
class Schedule {
    uint32_t wall_ = 0;
    uint32_t planned_ = 0;
public:
    void reset(uint32_t now) noexcept { wall_ = now; planned_ = 0; }
    bool dispatch(uint32_t at, uint32_t now) noexcept {
        if (now - wall_ < at - planned_) return false;
        wall_ = now;
        planned_ = at;
        return true;
    }
};

// A script explicitly knows whether its previous contact ended. Before the
// next down edge, repeat that known up state so the real controller can finish
// release debounce even if rendering prevented any intervening input poll.
// Continued held contacts receive no extra up edge.
class TouchEdges {
    bool held_ = false;
public:
    bool held() const noexcept { return held_; }
    template<class Emit> void set(bool down, int x, int y, Emit emit) {
        if (down && !held_) emit(false, x, y);
        emit(down, x, y);
        held_ = down;
    }
    template<class Emit> void tap(int x, int y, Emit emit) {
        set(true, x, y, emit);
        set(false, x, y, emit);
    }
};

} // namespace native_script
