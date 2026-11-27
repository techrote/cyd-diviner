// CANON — tape loops drifting out of phase. One short phrase plays on four
// loops at once, each a hair longer than the last, so they start in unison
// and slowly come apart into ever-changing counterpoint (after Eno's Music
// for Airports and Reich's phase pieces). Each loop is its own instrument
// and register; all of it falls through a long dark echo.
//
// keys  a w s e d f t g y h u j k : write a note into the phrase   0 rest
//       bksp erase   r new phrase   1-4 mode   z/x octave
//       ;/. tempo   ,// spread   enter realign   space hold
#include "../dsp.h"
#include "../rite.h"
#include "../theme.h"
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
struct Scale {
    const char *name;
    int8_t steps[7];
    int n;
};
const Scale SCALES[] = {
    {"ionian", {0, 2, 4, 5, 7, 9, 11}, 7},
    {"aeolian", {0, 2, 3, 5, 7, 8, 10}, 7},
    {"pent", {0, 2, 4, 7, 9, 0, 0}, 5},
    {"lydian", {0, 2, 4, 6, 7, 9, 11}, 7},
};
constexpr int N_SCALES = sizeof(SCALES) / sizeof(SCALES[0]);
const char PIANO[] = "awsedftgyhujk";

constexpr int MAXP = 8, NL = 4;
constexpr int8_t REST = -128;

// four reels: register, FM colour, ring time, and how much each one lags
struct Reel {
    const char *name;
    int8_t oct;
    float ratio, index, decay, gain;
};
const Reel REELS[NL] = {
    {"glass", 12, 3.5f, .16f, 2.4f, .55f},
    {"voice", 0, 1.0f, .30f, 3.2f, .80f},
    {"reed", 0, 2.0f, .22f, 2.0f, .55f},
    {"stone", -12, 0.5f, .35f, 4.5f, .75f},
};

struct Note {
    dsp::Osc car, mod;
    dsp::Decay env, modEnv;
    float att = 1; // 0..1 soft attack ramp
};
struct Loop {
    Note nt[2]; // alternate so the last note rings under the next
    int cur = 0;
    uint32_t count = 0, sps = 1; // samples into this step / samples per step
    volatile int idx = 0;
    volatile uint32_t hits = 0;
    // UI
    uint32_t seenHits = 0, hitAt = 0;
    int hitIdx = 0;
};

struct State {
    int8_t phrase[MAXP];
    int len = 0;
    int scale = 0, root = 57; // A3
    float tempo = 0.45f;      // 0..1 -> 1.6 s .. 0.3 s per step
    float spread = 0.5f;      // 0..1 how much the reels differ
    bool hold = false;
    Loop L[NL];
    dsp::Noise rng;
    dsp::Delay<4096> dl;
    float dlLp = 0, hiss = 0;
    dsp::SVF tone;
    uint32_t lastTyped = 0;
} *S = nullptr;

float stepSec() { return 1.6f - S->tempo * 1.3f; }

void retime() {
    for (int k = 0; k < NL; ++k) {
        // each reel is longer than the one inside it; the ratios are kept
        // irrational-ish so the canon never quite repeats
        static const float LAG[NL] = {0.f, 1.f, 2.17f, 3.41f};
        float mul = 1.f + S->spread * 0.045f * LAG[k];
        S->L[k].sps = (uint32_t)(stepSec() * mul * hal::RATE);
    }
}

void realign() {
    for (auto &l : S->L) {
        l.count = l.sps; // fire step 0 on the next sample, all together
        l.idx = -1;
    }
}

int snap(int semis) {
    // nearest tone of the current scale, keeping the octave
    const Scale &sc = SCALES[S->scale];
    int o = semis >= 0 ? semis / 12 : -((11 - semis) / 12);
    int pc = semis - 12 * o, best = sc.steps[0], bd = 99;
    for (int i = 0; i < sc.n; ++i) {
        int d = abs(sc.steps[i] - pc);
        if (d < bd) { bd = d; best = sc.steps[i]; }
    }
    if (12 - pc < bd) best = 12; // closer to the next octave's root
    return best + 12 * o;
}

void randomPhrase() {
    State &s = *S;
    const Scale &sc = SCALES[s.scale];
    s.len = 5 + s.rng.next() % 4;
    int deg = s.rng.next() % sc.n;
    for (int i = 0; i < s.len; ++i) {
        if (i && s.rng.next() % 4 == 0) { s.phrase[i] = REST; continue; } // space to breathe
        deg += (int)(s.rng.next() % 5) - 2;
        if (deg < 0) deg += sc.n;
        if (deg > sc.n + 2) deg -= sc.n;
        s.phrase[i] = sc.steps[deg % sc.n] + 12 * (deg / sc.n);
    }
    realign();
}

void fire(Loop &l, const Reel &r, int8_t semis) {
    Note &n = l.nt[l.cur];
    l.cur ^= 1;
    float hz = dsp::midiHz(S->root + semis + r.oct);
    n.car.hz(hz);
    n.mod.hz(hz * r.ratio);
    n.env.time(r.decay);
    n.modEnv.time(r.decay * 0.3f);
    n.env.trigger(r.gain);
    n.modEnv.trigger(1.f);
    n.att = 0;
}

void render(int16_t *out, int n) {
    State &s = *S;
    for (int i = 0; i < n; ++i) {
        float mix = 0;
        for (int k = 0; k < NL; ++k) {
            Loop &l = s.L[k];
            const Reel &r = REELS[k];
            if (!s.hold && s.len > 0 && ++l.count >= l.sps) {
                l.count = 0;
                int nx = l.idx + 1;
                if (nx >= s.len) nx = 0;
                l.idx = nx;
                if (s.phrase[nx] != REST) {
                    fire(l, r, s.phrase[nx]);
                    l.hits = l.hits + 1;
                }
            }
            for (auto &nt : l.nt) {
                if (nt.env.v < 0.0003f) continue;
                if (nt.att < 1.f) nt.att += 1.f / (0.025f * hal::RATE);
                float m = nt.mod.sine() * r.index * nt.modEnv.tick();
                mix += nt.car.pmSine(m) * nt.env.tick() * nt.att;
            }
        }
        // a little tape: hiss that breathes, everything rounded off
        s.hiss += (s.rng.white() - s.hiss) * 0.05f;
        mix = s.tone.lp(mix * 0.3f) + s.hiss * 0.012f;
        float wet = s.dl.tap(4093) * 0.5f + s.dl.tap(3001) * 0.3f;
        s.dlLp += (wet - s.dlLp) * 0.14f;
        s.dl.put(mix + s.dlLp * 0.6f);
        out[i] = dsp::toPcm((mix * 0.8f + s.dlLp * 0.55f) * 0.95f);
    }
}

void enter() {
    if (!S) S = new State();
    if (!S) return;
    S->rng.s ^= hal::ms() * 2654435761u;
    S->tone.set(2400, 0.1f);
    retime();
    randomPhrase();
    hal::setAudio(render);
}
void leave() {
    delete S;
    S = nullptr;
}

bool onKey(const hal::Key &k) {
    if (!S) return false;
    State &s = *S;
    if (k.ch) {
        const char *p = strchr(PIANO, k.ch);
        if (p || k.ch == '0') {
            // a pause in typing starts a fresh phrase; quick keys extend it
            uint32_t now = hal::ms();
            if (now - s.lastTyped > 2500) s.len = 0;
            s.lastTyped = now;
            if (s.len == MAXP) { memmove(s.phrase, s.phrase + 1, MAXP - 1); s.len--; }
            s.phrase[s.len++] = p ? (int8_t)(p - PIANO) : REST;
            if (s.len == 1) realign();
            return true;
        }
        if (k.ch >= '1' && k.ch < '1' + N_SCALES) {
            s.scale = k.ch - '1';
            for (int i = 0; i < s.len; ++i) if (s.phrase[i] != REST) s.phrase[i] = snap(s.phrase[i]);
            return true;
        }
    }
    switch (k.ch) {
    case 'r': randomPhrase(); return true;
    case 'z': if (s.root > 33) s.root -= 12; return true;
    case 'x': if (s.root < 69) s.root += 12; return true;
    case ' ': s.hold = !s.hold; return true;
    }
    switch (k.nav) {
    case hal::N_BACK: if (s.len > 1) s.len--; break;
    case hal::N_ENTER: realign(); break;
    case hal::N_UP: s.tempo = fminf(1.f, s.tempo + 0.05f); retime(); break;
    case hal::N_DOWN: s.tempo = fmaxf(0.f, s.tempo - 0.05f); retime(); break;
    case hal::N_RIGHT: s.spread = fminf(1.f, s.spread + 0.1f); retime(); break;
    case hal::N_LEFT: s.spread = fmaxf(0.f, s.spread - 0.1f); retime(); break;
    default: break;
    }
    return true;
}

void draw(uint32_t now) {
    if (!S) return;
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("CANON", "canon");

    // ---- four reels, innermost first; each playhead sweeps its own circle
    const int cx = 64, cy = 72;
    const float RAD[NL] = {14, 24, 34, 44};
    int hx[NL], hy[NL];
    int len = s.len > 0 ? s.len : 1;
    for (int k = 0; k < NL; ++k) {
        Loop &l = s.L[k];
        if (l.hits != l.seenHits) { l.seenHits = l.hits; l.hitAt = now; l.hitIdx = l.idx; }
        float r = RAD[k];
        cv.drawCircle(cx, cy, (int)r, C_DUSK);
        // the phrase written round the reel
        for (int i = 0; i < len; ++i) {
            float a = -1.5708f + i * 6.2831853f / len;
            int px = cx + (int)(cosf(a) * r), py = cy + (int)(sinf(a) * r);
            if (s.len == 0 || s.phrase[i] == REST) { cv.drawPixel(px, py, C_CLOT); continue; }
            uint32_t age = now - l.hitAt;
            bool lit = i == l.hitIdx && age < 900;
            uint8_t g = lit ? (uint8_t)(255 - age * 255 / 900) : 0;
            diamond(px, py, lit ? 2 : 1, lit ? blend(C_GLOW, C_RUST, g) : C_BLOOD);
            if (lit) cv.drawCircle(px, py, 3 + age / 110, blend(C_EMBER, C_VOID, g)); // ripple
        }
        // playhead: continuous, from the step index and how far into it we are
        float ph = (l.idx < 0 ? 0 : l.idx) + (float)l.count / (l.sps ? l.sps : 1);
        float a = -1.5708f + ph * 6.2831853f / len;
        hx[k] = cx + (int)(cosf(a) * r);
        hy[k] = cy + (int)(sinf(a) * r);
        cv.drawLine(cx + (int)(cosf(a) * (r - 4)), cy + (int)(sinf(a) * (r - 4)), cx + (int)(cosf(a) * (r + 3)),
                    cy + (int)(sinf(a) * (r + 3)), k == 0 ? C_GLOW : C_EMBER);
    }
    // the phase constellation: the shape the four heads make, always shifting
    for (int k = 0; k < NL; ++k) {
        int j = (k + 1) % NL;
        cv.drawLine(hx[k], hy[k], hx[j], hy[j], k == NL - 1 ? C_CLOT : C_BLOOD);
    }
    for (int k = 0; k < NL; ++k) cv.fillCircle(hx[k], hy[k], 2, k == 0 ? C_GLOW : C_EMBER);
    diamond(cx, cy, 3, s.hold ? C_GLOW : C_RUST);

    // ---- panel
    char b[40];
    int px = 132;
    frame(px - 4, 22, 110, 96, C_CLOT, C_UMBER);
    text("phrase", px, 26, C_DIM);
    // the notes, as they are written; the one reel I is on glows
    for (int i = 0; i < s.len; ++i) {
        int x = px + (i % 4) * 25, y = 37 + (i / 4) * 11;
        bool on = i == s.L[0].idx;
        if (s.phrase[i] == REST) { text("-", x + 4, y, on ? C_GLOW : C_CLOT); continue; }
        int n = s.root + s.phrase[i];
        text(noteName(n), x, y, on ? C_GLOW : C_BONE);
    }
    // how far apart the reels have drifted, in steps
    float lead = 0;
    for (int k = 1; k < NL; ++k) {
        float d = (s.L[0].idx - s.L[k].idx + (float)s.L[0].count / s.L[0].sps - (float)s.L[k].count / s.L[k].sps);
        d -= len * floorf(d / len); // wrap into 0..len
        if (d > lead) lead = d;
    }
    snprintf(b, sizeof b, "drift %.1f", lead);
    text(b, px, 62, C_EMBER);
    snprintf(b, sizeof b, "%s%d", noteName(s.root), s.root / 12 - 1);
    textRight(b, px + 102, 62, C_GLOW);
    meter("tempo", px, 74, px + 40, 62, s.tempo);
    meter("spread", px, 85, px + 40, 62, s.spread);
    text("mode", px, 97, C_DIM);
    text(SCALES[s.scale].name, px + 40, 97, C_BONE);
    text(s.hold ? "HOLD" : "turning", px, 107, s.hold ? C_GLOW : C_EMBER);
    footer("a-k write r new ent align spc hold");
}
} // namespace

const Rite RITE_CANON = {"CANON", "canon", "tape loops drifting out of phase", enter, leave, onKey, draw};
