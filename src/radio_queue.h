// Fixed-capacity receive ring. The platform must serialize push/pop/reset
// (ESP-NOW uses one short portMUX critical section). No allocation or wait.
#pragma once
#include <stddef.h>
namespace radio_queue {
template<class T, size_t Capacity> struct Ring {
    static_assert(Capacity > 0, "ring needs capacity");
    T slots[Capacity]{};
    size_t head = 0, tail = 0, count = 0, highWater = 0;
    bool push(const T &value) {
        if (count == Capacity) return false;
        slots[head] = value; head = (head + 1) % Capacity; ++count;
        if (count > highWater) highWater = count;
        return true;
    }
    bool pop(T &value) {
        if (!count) return false;
        value = slots[tail]; tail = (tail + 1) % Capacity; --count; return true;
    }
    void clear() { head = tail = count = highWater = 0; }
};
} // namespace radio_queue
