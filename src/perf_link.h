#pragma once
#include <stdint.h>
#ifdef GRIM_PERF_LINK
namespace perf_link {
void begin(); // No radio is started; every network action requires a command.
void poll();
bool command(const char *line);
void onRun(const char *runId, uint64_t steadyBeginUs, uint64_t endUs);
bool active();
}
#endif
