#pragma once
#include <stdint.h>
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
