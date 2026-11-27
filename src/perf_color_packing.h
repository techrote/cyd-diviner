// Portable packed color surfaces for the opt-in profiling experiment only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace perf_color_packing {
enum class Format : uint8_t { Rgb111, Rgb444, Rgba5551 };
// Native words: R2/G1/B0; R11..8/G7..4/B3..0;
// R15..11/G10..6/B5..1/A0. Quantization drops low component bits;
// expansion replicates high bits. Alpha is binary coverage, not blending.
constexpr unsigned bitsPerPixel(Format format) {
    return format == Format::Rgb111 ? 3u : format == Format::Rgb444 ? 12u :
           format == Format::Rgba5551 ? 16u : 0u;
}
// Pixel x starts at bit x*bpp; native bit zero is stored first (LSB-first).
// Rows are independent. Zero means empty/invalid or an unrepresentable stride.
constexpr size_t rowBytes(Format format, size_t width) {
    const size_t bits = bitsPerPixel(format), groups = width / 8;
    if (!bits || groups > std::numeric_limits<size_t>::max() / bits) return 0;
    const size_t whole = groups * bits, tail = ((width % 8) * bits + 7) / 8;
    return whole > std::numeric_limits<size_t>::max() - tail ? 0 : whole + tail;
}
constexpr uint16_t encode565(Format format, uint16_t rgb, bool isOpaque = true) {
    if (format == Format::Rgb111)
        return uint16_t(((rgb >> 13) & 4) | ((rgb >> 9) & 2) | ((rgb >> 4) & 1));
    if (format == Format::Rgb444)
        return uint16_t(((rgb >> 4) & 0xf00) | ((rgb >> 3) & 0x0f0) | ((rgb >> 1) & 0x00f));
    if (format == Format::Rgba5551)
        return uint16_t((rgb & 0xffc0) | ((rgb & 31) << 1) | unsigned(isOpaque));
    return 0;
}
constexpr uint16_t expand565(Format format, uint16_t native) {
    if (format == Format::Rgb111)
        return uint16_t((native & 4 ? 0xf800 : 0) | (native & 2 ? 0x07e0 : 0) | (native & 1 ? 31 : 0));
    if (format == Format::Rgb444) {
        const unsigned r = (native >> 8) & 15, g = (native >> 4) & 15, b = native & 15;
        return uint16_t(((r * 2 + r / 8) << 11) | ((g * 4 + g / 4) << 5) | (b * 2 + b / 8));
    }
    if (format == Format::Rgba5551)
        return uint16_t((native & 0xffc0) | ((native >> 5) & 32) | ((native >> 1) & 31));
    return 0;
}
constexpr bool opaque(Format format, uint16_t native) {
    return format == Format::Rgba5551 ? (native & 1) != 0 : bitsPerPixel(format) != 0;
}
// Caller supplies an allocated row and x < its width. Only bytes intersecting
// this pixel are accessed; neighboring pixels and unused row-tail bits survive.
inline uint16_t read(Format format, const uint8_t *row, size_t x) {
    const unsigned bits = bitsPerPixel(format);
    if (!bits) return 0;
    const size_t bit = x * bits, byte = bit / 8;
    const unsigned shift = unsigned(bit % 8), count = (shift + bits + 7) / 8;
    uint32_t word = 0;
    for (unsigned i = 0; i < count; ++i) word |= uint32_t(row[byte + i]) << (8 * i);
    return uint16_t((word >> shift) & ((1u << bits) - 1));
}
inline void write(Format format, uint8_t *row, size_t x, uint16_t native) {
    const unsigned bits = bitsPerPixel(format);
    if (!bits) return;
    const size_t bit = x * bits, byte = bit / 8;
    const unsigned shift = unsigned(bit % 8), count = (shift + bits + 7) / 8;
    const uint32_t mask = ((1u << bits) - 1) << shift;
    uint32_t word = 0;
    for (unsigned i = 0; i < count; ++i) word |= uint32_t(row[byte + i]) << (8 * i);
    word = (word & ~mask) | ((uint32_t(native) << shift) & mask);
    for (unsigned i = 0; i < count; ++i) row[byte + i] = uint8_t(word >> (8 * i));
}
constexpr size_t coverageRowBytes(size_t width) { return width / 8 + size_t(width % 8 != 0); }
inline bool readCoverage(const uint8_t *row, size_t x) { return (row[x / 8] & (1u << (x % 8))) != 0; }
inline void writeCoverage(uint8_t *row, size_t x, bool covered) {
    const uint8_t mask = uint8_t(1u << (x % 8));
    row[x / 8] = uint8_t((row[x / 8] & uint8_t(~mask)) | (covered ? mask : 0));
}
} // namespace perf_color_packing
