// Host replay and numeric oracle; its timings MUST NOT be labelled target results.
// g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I src src/perf_kernels.cpp tools/perf/kernels_host.cpp -o kernels_host
#include "perf_kernels.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <initializer_list>

int main(int argc, char **argv) {
    using namespace perf_kernels;
    auto first = std::make_unique<Workspace>();
    auto second = std::make_unique<Workspace>();
    bool valid = true;
    constexpr uint32_t Seed = 343709u;
    constexpr unsigned Steps = 100;
    std::printf("{\"schema\":1,\"type\":\"host_kernel_validation\",\"evidence_class\":\"host_replay\",\"seed\":%u,\"workspace_bytes\":%zu,\"cases\":[", Seed, sizeof(Workspace));
    for (unsigned size = 0; size < 2; ++size) {
        for (unsigned i = 0; i < static_cast<unsigned>(Id::Count); ++i) {
            const Id id = static_cast<Id>(i);
            prepare(*first, id, size != 0, Seed); prepare(*second, id, size != 0, Seed);
            const auto start = std::chrono::steady_clock::now();
            for (unsigned j = 0; j < Steps; ++j) step(*first);
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
            for (unsigned j = 0; j < Steps; ++j) step(*second);
            const Result output = inspect(*first), repeated = inspect(*second);
            const bool okay = output.finite && output.output_bytes && output.checksum == repeated.checksum && output.working_bytes <= sizeof(Workspace);
            valid &= okay;
            std::printf("%s{\"name\":\"%s\",\"large\":%u,\"steps\":%u,\"checksum_fnv1a\":%u,\"output_bytes\":%u,\"working_bytes\":%u,\"output_min\":%.9g,\"output_max\":%.9g,\"output_sum\":%.12g,\"host_elapsed_us\":%lld,\"valid\":%s}",
                size || i ? "," : "", descriptor(id).name, size, Steps, output.checksum, output.output_bytes, output.working_bytes,
                output.output_min, output.output_max, output.output_sum, static_cast<long long>(elapsed), okay ? "true" : "false");
        }
    }
    std::printf("],\"numeric_comparisons\":[");
    bool first_comparison = true;
    for (unsigned size = 0; size < 2; ++size) {
        for (Id id : {Id::OscFloat, Id::FilterFloat, Id::ModalFloat}) {
            const auto comparison = compare(*first, *second, id, size != 0, Seed);
            const double tolerance = id == Id::ModalFloat ? 8. : 3.;
            const bool okay = comparison.finite && comparison.samples > 0 && comparison.max_abs_lsb <= tolerance;
            valid &= okay;
            std::printf("%s{\"float_kernel\":\"%s\",\"large\":%u,\"samples\":%u,\"max_abs_lsb\":%.9g,\"rms_lsb\":%.9g,\"tolerance_lsb\":%.1f,\"valid\":%s}",
                first_comparison ? "" : ",", descriptor(id).name, size, comparison.samples, comparison.max_abs_lsb, comparison.rms_lsb, tolerance, okay ? "true" : "false");
            first_comparison = false;
        }
    }
    std::printf("],\"fft_oracle\":[");
    for (unsigned size = 0; size < 2; ++size) {
        const auto comparison = validateFft(*first, size != 0, Seed);
        const bool okay = comparison.finite && comparison.max_abs_error < .001 && comparison.relative_energy_error < 1.e-5;
        valid &= okay;
        std::printf("%s{\"points\":%u,\"max_abs_error\":%.12g,\"relative_energy_error\":%.12g,\"oracle\":\"direct double-precision DFT plus Parseval\",\"valid\":%s}",
            size ? "," : "", comparison.points, comparison.max_abs_error, comparison.relative_energy_error, okay ? "true" : "false");
    }
    // Reject unrecognized workload names instead of silently benchmarking another.
    valid &= find("missing") == nullptr && !prepare(*first, Id::Count, false, Seed);
    std::printf("],\"valid\":%s}\n", valid ? "true" : "false");
    if (argc > 1 && std::strcmp(argv[1], "--selftest") != 0) return 2;
    return valid ? 0 : 1;
}
