#pragma once
#ifdef GRIM_PERF
namespace perf_radio {
bool set(const char *mode);
const char *mode();
void report(); // Checked API results and actual controller state, never credentials.
}
#endif
