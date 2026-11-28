// Native counter semantics test, not a substitute for an ESP32 hardware capture.
#define GRIM_PERF 1
#define GRIM_PERF_I2S_EVENTS 1
#include "../../src/perf_profile.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace perf_i2s_events;
    Counters c;
    Drain d;
    d.events = 4; d.txUnderflow = 1; d.descriptorFault = 1; d.txDone = 2;
    d.pendingBefore = 4; d.durationUs = 12; d.intervalUs = 5333;
    assert(!c.add(d, 99, true, 100, 200));
    assert(!c.add(d, 100, false, 100, 200));
    assert(c.drains == 0 && c.events == 0);
    assert(c.add(d, 100, true, 100, 200)); // Inclusive steady start.
    assert(c.txUnderflow == 1 && c.descriptorFault == 1 && c.txDone == 2);
    assert(c.firstDrainOffsetUs == 0 && c.lastDrainOffsetUs == 0);
    assert(!c.add(d, 200, true, 100, 200)); // Exclusive fixed end.
    assert(c.drains == 1 && c.events == 4);

    Drain full;
    full.events = QueueCapacity; full.txDone = QueueCapacity;
    full.pendingBefore = QueueCapacity; full.pendingAfter = 8;
    full.durationUs = 35; full.intervalUs = 100000;
    assert(c.add(full, 150, true, 100, 200));
    assert(c.pendingMax == QueueCapacity && c.queueFullObservations == 1);
    assert(c.drainLimitHits == 1 && c.maxDrainUs == 35 && c.maxDrainIntervalUs == 100000);
    assert(c.lastDrainOffsetUs == 50 && c.firstDrainOffsetUs == 0);
    assert(c.txUnderflow == 1); // Saturation is not invented as extra underflows.

    // A run reset rejects an audio-core drain captured before the new window.
    c = {};
    assert(!c.add(d, 150, true, 300, 400));
    assert(c.drains == 0 && c.txUnderflow == 0 && c.descriptorFault == 0);
    Drain empty;
    empty.intervalUs = 5000;
    assert(c.add(empty, 310, true, 300, 400));
    assert(c.drains == 1 && c.events == 0 && c.txUnderflow == 0);
    assert(c.firstDrainOffsetUs == 10); // Observed empty drain is distinct from NOT RUN.
    assert(c.add(d, 399, true, 300, 400));
    assert(c.drains == 2 && c.events == 4 && c.txUnderflow == 1);
    assert(c.lastDrainOffsetUs == 99);
    std::puts("PASS: I2S drain-window boundaries, reset isolation, event categories and saturation observations");
}
