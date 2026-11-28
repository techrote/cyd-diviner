#pragma once
#include <stdint.h>
#if defined(GRIM_PERF_I2S_EVENTS) && !defined(GRIM_PERF)
#error GRIM_PERF_I2S_EVENTS requires the opt-in profiler
#endif
#ifdef GRIM_PERF_I2S_EVENTS
// Portable aggregation only; HAL owns the RTOS queue and timestamps.
namespace perf_i2s_events {
constexpr uint32_t QueueCapacity = 32;
struct Drain {
    uint32_t events = 0, txUnderflow = 0, descriptorFault = 0, txDone = 0, other = 0;
    uint32_t pendingBefore = 0, pendingAfter = 0, durationUs = 0, intervalUs = 0;
};
struct Counters {
    uint32_t drains = 0, events = 0, txUnderflow = 0, descriptorFault = 0, txDone = 0, other = 0;
    uint32_t pendingMax = 0, queueFullObservations = 0, drainLimitHits = 0;
    uint32_t maxDrainUs = 0, maxDrainIntervalUs = 0, firstDrainOffsetUs = 0, lastDrainOffsetUs = 0;

    // The caller holds the run/reset mutex. Events have no ISR timestamp:
    // all observations belong to the window containing drain completion.
    bool add(const Drain &d, uint64_t drainedAtUs, bool collecting, uint64_t beginUs, uint64_t endUs) {
        if (!collecting || drainedAtUs < beginUs || drainedAtUs >= endUs) return false;
        const uint32_t offset = static_cast<uint32_t>(drainedAtUs - beginUs);
        if (!drains) firstDrainOffsetUs = offset;
        lastDrainOffsetUs = offset;
        ++drains; events += d.events; txUnderflow += d.txUnderflow;
        descriptorFault += d.descriptorFault; txDone += d.txDone; other += d.other;
        if (d.pendingBefore > pendingMax) pendingMax = d.pendingBefore;
        if (d.pendingAfter > pendingMax) pendingMax = d.pendingAfter;
        if (d.pendingBefore >= QueueCapacity || d.pendingAfter >= QueueCapacity) ++queueFullObservations;
        if (d.events >= QueueCapacity) ++drainLimitHits;
        if (d.durationUs > maxDrainUs) maxDrainUs = d.durationUs;
        if (d.intervalUs > maxDrainIntervalUs) maxDrainIntervalUs = d.intervalUs;
        return true;
    }
};
}

#endif
#ifdef GRIM_PERF
namespace perf_profile {
enum Metric { FrameInterval, FrameUpdate, FrameRaster, FramePresent, TouchPoll, TouchDispatch,
    AudioPeriod, AudioLock, AudioCallback, AudioOverlay, AudioMix, AudioEnqueue, PcmWrite,
    AudioWriteError, AudioShortWrite, AudioLatePeriod, PcmDrop, PcmQueue, FrameMiss, Count };
void begin();
void poll();
bool command(const char *line);
void observe(Metric metric, uint32_t value);
void frame(uint32_t duration, bool interleaved);
void audioTask(void *handle);
void audioBlock();
#ifdef GRIM_PERF_I2S_EVENTS
void i2sEventQueueAvailable(bool available);
void i2sEventDrain(const perf_i2s_events::Drain &drain, uint64_t drainedAtUs);
#endif
bool quiet();
bool running();
uint32_t now();
uint32_t seed();
void sceneSettings(const char *scene, const char *fields);
}
#else
namespace perf_profile {
inline void begin() {}
inline void poll() {}
inline bool command(const char *) { return false; }
inline bool quiet() { return true; }
inline bool running() { return false; }
}
#endif
