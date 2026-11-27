#pragma once
#ifdef GRIM_PERF
namespace perf_radio {
bool set(const char *mode);
const char *mode();
}
#endif
