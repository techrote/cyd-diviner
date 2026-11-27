#pragma once
#include <cstdint>
namespace lgfx {
constexpr uint16_t getSwap16(uint32_t value) { return uint16_t((value << 8) | (value >> 8)); }
struct rgb565_t { uint16_t raw; };
struct swap565_t {
    uint16_t raw = 0;
    uint16_t get() const { return raw; }
    swap565_t &operator=(rgb565_t value) { raw = getSwap16(value.raw); return *this; }
};
}
