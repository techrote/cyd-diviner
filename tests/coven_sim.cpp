// Deterministic qualification of the exact controller and receive ring used
// in firmware. Synthetic clocks/packets only; no hardware latency claim.
#include "coven_protocol.h"
#include "radio_queue.h"
#include "radio_sonification.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
using Mac = std::array<uint8_t, 6>;
static Mac mac(unsigned n) { return {0x02, 0x44, 0x49, 0x56, uint8_t(n >> 8), uint8_t(n)}; }
static std::array<uint8_t, coven::WireSize> packet(uint8_t beat = 0, uint16_t phase = 0, uint8_t kind = 0, uint8_t root = 9) {
    uint8_t bytes[coven::WireSize]; coven::encode({kind, root, 0, 4, beat, phase}, bytes);
    std::array<uint8_t, coven::WireSize> r{}; std::copy(bytes, bytes + sizeof bytes, r.begin()); return r;
}
static void unitTests() {
    coven::Packet p;
    auto b = packet(255, 1799, 1, 11); CHECK(coven::decode(b.data(), b.size(), p));
    CHECK(p.beat == 255 && p.phase == 1799 && p.kind == 1 && p.root == 11);
    for (size_t n = 0; n < b.size(); ++n) CHECK(!coven::decode(b.data(), n, p));
    CHECK(!coven::decode(nullptr, 10, p)); CHECK(!coven::decode(b.data(), 11, p));
    for (int k = 0; k <= 6; ++k) { auto bad = b; bad[k] = 255; CHECK(!coven::decode(bad.data(), bad.size(), p)); }
    auto bad = packet(0, 1800); CHECK(!coven::decode(bad.data(), bad.size(), p));
    coven::Session s; auto mine = mac(3), elder = mac(1), middle = mac(2);
    s.begin(mine.data(), 1000); b = packet(7, 500, 0, 3);
    CHECK(s.receive(elder.data(), b.data(), b.size(), 2000) == coven::Receive::Presence);
    CHECK(!s.isLeader() && s.root == 3 && s.beat == 7);
    auto other = packet(99, 700, 0, 8); auto before = s.cursor(2100);
    CHECK(s.receive(middle.data(), other.data(), other.size(), 2100) == coven::Receive::Presence);
    CHECK(s.root == 3 && s.cursor(2100) == before); // one elected timing source
    CHECK(s.receive(elder.data(), b.data(), b.size(), 2200) == coven::Receive::Replay);
    b = packet(7, 499); CHECK(s.receive(elder.data(), b.data(), b.size(), 2300) == coven::Receive::Replay);
    b = packet(7, 900); CHECK(s.receive(elder.data(), b.data(), b.size(), 2400) == coven::Receive::Presence);
    b[3] = 1; CHECK(s.receive(elder.data(), b.data(), b.size(), 2400) == coven::Receive::Call);
    CHECK(s.receive(elder.data(), b.data(), b.size(), 2410) == coven::Receive::Replay);
    b = packet(7, 1000, 1); CHECK(s.receive(elder.data(), b.data(), b.size(), 2500) == coven::Receive::RateLimited);
    b = packet(8, 0, 1); CHECK(s.receive(elder.data(), b.data(), b.size(), 3300) == coven::Receive::Call);
    auto zero = Mac{}; CHECK(s.receive(zero.data(), b.data(), b.size(), 3301) == coven::Receive::Invalid);
    auto multi = mac(8); multi[0] |= 1; CHECK(s.receive(multi.data(), b.data(), b.size(), 3302) == coven::Receive::Invalid);
    CHECK(s.receive(elder.data(), b.data(), b.size(), 3303, 2001) == coven::Receive::Invalid);
    // Duplicates do not keep a dead member alive.
    s.tick(18300); CHECK(s.isLeader()); CHECK(s.members() == 1);
    // A second live elder replaces the expired first and retains its root.
    s.begin(mine.data(), 0); b = packet(0, 0, 0, 2); s.receive(elder.data(), b.data(), b.size(), 0);
    b = packet(5, 1000, 0, 6); s.receive(middle.data(), b.data(), b.size(), 10000);
    s.tick(15000); CHECK(!s.isLeader()); CHECK(!memcmp(s.leader, middle.data(), 6)); CHECK(s.root == 6);
    // Beat counter and millisecond clock wrap independently.
    uint32_t start = 0xFFFFFF00u; s.begin(mine.data(), start); s.beat = 255;
    CHECK(s.tick(start + 1800u)); CHECK(s.beat == 0 && s.beatAt == start + 1800u);
    b = packet(0, 80); s.receive(elder.data(), b.data(), b.size(), start + 1880u);
    CHECK(!s.isLeader()); s.tick(start + 1880u + coven::GoneMs); CHECK(s.isLeader());
    CHECK(coven::delta(30, coven::CycleMs - 20) == 50);
    // Independent elapsed-time oracle, spanning both wire beat wrap and the
    // uint32 monotonic clock wrap; do not use Session::cursor to predict it.
    s.begin(mine.data(), start);
    for (uint32_t elapsed = 0; elapsed < 1000000; elapsed += 137) {
        s.tick(start + elapsed);
        CHECK(s.beat == uint8_t((elapsed / 1800) % 256));
        CHECK(uint32_t(start + elapsed - s.beatAt) == elapsed % 1800);
    }
    // Capacity is explicit: no active members are evicted by new traffic.
    s.begin(mac(100).data(), 0); b = packet();
    for (unsigned i = 1; i <= coven::MaxPeers; ++i) CHECK(s.receive(mac(i).data(), b.data(), b.size(), 0) == coven::Receive::Presence);
    CHECK(s.receive(mac(11).data(), b.data(), b.size(), 0) == coven::Receive::Full); CHECK(s.members() == 11);
    char out[12]; const char text[] = "DY1|a|b|c|d|e|f|g|h|i|hi\nthere\033[31m";
    CHECK(coven::dyadText(reinterpret_cast<const uint8_t *>(text), sizeof(text) - 1, out, sizeof out) == 11);
    CHECK(!strcmp(out, "hi there[31"));
    CHECK(coven::dyadText(reinterpret_cast<const uint8_t *>("DY1|short"), 9, out, sizeof out) == 0);
    // Same bounded ring as the Wi-Fi callback, including wrap/drop behaviour.
    radio_queue::Ring<int, 15> q;
    for (int i = 0; i < 15; ++i) { CHECK(q.push(i)); }
    CHECK(!q.push(99)); CHECK(q.count == 15 && q.highWater == 15);
    for (int i = 0, value; i < 15; ++i) { CHECK(q.pop(value)); CHECK(value == i); }
    int value; CHECK(!q.pop(value)); CHECK(q.push(100)); CHECK(q.pop(value) && value == 100);
    q.clear(); CHECK(q.count == 0 && q.highWater == 0);
    int8_t steps[] = {0, 2, 3, 5, 7, 9, 10};
    for (int r = -128; r <= 126; ++r) CHECK(radio_music::level(r) <= radio_music::level(r + 1));
    CHECK(radio_music::level(-128) >= 0 && radio_music::level(127) <= 1);
    CHECK(radio_music::identityHash("synthetic") == radio_music::identityHash("synthetic"));
    for (unsigned i = 0; i < 10000; ++i) { int note = radio_music::note(i, 45, steps, 7); CHECK(note >= 45 && note <= 79); }
    CHECK(radio_music::fresh(10, 0xfffffff0, 30)); CHECK(!radio_music::fresh(10, 0xfffffff0, 20));
}
struct Options {
    const char *name; unsigned nodes; int duration = 60000, delayMax = 0, dropPercent = 0, driftPpm = 0;
    bool partition = false, replacement = false, flood = false;
};
struct Delivery { uint32_t due; unsigned from, to; std::array<uint8_t, coven::WireSize> bytes; bool junk = false; };
struct Result {
    std::string name; unsigned nodes, observations = 0, transmitted = 0, lost = 0, delivered = 0, duplicates = 0;
    unsigned unrelated = 0, queueDrops = 0, highWater = 0, leaderFailures = 0;
    double median = 0, p95 = 0, maximum = 0;
    uint32_t recoveryMs = 0;
};
static Result simulate(Options o) {
    Result r; r.name = o.name; r.nodes = o.nodes;
    std::vector<coven::Session> nodes(o.nodes);
    std::vector<radio_queue::Ring<Delivery, 15>> inbound(o.nodes);
    std::vector<Delivery> wire;
    std::vector<int> errors;
    uint32_t random = 0xD171C0DEu;
    auto rng = [&]() { random = random * 1664525u + 1013904223u; return random; };
    auto local = [&](unsigned i, uint32_t t) {
        int64_t drift = (int64_t(i) * 2 - int64_t(o.nodes - 1)) * o.driftPpm / int64_t(o.nodes > 1 ? o.nodes - 1 : 1);
        return uint32_t(0xffff0000u + i * 113 + int64_t(t) + int64_t(t) * drift / 1000000);
    };
    auto active = [&](unsigned i, uint32_t t) {
        if (!o.replacement) return true;
        if (i == 0 && t >= 20000 && t < 60000) return false;
        if (i == o.nodes - 1 && t < 10000) return false;
        return true;
    };
    auto linked = [&](unsigned a, unsigned b, uint32_t t) {
        return !o.partition || t < 20000 || t >= 50000 || (a < o.nodes / 2) == (b < o.nodes / 2);
    };
    for (unsigned i = 0; i < o.nodes; ++i) nodes[i].begin(mac(i + 1).data(), local(i, 0));
    for (uint32_t t = 0; t <= uint32_t(o.duration); t += 10) {
        if (o.replacement && t == 10000) nodes.back().begin(mac(o.nodes).data(), local(o.nodes - 1, t));
        if (o.replacement && t == 60000) nodes[0].begin(mac(1).data(), local(0, t));
        for (unsigned i = 0; i < o.nodes; ++i) {
            if (!active(i, t)) continue;
            nodes[i].tick(local(i, t));
            if (t % 1000 == (i * 70) % 1000) {
                uint8_t frame[coven::WireSize]; nodes[i].frame(frame, 0, local(i, t));
                for (unsigned j = 0; j < o.nodes; ++j) {
                    if (i == j || !active(j, t) || !linked(i, j, t)) continue;
                    ++r.transmitted;
                    if (rng() % 100 < unsigned(o.dropPercent)) { ++r.lost; continue; }
                    Delivery d{t + (o.delayMax ? 1 + rng() % unsigned(o.delayMax) : 0), i, j, {}};
                    std::copy(frame, frame + sizeof frame, d.bytes.begin()); wire.push_back(d);
                    if (o.delayMax && r.transmitted % 9 == 0) { d.due += 1300; wire.push_back(d); ++r.duplicates; }
                }
            }
        }
        // Flood bursts saturate the shared queue but do not alter valid state.
        // They are deliberately bursty (80 frames/s), not guaranteed anti-DoS.
        if (o.flood && t % 250 == 0) for (unsigned j = 0; j < o.nodes; ++j) {
            for (unsigned k = 0; k < 20; ++k) {
                Delivery d{t, 99, j, {}, true}; ++r.unrelated;
                if (!inbound[j].push(d)) ++r.queueDrops;
            }
        }
        for (size_t k = 0; k < wire.size();) {
            Delivery d = wire[k];
            if (d.due > t) { ++k; continue; }
            wire[k] = wire.back(); wire.pop_back();
            if (!active(d.to, t) || !linked(d.from, d.to, t)) continue;
            if (!inbound[d.to].push(d)) ++r.queueDrops;
        }
        for (unsigned j = 0; j < o.nodes; ++j) {
            r.highWater = std::max(r.highWater, unsigned(inbound[j].highWater));
            Delivery d;
            for (int budget = 0; budget < 8 && inbound[j].pop(d); ++budget) {
                nodes[j].receive(mac(d.from + 1).data(), d.bytes.data(), d.bytes.size(), local(j, t)); ++r.delivered;
            }
        }
        // Steady windows begin after the documented 15-second membership lease
        // plus three beacons. Fresh acquisition is measured separately below.
        bool steady = t >= 18000;
        if (o.partition && ((t >= 20000 && t < 38000) || (t >= 50000 && t < 68000))) steady = false;
        if (o.replacement && ((t >= 20000 && t < 38000) || (t >= 60000 && t < 78000))) steady = false;
        if (steady && t % 100 == 0) {
            for (unsigned j = 0; j < o.nodes; ++j) {
                if (!active(j, t)) continue;
                unsigned leader = 0;
                while (leader < o.nodes && (!active(leader, t) || !linked(leader, j, t))) ++leader;
                if (leader == o.nodes || leader == j) continue;
                if (memcmp(nodes[j].leader, nodes[leader].me, 6)) { ++r.leaderFailures; continue; }
                int error = std::abs(coven::delta(nodes[j].cursor(local(j, t)), nodes[leader].cursor(local(leader, t))));
                errors.push_back(error);
            }
        }
        if (o.partition && t >= 50000 && !r.recoveryMs) {
            bool recovered = true;
            for (unsigned j = 1; j < o.nodes; ++j) recovered &= !memcmp(nodes[j].leader, nodes[0].me, 6);
            if (recovered) r.recoveryMs = t - 50000;
        }
        if (o.replacement && t >= 20000 && t < 60000 && !r.recoveryMs) {
            bool recovered = true;
            for (unsigned j = 2; j < o.nodes; ++j) recovered &= !memcmp(nodes[j].leader, nodes[1].me, 6);
            if (recovered) r.recoveryMs = t - 20000;
        }
    }
    std::sort(errors.begin(), errors.end()); r.observations = unsigned(errors.size());
    CHECK(!errors.empty());
    r.median = errors[errors.size() / 2]; r.p95 = errors[(errors.size() - 1) * 95 / 100]; r.maximum = errors.back();
    CHECK(r.leaderFailures == 0);
    // Synthetic bound: one-way delay <=80 ms, 10 ms scheduler, bounded slew.
    // This is a model regression gate, not an ESP32 timing claim.
    CHECK(r.maximum <= (o.delayMax ? 150 : 5));
    if (o.partition) CHECK(r.recoveryMs > 0 && r.recoveryMs <= 18000);
    if (o.replacement) CHECK(r.recoveryMs >= 14000 && r.recoveryMs <= 18000);
    if (o.flood) CHECK(r.queueDrops > 0 && r.highWater == 15);
    return r;
}
int main(int argc, char **argv) {
    unitTests();
    std::vector<Result> results;
    results.push_back(simulate({"two_nodes_clean", 2}));
    results.push_back(simulate({"three_nodes_clean", 3}));
    results.push_back(simulate({"eleven_nodes_loss_drift_reorder", 11, 100000, 80, 20, 300}));
    results.push_back(simulate({"partition_reunion", 6, 90000, 80, 15, 200, true}));
    results.push_back(simulate({"leader_replacement_join_reentry", 3, 100000, 60, 10, 250, false, true}));
    results.push_back(simulate({"unrelated_traffic_queue_saturation", 3, 60000, 80, 10, 100, false, false, true}));
    std::string json = "{\n  \"status\": \"PASS\", \"kind\": \"deterministic_native_model\",\n  \"checks\": " + std::to_string(checks) + ", \"session_bytes\": " + std::to_string(sizeof(coven::Session)) + ",\n  \"scenarios\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto &r = results[i]; char row[1100];
        std::snprintf(row, sizeof row,
            "    {\"name\":\"%s\",\"nodes\":%u,\"observations\":%u,\"median_ms\":%.1f,\"p95_ms\":%.1f,\"max_ms\":%.1f,\"leader_failures\":%u,\"recovery_ms\":%u,\"transmitted\":%u,\"model_lost\":%u,\"delivered\":%u,\"duplicate_reordered\":%u,\"unrelated\":%u,\"queue_drops\":%u,\"queue_high_water\":%u}%s\n",
            r.name.c_str(), r.nodes, r.observations, r.median, r.p95, r.maximum, r.leaderFailures, r.recoveryMs,
            r.transmitted, r.lost, r.delivered, r.duplicates, r.unrelated, r.queueDrops, r.highWater, i + 1 == results.size() ? "" : ",");
        json += row;
    }
    json += "  ]\n}\n";
    std::printf("%s", json.c_str());
    if (argc > 1) { std::ofstream f(argv[1]); f << json; CHECK(bool(f)); }
}
