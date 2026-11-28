// Tiny UI sound engine: short, bright blips for menu navigation, select and
// back — the Kingdom-Hearts-menu feel. The shell installs render() as its
// audio callback while in the splash/menu and calls the trigger helpers on
// input. Rites that want their own sound just call hal::setAudio() over it.
#pragma once
#include "audio_exchange.h"
#include "dsp.h"
#include "hal.h"

namespace uisfx {

enum Id { MOVE, SELECT, BACK, OPEN, TOGGLE };

namespace detail {
struct Voice {
    dsp::Osc a, b;
    dsp::Decay env;
    float mix2 = 0, gain = 0;
    bool active = false;
};
inline Voice voices[4];
inline uint8_t next = 0;

// Single-producer (UI) / single-consumer (audio) event ring. Reuse the same
// aligned 32-bit bounded queue as the instruments; byte atomics on ESP32-S3
// may route through hidden masked-CAS retry loops.
inline audio_exchange::SpscQueue<uint8_t, 15> events;

inline void start(Id id) {
    Voice &v = voices[next++ & 3];
    v.a.phase = v.b.phase = 0;
    switch (id) {
    case MOVE: // crisp bright tick
        v.a.hz(1318); v.b.hz(1976); v.mix2 = 0.28f; v.gain = 0.42f; v.env.time(0.045f); v.env.trigger(1.f);
        break;
    case SELECT: // confirm ding (C6 + G6)
        v.a.hz(1046); v.b.hz(1568); v.mix2 = 0.55f; v.gain = 0.46f; v.env.time(0.16f); v.env.trigger(1.f);
        break;
    case BACK: // soft low blip
        v.a.hz(620); v.b.hz(415); v.mix2 = 0.4f; v.gain = 0.4f; v.env.time(0.09f); v.env.trigger(1.f);
        break;
    case OPEN: // fuller swell when a rite opens
        v.a.hz(784); v.b.hz(1046); v.mix2 = 0.6f; v.gain = 0.5f; v.env.time(0.22f); v.env.trigger(1.f);
        break;
    case TOGGLE: // dry click
        v.a.hz(2100); v.b.hz(2100); v.mix2 = 0; v.gain = 0.3f; v.env.time(0.02f); v.env.trigger(1.f);
        break;
    }
    v.active = true;
}
} // namespace detail

inline void trigger(Id id) {
    using namespace detail;
    (void)events.push(static_cast<uint8_t>(id)); // full queue deliberately drops new UI blips
}

inline void render(int16_t *out, int n) {
    using namespace detail;
    uint8_t event = 0;
    // Preserve the old ring's maximum of fifteen admitted events per block.
    for (int queued = 0; queued < 15 && events.pop(event); ++queued)
        start(static_cast<Id>(event));
    for (int i = 0; i < n; ++i) {
        float x = 0;
        for (auto &v : voices) {
            if (!v.active) continue;
            float e = v.env.tick();
            if (e < 0.0008f) { v.active = false; continue; }
            x += (v.a.sine() + v.b.sine() * v.mix2) * e * v.gain;
        }
        out[i] = dsp::toPcm(x * 0.7f);
    }
}

} // namespace uisfx
