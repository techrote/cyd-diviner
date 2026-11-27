#include "perf_kernels.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace perf_kernels {
namespace {
constexpr float Pi = 3.14159265358979323846f;
constexpr Descriptor Descriptors[] = {
    {Id::OscFloat, "osc-f32", "1024-entry sine LUT, linear interpolation, wrapping u32 phase", "float32 arithmetic, Q15 output", "samples"},
    {Id::OscFixed, "osc-q15", "1024-entry sine LUT, linear interpolation, wrapping u32 phase", "Q15 table, Q22 fraction, int64 product", "samples"},
    {Id::FilterFloat, "filter-f32", "one-pole lowpass y += 0.13*(x-y), deterministic white input", "float32 state, Q15 output", "samples"},
    {Id::FilterFixed, "filter-q15", "one-pole lowpass y += 4260/32768*(x-y), deterministic white input", "Q15 input, Q30 state and coefficient product", "samples"},
    {Id::ModalFloat, "modal-f32", "parallel damped second-order resonators, impulse per block", "float32 state and coefficients, Q15 output", "voice_samples"},
    {Id::ModalFixed, "modal-q15", "parallel damped second-order resonators, impulse per block", "Q30 state/coefficients, int64 products, Q15 output", "voice_samples"},
    {Id::Fft, "fft-f32", "in-place radix-2 DIT complex FFT, bit reversal, precomputed twiddles", "float32 complex, unnormalized forward", "complex_points"},
    {Id::Cellular, "cellular", "toroidal Conway Life B3/S23, ping-pong bytes", "uint8 cells", "cells"},
    {Id::Particles, "particles", "position/velocity integration, gravity, bounce, no raster", "Q16.16 state, int32", "particles"},
    {Id::Feedback, "feedback", "indexed nearest-neighbor wrapped warp, saturating decay", "uint8 indexed pixels, integer displacement", "pixels"},
    {Id::Stripe, "stripe-rgb565", "RGB565 moving gradient strip generation, no SPI transfer", "uint16 RGB565", "pixels"},
};
static_assert(sizeof(Descriptors) / sizeof(Descriptors[0]) == static_cast<size_t>(Id::Count));
uint32_t random32(uint32_t &state) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}
uint32_t seedState(uint32_t seed) { return seed ? seed : 0x6d2b79f5u; }
int16_t q15(float value) {
    return static_cast<int16_t>(std::lround(std::max(-1.f, std::min(32767.f / 32768.f, value)) * 32768.f));
}
int16_t q30ToQ15(int32_t value) {
    // Explicit division avoids implementation-dependent right shifts of negatives.
    const int32_t rounded = static_cast<int32_t>(value >= 0 ? (int64_t(value) + 16384) / 32768 : -((-int64_t(value) + 16384) / 32768));
    return static_cast<int16_t>(std::max<int32_t>(-32768, std::min<int32_t>(32767, rounded)));
}
struct Dsp {
    float table[1025];
    int16_t fixed_table[1025];
    int16_t input[512], output[512];
    float a[32], b[32], y1[32], y2[32];
    int32_t qa[32], qb[32], qy1[32], qy2[32];
    float filter;
    int32_t qfilter;
    uint32_t phase, increment, length, voices;
};
struct Fft {
    float real[1024], imag[1024], twiddle_real[512], twiddle_imag[512];
    uint32_t length;
};
struct Grid {
    uint8_t pixels[2][128 * 96];
    uint32_t width, height, front;
};
struct Particle { int32_t x, y, vx, vy; };
struct Particles { Particle particles[512]; uint32_t count; };
struct Feedback {
    uint8_t pixels[2][160 * 120];
    int8_t dx[160], dy[120];
    uint32_t width, height, front, phase;
};
struct Stripe { uint16_t pixels[320 * 16]; uint32_t width, rows, phase; };
template<class T> T &context(Workspace &w) { return *reinterpret_cast<T *>(w.storage); }
template<class T> const T &context(const Workspace &w) { return *reinterpret_cast<const T *>(w.storage); }
template<class T> T &construct(Workspace &w) {
    static_assert(sizeof(T) <= sizeof(w.storage), "kernel workspace exhausted");
    return *new(w.storage) T{};
}
bool isDsp(Id id) { return static_cast<unsigned>(id) <= static_cast<unsigned>(Id::ModalFixed); }
void oscillator(Dsp &d, bool fixed) {
    for (uint32_t i = 0; i < d.length; ++i) {
        const uint32_t index = d.phase >> 22;
        const uint32_t fraction = d.phase & ((1u << 22) - 1u);
        if (fixed) {
            const int64_t delta = int32_t(d.fixed_table[index + 1]) - d.fixed_table[index];
            d.output[i] = static_cast<int16_t>(d.fixed_table[index] + delta * fraction / (1u << 22));
        } else {
            const float fraction_float = static_cast<float>(fraction) / static_cast<float>(1u << 22);
            d.output[i] = q15(d.table[index] + (d.table[index + 1] - d.table[index]) * fraction_float);
        }
        d.phase += d.increment;
    }
}
void filter(Dsp &d, bool fixed) {
    for (uint32_t i = 0; i < d.length; ++i) {
        if (fixed) {
            const int32_t input = int32_t(d.input[i]) * 32768;
            d.qfilter += static_cast<int32_t>((int64_t(input) - d.qfilter) * 4260 / 32768);
            d.output[i] = q30ToQ15(d.qfilter);
        } else {
            d.filter += .13f * (static_cast<float>(d.input[i]) / 32768.f - d.filter);
            d.output[i] = q15(d.filter);
        }
    }
}
void modal(Dsp &d, bool fixed) {
    // Keep the steady workload audible/nonzero after many repetitions. Fixed
    // and floating paths receive equivalent quantized impulses once per block.
    for (uint32_t v = 0; v < d.voices; ++v) {
        if (fixed) d.qy1[v] += 4294967; // round(0.004 * 2^30)
        else d.y1[v] += .004f;
    }
    for (uint32_t i = 0; i < d.length; ++i) {
        if (fixed) {
            int64_t total = 0;
            for (uint32_t v = 0; v < d.voices; ++v) {
                const int32_t value = static_cast<int32_t>((int64_t(d.qa[v]) * d.qy1[v] - int64_t(d.qb[v]) * d.qy2[v]) / (1ll << 30));
                d.qy2[v] = d.qy1[v]; d.qy1[v] = value; total += value;
            }
            d.output[i] = q30ToQ15(static_cast<int32_t>(total / d.voices));
        } else {
            float total = 0.f;
            for (uint32_t v = 0; v < d.voices; ++v) {
                const float value = d.a[v] * d.y1[v] - d.b[v] * d.y2[v];
                d.y2[v] = d.y1[v]; d.y1[v] = value; total += value;
            }
            d.output[i] = q15(total / static_cast<float>(d.voices));
        }
    }
}
void fft(Fft &f, uint32_t seed, uint32_t iteration) {
    // Refresh deterministic input every step. Input construction is part of this
    // workload (same on every target); twiddle generation is excluded.
    uint32_t state = seedState(seed + iteration);
    for (uint32_t i = 0; i < f.length; ++i) {
        f.real[i] = static_cast<float>(int32_t(random32(state) & 65535u) - 32768) / 32768.f;
        f.imag[i] = 0.f;
    }
    for (uint32_t i = 1, j = 0; i < f.length; ++i) {
        uint32_t bit = f.length >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(f.real[i], f.real[j]); std::swap(f.imag[i], f.imag[j]); }
    }
    for (uint32_t span = 2; span <= f.length; span <<= 1) {
        const uint32_t half = span / 2, stride = f.length / span;
        for (uint32_t base = 0; base < f.length; base += span) {
            for (uint32_t j = 0; j < half; ++j) {
                const uint32_t a = base + j, b = a + half, t = j * stride;
                const float re = f.twiddle_real[t] * f.real[b] - f.twiddle_imag[t] * f.imag[b];
                const float im = f.twiddle_real[t] * f.imag[b] + f.twiddle_imag[t] * f.real[b];
                f.real[b] = f.real[a] - re; f.imag[b] = f.imag[a] - im;
                f.real[a] += re; f.imag[a] += im;
            }
        }
    }
}
void cellular(Grid &g) {
    const uint8_t *source = g.pixels[g.front];
    uint8_t *dest = g.pixels[g.front ^ 1u];
    for (uint32_t y = 0; y < g.height; ++y) {
        const uint32_t above = (y ? y - 1 : g.height - 1) * g.width;
        const uint32_t row = y * g.width, below = (y + 1 == g.height ? 0 : y + 1) * g.width;
        for (uint32_t x = 0; x < g.width; ++x) {
            const uint32_t left = x ? x - 1 : g.width - 1, right = x + 1 == g.width ? 0 : x + 1;
            const unsigned sum = source[above + left] + source[above + x] + source[above + right] +
                source[row + left] + source[row + right] + source[below + left] + source[below + x] + source[below + right];
            dest[row + x] = (sum == 3 || (sum == 2 && source[row + x])) ? 1 : 0;
        }
    }
    g.front ^= 1u;
}
void particles(Particles &p) {
    constexpr int32_t MaxX = 319 * 65536, MaxY = 239 * 65536;
    for (uint32_t i = 0; i < p.count; ++i) {
        auto &v = p.particles[i];
        v.vy += 655; v.x += v.vx; v.y += v.vy;
        if (v.x < 0) { v.x = -v.x; v.vx = -v.vx; }
        if (v.x > MaxX) { v.x = 2 * MaxX - v.x; v.vx = -v.vx; }
        if (v.y < 0) { v.y = -v.y; v.vy = -v.vy; }
        if (v.y > MaxY) { v.y = 2 * MaxY - v.y; v.vy = -v.vy * 7 / 8; }
    }
}
void feedback(Feedback &f) {
    const uint8_t *source = f.pixels[f.front];
    uint8_t *dest = f.pixels[f.front ^ 1u];
    ++f.phase;
    for (uint32_t y = 0; y < f.height; ++y) {
        for (uint32_t x = 0; x < f.width; ++x) {
            const uint32_t sx = (int32_t(x) + f.dx[(x + f.phase) % f.width] + f.width) % f.width;
            const uint32_t sy = (int32_t(y) + f.dy[(y + f.phase) % f.height] + f.height) % f.height;
            const uint8_t value = source[sy * f.width + sx];
            dest[y * f.width + x] = value ? value - 1 : 0;
        }
    }
    // Continuous deterministic excitation prevents a trivial all-zero result.
    dest[(f.phase * 53u) % (f.width * f.height)] = 255;
    f.front ^= 1u;
}
void stripe(Stripe &s) {
    ++s.phase;
    for (uint32_t y = 0; y < s.rows; ++y) {
        for (uint32_t x = 0; x < s.width; ++x) {
            const uint32_t r = (x + s.phase) & 31, g = (y * 3 + s.phase) & 63, b = (x + y + s.phase) & 31;
            s.pixels[y * s.width + x] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
        }
    }
}
uint32_t hash(const uint8_t *bytes, size_t count) {
    uint32_t result = 2166136261u;
    for (size_t i = 0; i < count; ++i) result = (result ^ bytes[i]) * 16777619u;
    return result;
}
template<class T> void range(Result &result, const T *data, size_t count) {
    result.output_min = count ? static_cast<double>(data[0]) : 0;
    result.output_max = result.output_min;
    result.output_sum = 0;
    result.finite = true;
    for (size_t i = 0; i < count; ++i) {
        const double value = static_cast<double>(data[i]);
        if (!std::isfinite(value)) result.finite = false;
        result.output_min = std::min(result.output_min, value);
        result.output_max = std::max(result.output_max, value);
        result.output_sum += value;
    }
    result.checksum = hash(reinterpret_cast<const uint8_t *>(data), count * sizeof(T));
    result.output_bytes = static_cast<uint32_t>(count * sizeof(T));
}
} // namespace

const Descriptor &descriptor(Id id) {
    const unsigned index = static_cast<unsigned>(id);
    return Descriptors[index < static_cast<unsigned>(Id::Count) ? index : 0];
}
const Descriptor *find(const char *name) {
    for (const auto &d : Descriptors) if (std::strcmp(name, d.name) == 0) return &d;
    return nullptr;
}
bool prepare(Workspace &w, Id id, bool large, uint32_t seed) {
    if (static_cast<unsigned>(id) >= static_cast<unsigned>(Id::Count)) return false;
    w.id = id; w.large = large; w.seed = seed; w.steps = 0;
    uint32_t state = seedState(seed);
    if (isDsp(id)) {
        auto &d = construct<Dsp>(w);
        d.length = large ? 512 : 128; d.voices = large ? 32 : 8;
        d.increment = static_cast<uint32_t>(440.0 / 24000.0 * 4294967296.0);
        for (unsigned i = 0; i <= 1024; ++i) {
            d.table[i] = std::sin(2.f * Pi * static_cast<float>(i) / 1024.f) * .8f;
            d.fixed_table[i] = q15(d.table[i]);
        }
        for (uint32_t i = 0; i < d.length; ++i) d.input[i] = static_cast<int16_t>(random32(state) & 65535u);
        for (uint32_t v = 0; v < d.voices; ++v) {
            const float frequency = 110.f + static_cast<float>(v) * 37.f;
            const float damping = .999f - static_cast<float>(v) * .00001f;
            d.a[v] = 2.f * damping * std::cos(2.f * Pi * frequency / 24000.f);
            d.b[v] = damping * damping;
            d.y1[v] = 0.f; d.y2[v] = 0.f;
            d.qa[v] = static_cast<int32_t>(std::llround(static_cast<double>(d.a[v]) * (1ll << 30)));
            d.qb[v] = static_cast<int32_t>(std::llround(static_cast<double>(d.b[v]) * (1ll << 30)));
            d.qy1[v] = static_cast<int32_t>(std::llround(static_cast<double>(d.y1[v]) * (1ll << 30)));
        }
    } else if (id == Id::Fft) {
        auto &f = construct<Fft>(w); f.length = large ? 1024 : 256;
        for (uint32_t i = 0; i < f.length / 2; ++i) {
            f.twiddle_real[i] = std::cos(-2.f * Pi * static_cast<float>(i) / static_cast<float>(f.length));
            f.twiddle_imag[i] = std::sin(-2.f * Pi * static_cast<float>(i) / static_cast<float>(f.length));
        }
    } else if (id == Id::Cellular) {
        auto &g = construct<Grid>(w); g.width = large ? 128 : 64; g.height = large ? 96 : 48;
        for (uint32_t i = 0; i < g.width * g.height; ++i) g.pixels[0][i] = (random32(state) % 4 == 0);
    } else if (id == Id::Particles) {
        auto &p = construct<Particles>(w); p.count = large ? 512 : 128;
        for (uint32_t i = 0; i < p.count; ++i) {
            auto &v = p.particles[i]; v.x = random32(state) % (319 * 65536); v.y = random32(state) % (239 * 65536);
            v.vx = (int32_t(random32(state) & 65535u) - 32768) * 4;
            v.vy = (int32_t(random32(state) & 65535u) - 32768) * 4;
        }
    } else if (id == Id::Feedback) {
        auto &f = construct<Feedback>(w); f.width = large ? 160 : 80; f.height = large ? 120 : 60;
        for (uint32_t i = 0; i < f.width * f.height; ++i) f.pixels[0][i] = random32(state) & 255;
        for (uint32_t x = 0; x < f.width; ++x) f.dx[x] = static_cast<int8_t>(int32_t(random32(state) % 7) - 3);
        for (uint32_t y = 0; y < f.height; ++y) f.dy[y] = static_cast<int8_t>(int32_t(random32(state) % 7) - 3);
    } else if (id == Id::Stripe) {
        auto &s = construct<Stripe>(w); s.width = 320; s.rows = large ? 16 : 4;
    }
    return true;
}
void step(Workspace &w) {
    switch (w.id) {
    case Id::OscFloat: oscillator(context<Dsp>(w), false); break;
    case Id::OscFixed: oscillator(context<Dsp>(w), true); break;
    case Id::FilterFloat: filter(context<Dsp>(w), false); break;
    case Id::FilterFixed: filter(context<Dsp>(w), true); break;
    case Id::ModalFloat: modal(context<Dsp>(w), false); break;
    case Id::ModalFixed: modal(context<Dsp>(w), true); break;
    case Id::Fft: fft(context<Fft>(w), w.seed, w.steps); break;
    case Id::Cellular: cellular(context<Grid>(w)); break;
    case Id::Particles: particles(context<Particles>(w)); break;
    case Id::Feedback: feedback(context<Feedback>(w)); break;
    case Id::Stripe: stripe(context<Stripe>(w)); break;
    case Id::Count: return;
    }
    ++w.steps;
}
Result inspect(const Workspace &w) {
    Result r{};
    if (isDsp(w.id)) {
        const auto &d = context<Dsp>(w); range(r, d.output, d.length); r.working_bytes = sizeof(Dsp);
        r.work_units = d.length * ((w.id == Id::ModalFloat || w.id == Id::ModalFixed) ? d.voices : 1);
    } else if (w.id == Id::Fft) {
        const auto &f = context<Fft>(w); range(r, f.real, f.length);
        Result imaginary{}; range(imaginary, f.imag, f.length);
        r.checksum ^= imaginary.checksum; r.output_bytes *= 2; r.finite &= imaginary.finite;
        r.output_min = std::min(r.output_min, imaginary.output_min); r.output_max = std::max(r.output_max, imaginary.output_max);
        r.output_sum += imaginary.output_sum; r.working_bytes = sizeof(Fft); r.work_units = f.length;
    } else if (w.id == Id::Cellular) {
        const auto &g = context<Grid>(w); range(r, g.pixels[g.front], g.width * g.height);
        r.working_bytes = sizeof(Grid); r.work_units = g.width * g.height;
    } else if (w.id == Id::Particles) {
        const auto &p = context<Particles>(w);
        // Explicit fields guarantee checksum independent of struct padding.
        r.finite = true; r.output_min = 0; r.output_max = 0; r.checksum = 2166136261u;
        for (uint32_t i = 0; i < p.count; ++i) {
            const auto &v = p.particles[i];
            const int32_t values[] = {v.x, v.y, v.vx, v.vy};
            for (int32_t value : values) {
                uint32_t bits = static_cast<uint32_t>(value);
                for (unsigned byte = 0; byte < 4; ++byte) { r.checksum = (r.checksum ^ (bits & 255)) * 16777619u; bits >>= 8; }
                r.output_min = std::min(r.output_min, double(value)); r.output_max = std::max(r.output_max, double(value)); r.output_sum += value;
            }
        }
        r.output_bytes = p.count * 16; r.working_bytes = sizeof(Particles); r.work_units = p.count;
    } else if (w.id == Id::Feedback) {
        const auto &f = context<Feedback>(w); range(r, f.pixels[f.front], f.width * f.height);
        r.working_bytes = sizeof(Feedback); r.work_units = f.width * f.height;
    } else if (w.id == Id::Stripe) {
        const auto &s = context<Stripe>(w); range(r, s.pixels, s.width * s.rows);
        r.working_bytes = sizeof(Stripe); r.work_units = s.width * s.rows;
    }
    return r;
}
Comparison compare(Workspace &floating, Workspace &fixed, Id floating_id, bool large, uint32_t seed) {
    Comparison result{};
    if (floating_id != Id::OscFloat && floating_id != Id::FilterFloat && floating_id != Id::ModalFloat) return result;
    const Id fixed_id = static_cast<Id>(static_cast<unsigned>(floating_id) + 1);
    prepare(floating, floating_id, large, seed); prepare(fixed, fixed_id, large, seed);
    step(floating); step(fixed);
    const auto &f = context<Dsp>(floating); const auto &q = context<Dsp>(fixed);
    result.samples = f.length; result.finite = true;
    for (uint32_t i = 0; i < f.length; ++i) {
        const double error = double(f.output[i]) - q.output[i];
        result.max_abs_lsb = std::max(result.max_abs_lsb, std::fabs(error)); result.rms_lsb += error * error;
    }
    result.rms_lsb = std::sqrt(result.rms_lsb / result.samples);
    return result;
}
FftComparison validateFft(Workspace &w, bool large, uint32_t seed) {
    prepare(w, Id::Fft, large, seed); step(w);
    const auto &f = context<Fft>(w);
    FftComparison result{}; result.points = f.length; result.finite = true;
    double output_energy = 0, input_energy = 0;
    constexpr double Tau = 6.283185307179586476925286766559;
    for (uint32_t k = 0; k < f.length; ++k) {
        uint32_t state = seedState(seed);
        double real = 0, imag = 0;
        for (uint32_t n = 0; n < f.length; ++n) {
            const double input = double(int32_t(random32(state) & 65535u) - 32768) / 32768.;
            const double angle = -Tau * double(k) * n / f.length;
            real += input * std::cos(angle); imag += input * std::sin(angle);
            if (k == 0) input_energy += input * input;
        }
        result.max_abs_error = std::max(result.max_abs_error, std::fabs(real - f.real[k]));
        result.max_abs_error = std::max(result.max_abs_error, std::fabs(imag - f.imag[k]));
        output_energy += double(f.real[k]) * f.real[k] + double(f.imag[k]) * f.imag[k];
        result.finite &= std::isfinite(f.real[k]) && std::isfinite(f.imag[k]);
    }
    result.relative_energy_error = std::fabs(output_energy / f.length - input_energy) / std::max(input_energy, 1.e-30);
    return result;
}
} // namespace perf_kernels

#if defined(GRIM_PERF) || defined(GRIM_PERF_KERNEL_STANDALONE)
#include <Arduino.h>
#include <esp_timer.h>
#include <cstdio>
#include <memory>
namespace perf_kernels {
bool command(const char *line) {
    if (std::strncmp(line, "perf kernel", 11) != 0) return false;
    if (std::strcmp(line, "perf kernel list") == 0) {
        Serial.print("PERF {\"schema\":1,\"type\":\"kernel_list\",\"kernels\":[");
        for (unsigned i = 0; i < static_cast<unsigned>(Id::Count); ++i) Serial.printf("%s\"%s\"", i ? "," : "", descriptor(static_cast<Id>(i)).name);
        Serial.println("]}"); return true;
    }
    char name[32], extra; unsigned long seed, iterations; unsigned large;
    if (std::sscanf(line, "perf kernel %31s %lu %lu %u %c", name, &seed, &iterations, &large, &extra) != 4 ||
        iterations < 1 || iterations > 256 || large > 1 || !find(name)) {
        Serial.println("PERF {\"schema\":1,\"type\":\"kernel_error\",\"error\":\"usage: perf kernel NAME SEED ITERATIONS(1..256) LARGE(0|1)\"}"); return true;
    }
    const uint32_t heap_before = ESP.getFreeHeap();
    std::unique_ptr<Workspace> workspace(new(std::nothrow) Workspace);
    if (!workspace) { Serial.println("PERF {\"schema\":1,\"type\":\"kernel_error\",\"error\":\"workspace allocation failed\"}"); return true; }
    const Descriptor &d = *find(name);
    prepare(*workspace, d.id, large != 0, static_cast<uint32_t>(seed));
    for (unsigned i = 0; i < 3; ++i) { step(*workspace); delay(1); }
    // Reinitialize after warmup so checksums always describe ITERATIONS steps,
    // independent of the warmup implementation.
    prepare(*workspace, d.id, large != 0, static_cast<uint32_t>(seed));
    uint32_t samples[256]; uint64_t sum = 0;
    const int64_t run_start = esp_timer_get_time();
    uint32_t heap_min = ESP.getFreeHeap();
    for (unsigned i = 0; i < iterations; ++i) {
        const int64_t start = esp_timer_get_time(); step(*workspace);
        samples[i] = static_cast<uint32_t>(esp_timer_get_time() - start); sum += samples[i];
        heap_min = std::min(heap_min, ESP.getFreeHeap()); delay(1);
    }
    const uint64_t elapsed = esp_timer_get_time() - run_start;
    const Result output = inspect(*workspace);
    uint32_t ordered[256]; std::memcpy(ordered, samples, iterations * sizeof(samples[0]));
    std::sort(ordered, ordered + iterations);
    const auto percentile = [&](unsigned percent) { return ordered[(iterations * percent + 99) / 100 - 1]; };
    Serial.printf("PERF {\"schema\":1,\"type\":\"kernel\",\"kernel\":\"%s\",\"algorithm\":\"%s\",\"precision\":\"%s\",\"work_unit\":\"%s\",\"seed\":%lu,\"large\":%u,\"warmup\":3,\"samples\":%lu,\"elapsed_us\":%llu,\"sum_us\":%llu,\"min_us\":%u,\"median_us\":%u,\"p95_us\":%u,\"p99_us\":%u,\"max_us\":%u,\"percentile_method\":\"exact nearest rank\",\"timing_scope\":\"step only; output inspection excluded; RTOS preemption included\",\"workspace_allocated_bytes\":%u,\"working_bytes\":%u,\"output_bytes\":%u,\"work_units_per_step\":%u,\"checksum_fnv1a\":%u,\"output_min\":%.9g,\"output_max\":%.9g,\"output_sum\":%.12g,\"finite\":%s,\"heap_before\":%u,\"heap_min\":%u,\"cpu_mhz\":%u,\"samples_us\":[",
        d.name, d.algorithm, d.precision, d.work_unit, seed, large, iterations,
        static_cast<unsigned long long>(elapsed), static_cast<unsigned long long>(sum), ordered[0], percentile(50), percentile(95), percentile(99), ordered[iterations - 1],
        static_cast<unsigned>(sizeof(Workspace)), output.working_bytes, output.output_bytes, output.work_units, output.checksum,
        output.output_min, output.output_max, output.output_sum, output.finite ? "true" : "false", heap_before, heap_min, ESP.getCpuFreqMHz());
    for (unsigned i = 0; i < iterations; ++i) Serial.printf(i ? ",%u" : "%u", samples[i]);
    Serial.println("]}");
    return true;
}
} // namespace perf_kernels
#endif
