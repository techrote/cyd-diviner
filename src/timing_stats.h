// Fixed-memory, allocation-free telemetry. MIT; Diviner contributors.
#pragma once
#include <atomic>
#include <cstdint>

namespace diagnostics {
// Bin upper bounds are powers of two microseconds. Percentiles are upper
// bounds, never advertised as exact samples. Only one thread records into an
// instance; readers may take a concurrent approximate snapshot.
struct TimingSnapshot {
    uint32_t count = 0, maxUs = 0, overDeadline = 0;
    uint32_t bins[32]{};
    uint32_t percentileUpper(unsigned percent) const {
        if (!count) return 0;
        const uint64_t rank = (uint64_t(count) * percent + 99) / 100;
        uint64_t seen = 0;
        for (unsigned i = 0; i < 32; ++i) {
            seen += bins[i];
            if (seen >= rank) return i == 31 ? UINT32_MAX : uint32_t(1) << i;
        }
        return maxUs; // a concurrently published sample was not in the bins
    }
};
class TimingStats {
    std::atomic<uint32_t> count_{0}, max_{0}, late_{0};
    std::atomic<uint32_t> bins_[32]{};
public:
    void record(uint32_t us, uint32_t samples = 0, uint32_t rate = 24000) {
        unsigned b = 0;
        uint32_t upper = 1;
        while (us > upper && b < 31) { ++b; upper = b == 31 ? UINT32_MAX : upper << 1; }
        bins_[b].fetch_add(1, std::memory_order_relaxed);
        if (us > max_.load(std::memory_order_relaxed)) max_.store(us, std::memory_order_relaxed);
        if (samples && uint64_t(us) * rate > uint64_t(samples) * 1000000)
            late_.fetch_add(1, std::memory_order_relaxed);
        count_.fetch_add(1, std::memory_order_relaxed);
    }
    TimingSnapshot snapshot() const {
        TimingSnapshot s;
        for (unsigned b = 0; b < 32; ++b) s.bins[b] = bins_[b].load(std::memory_order_relaxed);
        // Derive count from the same sampled bins, to keep percentile ranks
        // defined even when the writer is between bin and count publication.
        for (uint32_t n : s.bins) s.count += n;
        s.maxUs = max_.load(std::memory_order_relaxed);
        s.overDeadline = late_.load(std::memory_order_relaxed);
        return s;
    }
};
} // namespace diagnostics
