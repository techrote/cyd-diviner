// Native-CYD settings persistence. Explicit path only; no radio credentials.
#pragma once
#ifdef GRIM_NATIVE
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace cyd_settings_file {
using Record = std::array<uint8_t, 32>;
inline uint32_t checksum(const Record &r) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < 28; ++i) h = (h ^ r[i]) * 16777619u;
    return h;
}
inline bool valid(const Record &r) {
    if (std::memcmp(r.data(), "DUI1", 4)) return false;
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= uint32_t(r[28 + i]) << (8 * i);
    return checksum(r) == stored;
}
inline bool read(Record &r) {
    const char *path = std::getenv("GRIM_SETTINGS");
    if (!path || !*path) return false;
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    const bool complete = std::fread(r.data(), 1, r.size(), f) == r.size();
    const bool exact = std::fgetc(f) == EOF && !std::ferror(f);
    std::fclose(f);
    return complete && exact && valid(r);
}
inline bool write(Record r) {
    const char *path = std::getenv("GRIM_SETTINGS");
    if (!path || !*path) return true; // opt-in persistence; otherwise session only
    std::memcpy(r.data(), "DUI1", 4);
    const uint32_t h = checksum(r);
    for (unsigned i = 0; i < 4; ++i) r[28 + i] = uint8_t(h >> (8 * i));
    const std::string temporary = std::string(path) + ".tmp";
    FILE *f = std::fopen(temporary.c_str(), "wb");
    if (!f) return false;
    const bool complete = std::fwrite(r.data(), 1, r.size(), f) == r.size();
    const bool flushed = std::fflush(f) == 0;
    const bool closed = std::fclose(f) == 0;
    if (!complete || !flushed || !closed || std::rename(temporary.c_str(), path)) {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}
} // namespace cyd_settings_file
#endif
