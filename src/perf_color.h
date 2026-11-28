#pragma once
#ifdef GRIM_PERF_COLOR
#include <LovyanGFX.hpp>
#include <stddef.h>
#include <stdint.h>

// Opt-in packed intermediates around the existing RGB565 rasterizer. These
// surfaces are one stripe tall; they are neither full frame nor temporal history.
namespace perf_color {
constexpr unsigned BandRows = 8;
bool enabled();
bool configure(const char *name, unsigned width, unsigned height, size_t backingBytes,
               bool layered, const char *base, const char *overlay);
void report();
bool beginFrame(uint8_t *canvas, size_t backingBytes, unsigned width, unsigned height,
                bool layered, const char *base, const char *overlay);
void baseRoundtrip(lgfx::swap565_t *pixels, unsigned rows);
void compositeRoundtrip(lgfx::swap565_t *pixels, unsigned rows);
void endFrame();
void onRun(const char *runId, uint64_t steadyBeginUs, uint64_t endUs);
void exportRun();
}
#endif
