#pragma once
#ifdef GRIM_PERF_AUDIO_MVP
namespace perf_audio_mvp {
void begin();
void poll();
// Dispatch before other radio commands so a running/initialized MVP has one
// radio owner. The module is opt-in and starts no radio from begin().
bool command(const char *line);
bool ownsRadio();
}
#endif
