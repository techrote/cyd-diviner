// Tiny fixed-rate DSP kit for the rites: table oscillators, noise, envelopes,
// a Chamberlin state-variable filter and a feedback delay. Everything runs
// per sample on the audio thread at RATE, so keep it cheap.
#pragma once
#ifndef DIVINER_DSP_RATE
#include "hal.h"
#endif
#include <atomic>
#include <math.h>
#include <stdint.h>

namespace dsp {

#ifdef DIVINER_DSP_RATE
constexpr int RATE = DIVINER_DSP_RATE;
#else
constexpr int RATE = hal::RATE;
#endif
static_assert(RATE >= 8000, "DSP requires at least 8 kHz");

constexpr float PI2 = 6.2831853f;

// Built once with C++ thread-safe static initialization. Call prepare() from
// the control thread before publishing an audio callback to avoid first-use work.
inline const float *sineTable() {
    struct Table {
        float values[1025];
        Table() { for (int i = 0; i <= 1024; ++i) values[i] = sinf(PI2 * i / 1024.f); }
    };
    static const Table table;
    return table.values;
}
inline void prepare() { (void)sineTable(); }

inline uint32_t hzToInc(float hz) {
    if (!(hz > 0.f) || !isfinite(hz)) return 0;
    if (hz >= RATE) hz = fmodf(hz, float(RATE));
    return static_cast<uint32_t>(double(hz) * (4294967296.0 / RATE));
}
inline float midiHz(float note) { return 440.f * powf(2.f, (note - 69.f) / 12.f); }

struct Osc {
    uint32_t phase = 0, inc = 0;
    void hz(float f) { inc = hzToInc(f); }
    inline float sine() {
        const float *t = sineTable();
        uint32_t i = phase >> 22;
        float frac = (phase & 0x3FFFFF) * (1.f / 4194304.f);
        float v = t[i] + (t[i + 1] - t[i]) * frac;
        phase += inc;
        return v;
    }
    // Advance an oscillator called at a decimated control rate by the
    // number of elapsed audio samples, retaining frequency units in Hz.
    inline float sineStrided(uint32_t samplesPerTick) {
        float value = sine();
        if (samplesPerTick > 1) phase += inc * (samplesPerTick - 1);
        return value;
    }
    // Phase-modulated sine: offset in cycles (any sign/size). Bells use this
    // for 2-op FM so the strike has real inharmonic partials.
    inline float pmSine(float cyc) {
        const float *t = sineTable();
        if (!isfinite(cyc)) cyc = 0.f;
        cyc -= floorf(cyc);
        uint32_t ph = phase + (uint32_t)(cyc * 4294967040.f);
        uint32_t i = ph >> 22;
        float frac = (ph & 0x3FFFFF) * (1.f / 4194304.f);
        phase += inc;
        return t[i] + (t[i + 1] - t[i]) * frac;
    }
    inline float saw() {
        float v = (int32_t)phase * (1.f / 2147483648.f);
        phase += inc;
        return v;
    }
    inline float square(uint32_t pw = 0x80000000u) {
        float v = phase < pw ? 1.f : -1.f;
        phase += inc;
        return v;
    }
    inline float tri() {
        float s = (int32_t)phase * (1.f / 2147483648.f);
        phase += inc;
        return 2.f * fabsf(s) - 1.f;
    }
};

struct Noise {
    uint32_t s = 0x9E3779B9u;
    inline uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    inline float white() { return (int32_t)next() * (1.f / 2147483648.f); }
};

// Exponential decay envelope; trigger() then tick() per sample.
struct Decay {
    float v = 0, k = 0.999f;
    void time(float sec) { k = sec > 0.f && isfinite(sec) ? expf(-1.f / (sec * RATE)) : 0.f; }
    void trigger(float level = 1.f) { v = level; }
    inline float tick() { return v *= k; }
};

// Chamberlin SVF. f = 2 sin(pi fc / fs); stable for fc < fs/6.
struct SVF {
    float low = 0, band = 0, f = 0.1f, q = 1.f;
    void set(float hz, float res) { // res 0..1
        if (!(hz >= 0.f)) hz = 0.f;
        if (!(res >= 0.f)) res = 0.f;
        if (res > 1.f) res = 1.f;
        if (hz > RATE / 6.5f) hz = RATE / 6.5f;
        f = 2.f * sinf(3.14159265f * hz / RATE);
        q = 2.f - 1.9f * res;
        // Chamberlin is only stable while f^2 + 2qf < 4
        float fmax = 0.95f * (sqrtf(q * q + 4.f) - q);
        if (f > fmax) f = fmax;
    }
    inline float lp(float in) {
        low += f * band;
        float high = in - low - q * band;
        band += f * high;
        return low;
    }
    inline float bp(float in) {
        lp(in);
        return band;
    }
};

template <int N> struct Delay {
    static_assert(N > 0, "delay needs storage");
    // power-of-two lengths wrap with a mask instead of an integer divide
    // (three per sample per rite on the audio core)
    static constexpr bool POW2 = (N & (N - 1)) == 0;
    float buf[N] = {};
    int w = 0;
    inline float tap(int d) const {
        if constexpr (POW2) return buf[(uint32_t(w) - uint32_t(d)) & (N - 1)];
        int r = d % N;
        return buf[(w - r + N) % N];
    }
    inline void put(float v) {
        buf[w] = v;
        w = POW2 ? (w + 1) & (N - 1) : (w + 1) % N;
    }
};

// The same echo line stored as int16 (half the RAM): for rites that share
// the heap with a radio stack. Plenty of resolution for a dark echo.
template <int N> struct Delay16 {
    static_assert(N > 0 && (N & (N - 1)) == 0, "Delay16 length must be a power of two");
    int16_t buf[N] = {};
    int w = 0;
    inline float tap(int d) const { return buf[(w - d) & (N - 1)] * (1.f / 16384.f); }
    inline void put(float v) {
        if (!(v == v)) v = 0.f;
        v = v > 1.99f ? 1.99f : v < -1.99f ? -1.99f : v;
        buf[w] = (int16_t)(v * 16384.f);
        w = (w + 1) & (N - 1);
    }
};

inline float softclip(float x) {
    if (x > 1.5f) return 1.f;
    if (x < -1.5f) return -1.f;
    return x - x * x * x * (4.f / 27.f);
}

inline int16_t toPcm(float x) {
    if (!(x == x)) x = 0; // NaN guard: never hand the codec garbage
    x = softclip(x);
    return (int16_t)(x * 30000.f);
}

// A scope ring the rites fill from the audio thread and draw from the UI.
struct Scope {
    static constexpr int N = 256;
    // Relaxed atomic samples avoid races for legacy viewers. A viewer can
    // observe a rolling frame; new instruments publish coherent telemetry.
    std::atomic<int32_t> s[N]{};
    std::atomic<int> w{0};
    Scope() { for (auto &sample : s) sample.store(0, std::memory_order_relaxed); }
    inline void put(int16_t v) {
        const int i = w.load(std::memory_order_relaxed);
        s[i].store(v, std::memory_order_relaxed);
        w.store((i + 1) & (N - 1), std::memory_order_relaxed);
    }
};

} // namespace dsp
