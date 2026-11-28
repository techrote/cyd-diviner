// Pure radio-to-musical mappings. RSSI describes received signal level, not
// calibrated distance or physical position. Identifiers stay transient.
#pragma once
#include <stdint.h>
namespace radio_music {
inline uint32_t identityHash(const char *s) {
    uint32_t h = 2166136261u;
    if (s) while (*s) h = (h ^ uint8_t(*s++)) * 16777619u;
    return h;
}
inline float level(int rssi) {
    float t = (rssi + 95) / 60.f;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return 0.04f + t * t * 0.96f;
}
inline int note(uint32_t hash, int root, const int8_t *steps, unsigned count) {
    return count && steps ? root + steps[hash % count] + 12 * ((hash >> 9) % 3) : root;
}
inline bool fresh(uint32_t now, uint32_t seen, uint32_t lifetime) {
    return uint32_t(now - seen) < lifetime;
}
} // namespace radio_music
