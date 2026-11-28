// DRONE — generative drone box. Five slow voices hold a chord from the chosen
// scale and drift to new degrees on their own; a resonant low-pass breathes
// over them, wind and bells come and go, all through a dark feedback delay.
//
// keys  a w s e d f t g y h u j k : root note (piano row, a = C)
//       1-6 scale   z/x octave   ;/. brightness   ,// drift
//       p style (bell voice + pad colour + delay space)
//       n wind   b bells   space freeze   r reseed
#include "../dsp.h"
#include "../audio_exchange.h"
#include "../rite.h"
#include "../theme.h"
#include <stdio.h>
#include <new>
#include <string.h>
#ifdef GRIM_DEVICE
#include <esp_system.h>
#endif

using namespace theme;

namespace {
struct Scale {
    const char *name;
    int8_t steps[7];
    int n;
};
const Scale SCALES[] = {
    {"phrygian", {0, 1, 3, 5, 7, 8, 10}, 7}, {"aeolian", {0, 2, 3, 5, 7, 8, 10}, 7},
    {"dorian", {0, 2, 3, 5, 7, 9, 10}, 7},   {"locrian", {0, 1, 3, 5, 6, 8, 10}, 7},
    {"hijaz", {0, 1, 4, 5, 7, 8, 10}, 7},    {"fifths", {0, 7, 0, 7, 0, 7, 0}, 2},
};
constexpr int N_SCALES = sizeof(SCALES) / sizeof(SCALES[0]);

// A style is the instrument, independent of the scale: how the bells are
// struck (2-op FM ratio/index/decay, register, density, clusters), what the
// pad is made of (saw vs triangle, detune, sub) and how big the room is.
struct Style {
    const char *name;
    float ratio, index;   // FM modulator ratio and depth (cycles)
    float decay;          // bell ring time, seconds
    int8_t reg;           // semitones above the C3 octave of the root
    float density;        // strike rate multiplier
    uint8_t cluster;      // max notes per strike (rolled)
    float saw, detune, sub, res;
    int16_t tapA, tapB;   // delay taps (<= 4095)
    float fb, wet;
};
const Style STYLES[] = {
    // name      ratio idx   dec  reg dens cl  saw  det     sub  res   tapA  tapB  fb    wet
    {"temple",   1.40f, .32f, 4.0f, 12, 1.0f, 1, .80f, .0031f, .6f, .00f, 4095, 3072, .62f, .50f},
    {"tubular",  3.50f, .22f, 3.0f, 12, 1.2f, 2, .55f, .0022f, .5f, .10f, 3600, 2400, .55f, .45f},
    {"bowl",     2.76f, .10f, 7.5f, 0,  .45f, 1, .15f, .0012f, .6f, .05f, 4095, 3900, .64f, .55f},
    {"kalimba",  1.00f, .18f, 0.7f, 12, 2.6f, 3, .35f, .0040f, .4f, .20f, 2200, 1500, .45f, .40f},
    {"gong",     1.17f, .60f, 8.0f, -12, .30f, 1, .90f, .0050f, .45f, .25f, 4095, 2900, .65f, .50f},
    {"ice",      4.00f, .14f, 2.2f, 12, 1.6f, 2, .25f, .0018f, .3f, .35f, 1800, 1150, .58f, .55f},
};
constexpr int N_STYLES = sizeof(STYLES) / sizeof(STYLES[0]);
constexpr int BELL_CEIL = 76; // E5: no bell fundamental above ~660 Hz

struct Bell {
    dsp::Osc car, mod;
    dsp::Decay env, modEnv;
};
constexpr int NB = 3;

constexpr int NV = 5;
struct Voice {
    dsp::Osc a, b, sub;
    float hz = 110, target = 110;
    float amp = 0, ampTarget = 0.5f;
    float swellRate = 0.0002f;
    int degree = 0, octave = 0;
};

struct Controls {
    int root = 38;          // D2
    int scale = 0;
    float bright = 0.45f;   // 0..1
    float drift = 0.5f;     // 0..1
    bool wind = true, bells = true, freeze = false;
    bool safeMode = false;
    int style = 0;
    uint32_t reseed = 0;
};
struct VoiceView { float hz = 110, amp = 0; };
struct Telemetry {
    VoiceView v[NV];
    float cutoffNow = 600;
    int lastMoved = -1;
    uint32_t movedAt = 0;
};
struct State : Controls, audio_exchange::InternalState {
    Controls audio;
    audio_exchange::Latest<Controls> updates;
    audio_exchange::Latest<Telemetry> feedback;
    Telemetry view;
    float windAmp = 1;      // eases toward the wind toggle
    Voice v[NV];
    dsp::SVF filt, windF;
    dsp::Osc lfo, lfo2;
    dsp::Noise rng;
    Bell bl[NB];
    int nextBell = 0;
    int strikeLeft = 0, strikeDeg = 0;
    uint32_t strikeAt = 0;
    // Keep the feedback buffer below 20 KB on the PSRAM-less Cardputer. The
    // old 9000-sample delay used 36 KB by itself and could exhaust/fragment
    // heap after visiting radio-heavy rites before opening DRONE.
    dsp::Delay<4096> dl;
    float dlLp = 0;
    uint32_t ctrl = 0, nextMove = 0;
    float cutoffNow = 600;
    dsp::Scope scope;
    int lastMoved = -1;
    uint32_t movedAt = 0;
} *S = nullptr;

void publish() { S->updates.publish(static_cast<const Controls &>(*S)); }

float noteHz(int degree, int octave) {
    const Scale &sc = SCALES[S->audio.scale];
    int d = ((degree % sc.n) + sc.n) % sc.n;
    int o = octave + (degree < 0 ? -1 : degree / sc.n);
    return dsp::midiHz(S->audio.root + sc.steps[d] + 12 * o);
}

void retune(int i, bool snap) {
    Voice &v = S->v[i];
    v.target = noteHz(v.degree, v.octave);
    if (snap) v.hz = v.target;
}

void seed(bool snap) {
    // root, fifth, octave-ish spread, then two wanderers higher up
    const int deg[NV] = {0, 4, 0, 2, 6};
    const int oct[NV] = {0, 0, 1, 1, 1};
    for (int i = 0; i < NV; ++i) {
        Voice &v = S->v[i];
        v.degree = deg[i] + (S->rng.next() % 3 == 0 ? 2 : 0);
        v.octave = oct[i];
        v.ampTarget = 0.25f + (S->rng.next() % 100) / 160.f;
        v.swellRate = 0.00008f + (S->rng.next() % 100) * 0.000003f;
        retune(i, snap);
    }
    S->v[0].degree = 0;
    S->v[0].octave = 0;
    retune(0, snap);
}

void applyStyle() {
    const Style &st = STYLES[S->audio.style];
    for (int i = 0; i < NB; ++i) {
        S->bl[i].env.time(st.decay);
        S->bl[i].modEnv.time(st.decay * 0.22f);
    }
}

void strike(int degree) {
    State &s = *S;
    const Style &st = STYLES[s.audio.style];
    const Scale &sc = SCALES[s.audio.scale];
    int d = ((degree % sc.n) + sc.n) % sc.n;
    int note = 48 + s.audio.root % 12 + sc.steps[d] + st.reg + 12 * (degree / sc.n);
    while (note > BELL_CEIL) note -= 12;
    while (note < 26) note += 12;
    float hz = dsp::midiHz(note);
    Bell &b = s.bl[s.nextBell];
    s.nextBell = (s.nextBell + 1) % NB;
    b.car.hz(hz);
    b.mod.hz(hz * st.ratio);
    b.env.trigger(0.35f + (s.rng.next() % 100) * 0.0025f);
    b.modEnv.trigger(1.f);
}

void moveVoice() {
    // pick a voice (never the root drone) and step it a little through the scale
    int i = 1 + S->rng.next() % (NV - 1);
    Voice &v = S->v[i];
    int step = (int)(S->rng.next() % 5) - 2;
    if (step == 0) step = 1;
    v.degree += step;
    if (v.degree > 9) v.degree -= 7;
    if (v.degree < -2) v.degree += 7;
    v.ampTarget = 0.15f + (S->rng.next() % 100) / 140.f;
    retune(i, false);
    S->lastMoved = i;
    S->movedAt = hal::ms();
}

void render(int16_t *out, int n) {
    State &s = *S;
    Controls next;
    if (s.updates.consume(next)) {
        const bool tuning = next.root != s.audio.root || next.scale != s.audio.scale;
        const bool style = next.style != s.audio.style;
        const bool reseed = next.reseed != s.audio.reseed;
        s.audio = next;
        if (tuning) for (int k = 0; k < NV; ++k) retune(k, false);
        if (style) applyStyle();
        if (reseed) seed(false);
    }
    for (int i = 0; i < n; ++i) {
        if ((s.ctrl++ & 63) == 0) {
            // control rate: glides, swells, filter LFO, wandering, bells
            for (int k = 0; k < NV; ++k) {
                Voice &v = s.v[k];
                v.hz += (v.target - v.hz) * 0.0045f;
                const float det = STYLES[s.audio.style].detune;
                v.a.hz(v.hz * (1.f + det));
                v.b.hz(v.hz * (1.f - det));
                v.sub.hz(v.hz * 0.5f);
                v.amp += (v.ampTarget - v.amp) * v.swellRate * 64;
                if (fabsf(v.amp - v.ampTarget) < 0.01f) v.ampTarget = 0.1f + (s.rng.next() % 100) / 120.f;
            }
            float l = s.lfo.sineStrided(64);
            float l2 = s.lfo2.sineStrided(64);
            float cut = 120.f + s.audio.bright * s.audio.bright * 2600.f * (1.f + 0.45f * l + 0.2f * l2);
            s.cutoffNow = cut;
            s.filt.set(cut, fminf(0.9f, 0.35f + 0.25f * s.audio.bright + STYLES[s.audio.style].res));
            s.windF.set(300.f + 900.f * (0.5f + 0.5f * l2), 0.85f);
            s.windAmp += ((s.audio.wind ? 1.f : 0.f) - s.windAmp) * 0.006f;
            if (!s.audio.freeze && int32_t(s.ctrl - s.nextMove) > 0) {
                moveVoice();
                uint32_t gap = (uint32_t)((1.f - s.audio.drift) * 9.f * hal::RATE) + hal::RATE / 2;
                s.nextMove = s.ctrl + gap / 2 + s.rng.next() % gap;
            }
            const Style &st = STYLES[s.audio.style];
            if (s.audio.bells && !s.audio.safeMode) {
                if (s.strikeLeft > 0 && int32_t(s.ctrl - s.strikeAt) >= 0) {
                    // rolled cluster: climb the scale in thirds, slightly uneven
                    strike(s.strikeDeg);
                    s.strikeDeg += 2;
                    --s.strikeLeft;
                    s.strikeAt = s.ctrl + hal::RATE / 9 + s.rng.next() % (hal::RATE / 6);
                } else if (s.strikeLeft == 0) {
                    int odds = (int)(hal::RATE / 64 * (3.5f - s.audio.drift * 2.f) / st.density);
                    if (odds < 8) odds = 8;
                    if (s.rng.next() % odds == 0) {
                        s.strikeDeg = s.rng.next() % SCALES[s.audio.scale].n;
                        s.strikeLeft = 1 + s.rng.next() % st.cluster;
                        s.strikeAt = s.ctrl;
                    }
                }
            }
        }
        const Style &st = STYLES[s.audio.style];
        float mix = 0;
        for (int k = 0; k < NV; ++k) {
            Voice &v = s.v[k];
            float sa = v.a.saw(), sb = v.b.saw();
            float saw = (sa + sb) * 0.5f;
            float tri = fabsf(sa) + fabsf(sb) - 1.f; // same phases, folded
            float x = saw * st.saw + tri * (1.f - st.saw) * 1.3f + v.sub.sine() * st.sub;
            mix += x * v.amp;
        }
        mix = s.filt.lp(mix * 0.32f);
        if (s.windAmp > 0.001f) mix += s.windF.bp(s.rng.white()) * 0.09f * s.windAmp;
        float bells = 0;
        for (int k = 0; k < NB; ++k) {
            Bell &b = s.bl[k];
            if (b.env.v < 0.0004f) continue;
            float m = b.mod.sine() * st.index * b.modEnv.tick();
            bells += b.car.pmSine(m) * b.env.tick();
        }
        mix += bells * 0.24f;
        // dark delay: two taps, low-passed feedback
        float wet = s.audio.safeMode ? 0.f : s.dl.tap(st.tapA) * 0.55f + s.dl.tap(st.tapB) * 0.3f;
        s.dlLp += (wet - s.dlLp) * 0.18f;
        s.dl.put(mix + s.dlLp * st.fb);
        float o = mix * 0.8f + s.dlLp * st.wet;
        int16_t p = dsp::toPcm(o * 0.9f);
        out[i] = p;
        if ((i & 3) == 0) s.scope.put(p);
    }
    Telemetry t;
    for (int k = 0; k < NV; ++k) t.v[k] = {s.v[k].hz, s.v[k].amp};
    t.cutoffNow = s.cutoffNow; t.lastMoved = s.lastMoved; t.movedAt = s.movedAt;
    s.feedback.publish(t);
}

// The ~17 KB of voices and delay are freed on leave so radio rites get that
// heap back; only the player's settings survive until the next visit.
struct Saved {
    bool valid;
    int root, scale, style;
    float bright, drift;
    bool wind, bells, freeze;
} saved = {};

void enter() {
    if (!S) {
        S = new (std::nothrow) State();
        if (!S) { hal::setAudio(nullptr); return; }
        if (saved.valid) {
            S->root = saved.root;
            S->scale = saved.scale;
            S->style = saved.style;
            S->bright = saved.bright;
            S->drift = saved.drift;
            S->wind = saved.wind;
            S->bells = saved.bells;
            S->freeze = saved.freeze;
            S->windAmp = S->wind ? 1.f : 0.f;
        }
    }
    S->lfo.hz(0.045f);
    S->lfo2.hz(0.013f);
    dsp::prepare();
#ifdef GRIM_DEVICE
    // Radio rites can leave the PSRAM-less S3 heap fragmented. Safe mode keeps
    // the core drone alive while dropping bells and feedback if headroom is low.
    S->safeMode = esp_get_free_heap_size() < 60000;
#else
    S->safeMode = false;
#endif
    S->audio = static_cast<const Controls &>(*S);
    applyStyle();
    seed(true);
    hal::setAudio(render);
}
void leave() {
    hal::setAudio(nullptr);
    if (!S) return;
    saved = {true, S->root, S->scale, S->style, S->bright, S->drift, S->wind, S->bells, S->freeze};
    delete S; // the shell has already detached audio
    S = nullptr;
}

const char PIANO[] = "awsedftgyhujk";

bool onKey(const hal::Key &k) {
    if (!S) return false;
    State &s = *S;
    if (k.ch) {
        const char *p = strchr(PIANO, k.ch);
        if (p && k.ch) {
            int semis = p - PIANO;
            s.root = (s.root / 12) * 12 + semis;

            publish(); return true;
        }
        if (k.ch >= '1' && k.ch < '1' + N_SCALES) {
            s.scale = k.ch - '1';

            publish(); return true;
        }
    }
    switch (k.nav) {
    case hal::N_UP: s.bright = fminf(1.f, s.bright + 0.05f); publish(); return true;
    case hal::N_DOWN: s.bright = fmaxf(0.f, s.bright - 0.05f); publish(); return true;
    case hal::N_RIGHT: s.drift = fminf(1.f, s.drift + 0.1f); publish(); return true;
    case hal::N_LEFT: s.drift = fmaxf(0.f, s.drift - 0.1f); publish(); return true;
    default: break;
    }
    switch (k.ch) {
    case 'z': if (s.root >= 24) s.root -= 12; break;
    case 'x': if (s.root < 60) s.root += 12; break;
    case 'p': s.style = (s.style + 1) % N_STYLES; break;
    case 'n': s.wind = !s.wind; break;
    case 'b': s.bells = !s.bells; break;
    case 'q': s.safeMode = !s.safeMode; break;
    case ' ': s.freeze = !s.freeze; break;
    case 'r': ++s.reseed; break;
    }
    publish(); return true;
}

bool onPointer(const InstrumentPointer &p) {
    if (!S) return false;
    if (!p.down) return true;
    if (p.start && (p.x < 8 || p.x > 126 || p.y < 23 || p.y > 119)) return false;
    S->drift = fminf(1.f, fmaxf(0.f, (p.x - 8) / 118.f));
    S->bright = fminf(1.f, fmaxf(0.f, (119 - p.y) / 96.f));
    publish();
    return true;
}

void draw(uint32_t now) {
    if (!S) { starved("DRONE", "drone"); return; }
    State &s = *S;
    s.feedback.consume(s.view);
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("DRONE", "drone");

    // mandala: each voice is a ring of 7 nodes; radius from pitch, glow from amp
    const int cx = 70, cy = 75;
    float rot = now * 0.00012f;
    cv.drawCircle(cx, cy, 52, C_DUSK);
    for (int k = 0; k < NV; ++k) {
        const VoiceView &v = s.view.v[k];
        float r = 14 + log2f(v.hz / 30.f) * 9.f;
        if (r > 50) r = 50;
        uint8_t a = (uint8_t)fminf(255.f, 60 + v.amp * 260.f);
        uint16_t c = blend(k == s.view.lastMoved && now - s.view.movedAt < 1500 ? C_GLOW : C_EMBER, C_VOID, a);
        int nodes = 7;
        float ph = rot * (k & 1 ? -1.f : 1.f) * (1 + k * 0.3f);
        int px0 = 0, py0 = 0, fx = 0, fy = 0;
        for (int j = 0; j <= nodes; ++j) {
            float t = ph + j * 6.2831853f / nodes;
            int px = cx + (int)(cosf(t) * r), py = cy + (int)(sinf(t) * r);
            if (j) cv.drawLine(px0, py0, px, py, blend(C_BLOOD, C_VOID, a));
            else { fx = px; fy = py; }
            px0 = px;
            py0 = py;
            if (j < nodes) cv.fillCircle(px, py, 1, c);
        }
        (void)fx; (void)fy;
    }
    // polar scope in the middle
    int w = s.scope.w;
    int lx = 0, ly = 0;
    for (int i = 0; i <= 64; ++i) {
        int16_t v = s.scope.s[(w + i * 4) & (dsp::Scope::N - 1)];
        float r = 10 + v / 2400.f;
        float t = i * 6.2831853f / 64;
        int px = cx + (int)(cosf(t) * r), py = cy + (int)(sinf(t) * r);
        if (i) cv.drawLine(lx, ly, px, py, C_RUST);
        lx = px;
        ly = py;
    }

#ifdef GRIM_CYD
    text("touch: light / drift", 10, 114, C_DIM);
#endif
    // panel
    char b[40];
    int px = 132;
    frame(px - 4, 24, 110, 92, C_CLOT, C_UMBER);
    snprintf(b, sizeof b, "%s%d", noteName(s.root), s.root / 12 - 1);
    text("root", px, 29, C_DIM);
    text(b, px + 40, 26, C_GLOW, 2);
    text("mode", px, 46, C_DIM);
    text(SCALES[s.scale].name, px + 40, 46, C_BONE);
    meter("light", px, 58, px + 40, 60, s.bright);
    meter("drift", px, 70, px + 40, 60, s.drift);
    snprintf(b, sizeof b, "%s %s %s", s.safeMode ? "SAFE" : (s.wind ? "WIND" : "wind"), s.bells ? "BELL" : "bell",
             s.freeze ? "HOLD" : "flow");
    text(b, px, 84, s.safeMode ? C_GLOW : (s.freeze ? C_GLOW : C_EMBER));
    text("style", px, 96, C_DIM);
    text(STYLES[s.style].name, px + 40, 96, C_BONE);
    snprintf(b, sizeof b, "lp %4dhz", (int)s.view.cutoffNow);
    text(b, px, 106, C_DIM);
    footer("a-k root 1-6 mode p style spc hold", "drag: drift / brightness");
}
} // namespace

const Rite RITE_DRONE = {"DRONE", "drone", "a generative drone box", enter, leave, onKey, draw, onPointer};
