// Portable scalar reference workloads. No display, RTOS, radio or DSP library dependency.
#pragma once
#include <cstddef>
#include <cstdint>

namespace perf_kernels {
enum class Id : uint8_t {
    OscFloat, OscFixed, FilterFloat, FilterFixed, ModalFloat, ModalFixed,
    Fft, Cellular, Particles, Feedback, Stripe, Count
};
struct Descriptor {
    Id id;
    const char *name;
    const char *algorithm;
    const char *precision;
    const char *work_unit;
};
// Optional allocation: 40,016 bytes, reused by all workloads. Never put this on
// an ESP32 task stack. prepare() initializes only the selected context.
struct Workspace {
    alignas(16) uint8_t storage[40000];
    Id id;
    bool large;
    uint32_t seed, steps;
};
struct Result {
    uint32_t checksum;
    uint32_t output_bytes;
    uint32_t working_bytes; // selected context, excluding this common workspace's spare capacity
    uint32_t work_units; // per step, in Descriptor::work_unit, not estimated CPU operations
    double output_min, output_max, output_sum;
    bool finite;
};
struct Comparison {
    double max_abs_lsb, rms_lsb;
    uint32_t samples;
    bool finite;
};
struct FftComparison {
    double max_abs_error, relative_energy_error;
    uint32_t points;
    bool finite;
};
const Descriptor &descriptor(Id id);
const Descriptor *find(const char *name);
bool prepare(Workspace &workspace, Id id, bool large, uint32_t seed);
void step(Workspace &workspace); // measured region: initialization and output inspection excluded
Result inspect(const Workspace &workspace);
// Reset both paths, process one block, compare quantized Q15 outputs. This uses
// caller-owned workspaces; its timings are not benchmark results.
Comparison compare(Workspace &floating, Workspace &fixed, Id floating_id,
                   bool large, uint32_t seed);
// Slow independent double-precision DFT oracle, outside all timing windows.
FftComparison validateFft(Workspace &workspace, bool large, uint32_t seed);

#if defined(GRIM_PERF) || defined(GRIM_PERF_KERNEL_STANDALONE)
// UART is caller-owned. Invoke only after stopping PCM and finishing writes.
// perf kernel list | perf kernel NAME SEED ITERATIONS LARGE(0|1) [internal|psram]
// Default workspace allocation is explicitly internal RAM; no silent fallback.
// Emits one PERF JSON receipt; 3 warmups then 1..256 exact timed samples.
bool command(const char *line);
#endif
} // namespace perf_kernels
