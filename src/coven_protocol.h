// Diviner COVEN CV1 protocol. Portable, allocation-free UI-thread state.
// CV1 is a musical pulse protocol, NOT an authenticated or sample clock.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace coven {
constexpr uint32_t BeatMs = 1800;
constexpr uint32_t GoneMs = 15000;
constexpr uint32_t CycleMs = BeatMs * 256;
constexpr size_t WireSize = 10;
constexpr size_t MaxPeers = 10;

struct Packet { uint8_t kind = 0, root = 9, scale = 0, degree = 4, beat = 0; uint16_t phase = 0; };
inline bool decode(const void *data, size_t n, Packet &p) {
    if (!data || n != WireSize) return false;
    const auto *b = static_cast<const uint8_t *>(data);
    if (memcmp(b, "CV1", 3) || b[3] > 1 || b[4] > 11 || b[5] > 3 || b[6] > 12) return false;
    uint16_t phase = uint16_t(b[8]) | (uint16_t(b[9]) << 8);
    if (phase >= BeatMs) return false;
    p = {b[3], b[4], b[5], b[6], b[7], phase};
    return true;
}
inline void encode(const Packet &p, uint8_t (&b)[WireSize]) {
    b[0] = 'C'; b[1] = 'V'; b[2] = '1'; b[3] = p.kind;
    b[4] = p.root; b[5] = p.scale; b[6] = p.degree; b[7] = p.beat;
    b[8] = uint8_t(p.phase); b[9] = uint8_t(p.phase >> 8);
}
inline int compareMac(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6); }
inline bool validMac(const uint8_t *m) {
    if (!m || (m[0] & 1)) return false; // multicast/broadcast is not a member
    uint8_t any = 0; for (int i = 0; i < 6; ++i) any |= m[i];
    return any != 0;
}
inline int32_t delta(uint32_t a, uint32_t b) {
    int32_t d = int32_t(a) - int32_t(b);
    if (d > int32_t(CycleMs / 2)) d -= CycleMs;
    if (d < -int32_t(CycleMs / 2)) d += CycleMs;
    return d;
}
inline uint32_t wrap(int32_t c) {
    c %= int32_t(CycleMs);
    return uint32_t(c < 0 ? c + CycleMs : c);
}
struct Peer {
    bool used = false, haveCall = false;
    uint8_t mac[6] = {};
    Packet packet;
    uint32_t seen = 0, cursor = 0, callCursor = 0, callAt = 0;
};
enum class Receive { Invalid, Self, Replay, RateLimited, Full, Presence, Call };
struct Counters {
    uint32_t accepted = 0, invalid = 0, replay = 0, rateLimited = 0, full = 0;
    uint32_t leaderChanges = 0, synchronisations = 0;
};

struct Session {
    uint8_t me[6] = {}, leader[6] = {};
    Peer peers[MaxPeers];
    uint8_t root = 9, scale = 0, degree = 4, beat = 0;
    uint32_t beatAt = 0;
    int leaderSlot = -1;
    int32_t lastErrorMs = 0;
    Counters counters;

    void begin(const uint8_t mac[6], uint32_t now) {
        *this = Session{}; memcpy(me, mac, 6); memcpy(leader, mac, 6); beatAt = now;
    }
    bool isLeader() const { return leaderSlot < 0; }
    size_t members() const { size_t n = 1; for (const auto &p : peers) n += p.used; return n; }
    uint32_t cursor(uint32_t now) const { return (uint32_t(beat) * BeatMs + uint32_t(now - beatAt)) % CycleMs; }
    bool advance(uint32_t now) {
        uint32_t n = uint32_t(now - beatAt) / BeatMs;
        if (!n) return false;
        beat = uint8_t(uint32_t(beat) + n); beatAt += n * BeatMs; return true;
    }
    void align(const Peer &p, uint32_t now, bool hard) {
        uint32_t target = (p.cursor + uint32_t(now - p.seen)) % CycleMs;
        uint32_t current = cursor(now);
        lastErrorMs = delta(target, current);
        // Hard acquisition/large discontinuity; otherwise bounded slew rejects
        // one packet's arrival jitter. Unknown one-way latency remains a bias.
        int32_t correction = lastErrorMs;
        if (!hard && correction >= -200 && correction <= 200) {
            correction /= 4;
            if (correction > 12) correction = 12;
            if (correction < -12) correction = -12;
        }
        uint32_t corrected = wrap(int32_t(current) + correction);
        beat = uint8_t(corrected / BeatMs); beatAt = now - corrected % BeatMs;
        root = p.packet.root; scale = p.packet.scale; ++counters.synchronisations;
    }
    bool elect(uint32_t now) {
        int chosen = -1;
        const uint8_t *best = me;
        for (size_t i = 0; i < MaxPeers; ++i) {
            auto &p = peers[i];
            if (p.used && uint32_t(now - p.seen) >= GoneMs) p.used = false;
            if (p.used && compareMac(p.mac, best) < 0) { best = p.mac; chosen = int(i); }
        }
        bool changed = compareMac(best, leader) != 0;
        leaderSlot = chosen;
        if (changed) {
            memcpy(leader, best, 6); ++counters.leaderChanges;
            if (chosen >= 0) align(peers[chosen], now, true);
        }
        return changed;
    }
    bool tick(uint32_t now) { bool b = advance(now); elect(now); return b; }
    Receive receive(const uint8_t source[6], const void *data, size_t n, uint32_t now, uint32_t ageMs = 0) {
        tick(now);
        Packet packet;
        if (!validMac(source) || !decode(data, n, packet) || ageMs > 2000) { ++counters.invalid; return Receive::Invalid; }
        if (!compareMac(source, me)) return Receive::Self;
        Peer *p = nullptr, *free = nullptr;
        for (auto &x : peers) {
            if (x.used && !compareMac(x.mac, source)) p = &x;
            if (!x.used && !free) free = &x;
        }
        if (!p && !free) { ++counters.full; return Receive::Full; }
        uint32_t remote = uint32_t(packet.beat) * BeatMs + packet.phase;
        if (p) {
            int32_t progress = delta(remote, p->cursor);
            bool newCall = packet.kind == 1 && (!p->haveCall || remote != p->callCursor);
            // A call can share its timestamp with a presence, once. Replayed
            // packets do not refresh liveness, preventing an old loop from
            // indefinitely retaining a dead member. CV1 has no boot nonce.
            if (progress < 0 || (progress == 0 && !newCall)) { ++counters.replay; return Receive::Replay; }
            if (packet.kind == 1 && p->haveCall && uint32_t(now - p->callAt) < 700) {
                ++counters.rateLimited; return Receive::RateLimited;
            }
            if (packet.kind == 0 && uint32_t(now - p->seen) < 80) {
                ++counters.rateLimited; return Receive::RateLimited;
            }
        } else {
            p = free; *p = Peer{}; p->used = true; memcpy(p->mac, source, 6);
        }
        p->packet = packet; p->cursor = remote; p->seen = now - ageMs;
        if (packet.kind == 1) { p->haveCall = true; p->callCursor = remote; p->callAt = now; }
        ++counters.accepted;
        bool changed = elect(now);
        if (!changed && leaderSlot >= 0 && &peers[leaderSlot] == p) align(*p, now, false);
        return packet.kind == 1 ? Receive::Call : Receive::Presence;
    }
    void frame(uint8_t (&out)[WireSize], uint8_t kind, uint32_t now) {
        tick(now);
        Packet p{uint8_t(kind != 0), root, scale, degree, beat, uint16_t(now - beatAt)};
        encode(p, out);
    }
};

// DYAD text is transient local performance input. Bound it and strip control
// bytes; never pass raw radio payloads to printf or retain them in diagnostics.
inline size_t dyadText(const uint8_t *bytes, size_t n, char *out, size_t capacity) {
    if (!out || !capacity) return 0;
    out[0] = 0;
    if (!bytes || n < 5 || n > 250 || memcmp(bytes, "DY1|", 4)) return 0;
    size_t at = 0; unsigned bars = 0;
    for (size_t i = 0; i < n; ++i) if (bytes[i] == '|' && ++bars == 10) { at = i + 1; break; }
    if (!at) return 0;
    size_t w = 0;
    for (size_t i = at; i < n && w + 1 < capacity; ++i) {
        uint8_t c = bytes[i];
        if (c >= 32 && c <= 126 && c != '|') out[w++] = char(c);
        else if ((c == '\t' || c == '\n' || c == '\r') && w && out[w - 1] != ' ') out[w++] = ' ';
    }
    out[w] = 0; return w;
}
} // namespace coven
