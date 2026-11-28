// Independent rational-time oracle for the portable schedule, not RF timing.
#include "main/fast_schedule.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

using Wide = unsigned __int128;
static uint64_t checks, cases, symbols, ringChecks;
static bool boundaryExtraObserved;
static void require(bool condition) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "fast schedule oracle failed at check %llu\n", (unsigned long long)checks); std::abort(); }
}
static uint64_t floorClock(uint64_t symbol, uint32_t cpu, uint32_t baud) {
    return uint64_t(Wide(symbol) * cpu / baud);
}
static void oneCase(uint32_t cpu, uint32_t baud, uint32_t length) {
    ++cases;
    constexpr uint32_t Guard = 0x91d6a3efu;
    std::vector<uint32_t> allocation(size_t(length) + 2, Guard);
    auto *delta = allocation.data() + 1;
    metal_fast_schedule_t schedule{};
    require(metal_fast_schedule_build(&schedule, cpu, baud, length, delta));
    require(allocation.front() == Guard && allocation.back() == Guard);
    require(schedule.cpu_hz == cpu && schedule.baud == baud && schedule.length == length);
    uint64_t sum = 0;
    for (uint32_t n = 0; n < length; ++n) {
        uint64_t expected = floorClock(uint64_t(n) + 1, cpu, baud) - floorClock(n, cpu, baud);
        require(delta[n] == expected && delta[n] > 0);
        sum += delta[n];
    }
    require(sum == schedule.ring_cycles_floor && sum == floorClock(length, cpu, baud));
    require(schedule.ring_remainder == uint32_t(Wide(length) * cpu % baud));
    require(schedule.min_delta_cycles == *std::min_element(delta, delta + length));
    require(schedule.max_delta_cycles == *std::max_element(delta, delta + length));

    uint32_t index = 0, phase = 0;
    uint64_t deadline = 0, count = std::min<uint64_t>(uint64_t(length) * 5 + 3, 4096);
    for (uint64_t n = 0; n < count; ++n) {
        ++symbols;
        const uint64_t exactFloor = floorClock(n, cpu, baud);
        require(deadline <= exactFloor && exactFloor - deadline <= 1);
        require(Wide(n) * cpu - Wide(deadline) * baud < Wide(2) * baud);
        if (!index) require(deadline == exactFloor);
        // Source location follows transmitted word count, never inferred from
        // elapsed time. This checks ring wrap at every scheduled word, even
        // where a modelled interrupt has crossed several ideal deadlines.
        require(index == n % length);
        const uint64_t interval = delta[index];
        deadline += interval;
        if (++index == length) {
            index = 0;
            const uint32_t carry = metal_fast_ring_carry(&phase, schedule.ring_remainder, baud);
            require(carry <= 1 && phase < baud);
            if (interval + carry > schedule.max_delta_cycles) boundaryExtraObserved = true;
            deadline += carry;
            require(deadline == floorClock(n + 1, cpu, baud));
        }
    }
    // Exercise carry arithmetic at distant ring numbers without iterating to
    // them. An independent 128-bit product avoids repeating uint32 arithmetic.
    const uint64_t rings[] = {0, 1, 2, 17, 65537, 0xffffffffULL, 0xffffffffffffULL};
    for (uint64_t ring : rings) {
        ++ringChecks;
        uint32_t before = uint32_t(Wide(ring) * schedule.ring_remainder % baud);
        uint32_t actualPhase = before;
        uint32_t carry = metal_fast_ring_carry(&actualPhase, schedule.ring_remainder, baud);
        Wide expected = Wide(before) + schedule.ring_remainder;
        require(carry == uint32_t(expected / baud));
        require(actualPhase == uint32_t(expected % baud));
    }
}

static uint32_t randomWord(uint32_t &state) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}

static void finiteSourceOrderModel(void) {
    // Deliberately independent execution model: ordered symbols become ready
    // at rational absolute deadlines, while an interrupt/cost model delays
    // service. No elapsed-time index or skip-ahead is permitted. The target
    // loop still needs its own implementation test and physical measurement.
    uint32_t state = 0x517ed003u;
    for (unsigned trial = 0; trial < 400; ++trial) {
        uint32_t length = 1 + randomWord(state) % 97, baud = 1 + randomWord(state) % 240000000;
        std::vector<uint32_t> delta(length), source(length);
        for (uint32_t n = 0; n < length; ++n) source[n] = 0x13579bdfu ^ (n * 0x9e3779b9u);
        metal_fast_schedule_t schedule{};
        require(metal_fast_schedule_build(&schedule, 240000000, baud, length, delta.data()));
        std::vector<uint32_t> emitted;
        uint64_t now = 0, deadline = 0;
        uint32_t index = 0, phase = 0;
        constexpr uint64_t end = 100000;
        while (emitted.size() < 10000 && now < end) {
            now = std::max(now, deadline);
            // Fixed seeded CPU cost and intermittent IRQ delays; this is a
            // software fault model, never an achieved target throughput claim.
            if ((randomWord(state) & 31) == 0) now += 1000;
            if (now >= end) break;
            emitted.push_back(source[index]);
            deadline += delta[index];
            if (++index == length) { index = 0; deadline += metal_fast_ring_carry(&phase, schedule.ring_remainder, baud); }
            now += 4 + randomWord(state) % 19;
        }
        for (size_t n = 0; n < emitted.size(); ++n) require(emitted[n] == source[n % source.size()]);
        require(index == emitted.size() % source.size());
        require(deadline <= floorClock(emitted.size(), 240000000, baud));
    }
}

int main() {
    metal_fast_schedule_t output{};
    uint32_t memory[3] = {11, 22, 33};
    require(!metal_fast_schedule_build(nullptr, 240000000, 1000, 1, memory));
    require(!metal_fast_schedule_build(&output, 240000000, 1000, 1, nullptr));
    require(!metal_fast_schedule_build(&output, 0, 1, 1, memory));
    require(!metal_fast_schedule_build(&output, 1, 0, 1, memory));
    require(!metal_fast_schedule_build(&output, 1, 2, 1, memory));
    require(!metal_fast_schedule_build(&output, 1, 1, 0, memory));
    require(memory[0] == 11 && memory[1] == 22 && memory[2] == 33 && !output.length);
    for (uint32_t cpu = 1; cpu <= 64; ++cpu)
        for (uint32_t baud = 1; baud <= cpu; ++baud)
            for (uint32_t length = 1; length <= 19; ++length) oneCase(cpu, baud, length);
    const uint32_t clocks[] = {240000000u, 0x80000000u, 0xfffffffeu, 0xffffffffu};
    const uint32_t lengths[] = {1, 2, 3, 7, 31, 32, 240, 368, 7680, 11776, 16384, 65537};
    for (uint32_t cpu : clocks) {
        const uint32_t rates[] = {1, 2, 3, 11500, 23000, 276000, 360000, 1000000, 12000000, cpu - 1, cpu};
        for (uint32_t baud : rates) for (uint32_t length : lengths) oneCase(cpu, baud, length);
    }
    uint32_t state = 0xa1739b5fu;
    for (unsigned n = 0; n < 1000; ++n) {
        uint32_t cpu = randomWord(state) | 1u;
        uint32_t baud = 1 + uint64_t(randomWord(state)) % cpu;
        oneCase(cpu, baud, 1 + randomWord(state) % 257);
    }
    finiteSourceOrderModel();
    require(boundaryExtraObserved); // Table max alone is not boundary-gap max.
    std::printf("{\"schema\":\"sdr-metal-fast-schedule-oracle/1\",\"evidence_class\":\"HOST_MATH_ORACLE_AND_SOURCE_ORDER_MODEL\",\"valid\":true,\"RF_measurement\":false,\"checks\":%llu,\"schedule_cases\":%llu,\"symbol_deadlines_checked\":%llu,\"distant_ring_carries_checked\":%llu,\"finite_stall_source_order_model_cases\":400,\"boundary_extra_cycle_observed\":true}\n",
        (unsigned long long)checks, (unsigned long long)cases, (unsigned long long)symbols, (unsigned long long)ringChecks);
}
