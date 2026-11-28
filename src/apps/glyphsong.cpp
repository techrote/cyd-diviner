// GLYPHSONG — type a line and the grimoire sings it back, one syllable per
// letter in a small animalese voice: a e i o u sound as themselves, every
// consonant sings the vowel of its own name. The line is written in Daedric
// runes that wake into latin as they are voiced, and a vowel chart traces
// where the mouth is. Three ways to sing it:
//   speak  quick chirps with a sentence contour (? rises, . falls)
//   chant  slow and joined, each letter a scale degree, over a drone
//   choir  the chant doubled a fifth below
//
// keys  type to write   enter sing   bksp erase   tab style
//       fn+;/. pitch   fn+,// pace
#include "../dsp.h"
#include "../rite.h"
#include "../theme.h"
#include "../vox.h"
#include <new>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
constexpr int MAXT = 40;
const char *const STYLE_NAME[] = {"speak", "chant", "choir"};
const int8_t AEOLIAN[7] = {0, 2, 3, 5, 7, 8, 10};

struct State {
    char text[MAXT + 1] = "present day. present time?";
    int len = 0;
    int style = 0;
    int pitch = 0;      // semitones around the base
    float pace = 0.5f;  // 0..1
    vox::Voice v, v2;
    vox::Reader rd;
    dsp::Osc pedal, pedal5;
    float pedalAmp = 0;
    dsp::Delay<4096> dl;
    float dlLp = 0;
    bool dirty = false; // text changed since it was last sung
    // UI
    int trailX[20] = {}, trailY[20] = {};
    int trailN = 0;
    uint32_t litAt[MAXT] = {};
    int lastIdx = -1;
} *S = nullptr;

float baseHz() { return 300.f * powf(2.f, S->pitch / 12.f); }

// chant: each letter is a scale degree, vowels nearer the root
float chantPitch(char c, int i, void *) {
    (void)i;
    int l = vox::lower(c) - 'a';
    int deg = (l * 3) % 9 - 2; // -2..6: spans a little over an octave
    int o = deg < 0 ? -1 : 0;
    int d = (deg + 7) % 7;
    return baseHz() * 0.5f * powf(2.f, (AEOLIAN[d] + 12 * o) / 12.f);
}

void configure() {
    State &s = *S;
    bool chant = s.style > 0;
    s.v.legato = s.v2.legato = chant ? 1.f : 0.f;
    s.v.vibDepth = s.v2.vibDepth = chant ? 0.006f : 0.f;
    s.v.vib.hz(5.2f);
    s.v2.vib.hz(4.7f);
    s.v2.gain = 0.7f;
    s.rd.pitchFn = chant ? chantPitch : nullptr;
    s.rd.hz = baseHz();
    s.rd.sylSec = chant ? 0.42f - s.pace * 0.26f : 0.12f - s.pace * 0.075f;
    s.rd.gapSec = chant ? 1.8f : 1.1f;
    s.rd.loop = true;
    float root = baseHz() * 0.25f;
    s.pedal.hz(root);
    s.pedal5.hz(root * 1.5f);
}

void sing() {
    S->len = strlen(S->text);
    configure();
    S->rd.post(S->text, S->len);
    S->dirty = false;
}

void render(int16_t *out, int n) {
    State &s = *S;
    bool choir = s.style == 2;
    for (int i = 0; i < n; ++i) {
        s.rd.step(s.v, choir ? &s.v2 : nullptr);
        float x = s.v.tick();
        if (choir) x += s.v2.tick();
        s.pedalAmp += ((s.style ? 0.07f : 0.f) - s.pedalAmp) * 0.0002f;
        if (s.pedalAmp > 0.001f) x += (s.pedal.sine() + s.pedal5.sine() * 0.5f) * s.pedalAmp;
        float wet = s.dl.tap(4090) * 0.45f + s.dl.tap(2900) * 0.3f;
        s.dlLp += (wet - s.dlLp) * 0.25f;
        float fb = s.style ? 0.55f : 0.3f;
        s.dl.put(x + s.dlLp * fb);
        out[i] = dsp::toPcm(x * 0.85f + s.dlLp * (s.style ? 0.5f : 0.25f));
    }
}

void enter() {
    if (!S) S = new (std::nothrow) State();
    if (!S) return;
    S->rd.rng.s ^= hal::ms() * 2654435761u;
    sing();
    hal::setAudio(render);
}
void leave() {
    delete S;
    S = nullptr;
}

bool onKey(const hal::Key &k) {
    if (!S) return false;
    State &s = *S;
    // Fn+arrows tune; bare ; . , / are letters of the line
    if (k.fn) {
        switch (k.nav) {
        case hal::N_UP: if (s.pitch < 9) s.pitch++; configure(); return true;
        case hal::N_DOWN: if (s.pitch > -9) s.pitch--; configure(); return true;
        case hal::N_RIGHT: s.pace = fminf(1.f, s.pace + 0.1f); configure(); return true;
        case hal::N_LEFT: s.pace = fmaxf(0.f, s.pace - 0.1f); configure(); return true;
        default: break;
        }
    }
    if (k.nav == hal::N_ENTER) { sing(); return true; }
    if (k.nav == hal::N_TAB) { s.style = (s.style + 1) % 3; configure(); return true; }
    if (k.nav == hal::N_BACK) {
        int l = strlen(s.text);
        if (l) s.text[l - 1] = 0;
        s.dirty = true;
        return true;
    }
    if (k.ch >= ' ' && k.ch < 127) {
        int l = strlen(s.text);
        if (s.dirty == false && l) { s.text[0] = 0; l = 0; } // first key after singing starts a new line
        if (l < MAXT) { s.text[l] = k.ch; s.text[l + 1] = 0; }
        s.dirty = true;
    }
    return true;
}

// the vowel chart: F2 runs right-to-left (front vowels on the left, as
// linguists draw it), F1 top-to-bottom (close vowels up top)
void chartXY(float f1, float f2, int x0, int y0, int w, int h, int &x, int &y) {
    float tx = (2500.f - f2) / (2500.f - 700.f);
    float ty = (f1 - 280.f) / (900.f - 280.f);
    x = x0 + (int)(fminf(1.f, fmaxf(0.f, tx)) * w);
    y = y0 + (int)(fminf(1.f, fmaxf(0.f, ty)) * h);
}

void draw(uint32_t now) {
    if (!S) { starved("GLYPHSONG", "glyphsong"); return; }
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("GLYPHSONG", "glyphsong");

    int cur = s.dirty ? -1 : s.rd.idx;
    if (cur >= 0 && cur != s.lastIdx && cur < MAXT) s.litAt[cur] = now;
    s.lastIdx = cur;

    // ---- the line, in runes; letters wake into latin as they are sung
    int n = strlen(s.text);
    int widths[MAXT + 1], total = 0;
    for (int i = 0; i < n; ++i) {
        char c = s.text[i];
        char g[2] = {(char)(vox::isLetter(c) ? (c & ~32) : 0), 0};
        widths[i] = g[0] ? glyphWidth(DAEDRIC_S, g) + 3 : 6;
        total += widths[i];
    }
    // scroll so the voiced (or last typed) letter stays on screen
    int focus = cur >= 0 ? cur : n - 1, fx = 0;
    for (int i = 0; i < focus && i < n; ++i) fx += widths[i];
    int x = total <= 228 ? 120 - total / 2 : 120 - fx;
    if (total > 228 && x > 6) x = 6;
    if (total > 228 && x + total < 234) x = 234 - total;
    for (int i = 0; i < n; ++i) {
        char c = s.text[i];
        int w = widths[i];
        if (x + w >= 0 && x < 240) {
            uint32_t age = now - s.litAt[i];
            bool fresh = s.litAt[i] && age < 1400;
            float wake = fresh ? 1.f - age / 1400.f : 0.f;
            if (vox::isLetter(c)) {
                char g[2] = {(char)(c & ~32), 0};
                uint16_t col = i == cur ? C_GLOW : blend(C_EMBER, C_BLOOD, (uint8_t)(wake * 255));
                glyphs(DAEDRIC_S, g, x, 24 - (i == cur ? 2 : 0), col);
                char l[2] = {vox::lower(c), 0};
                if (fresh) text(l, x + w / 2 - 4, 43, blend(C_BONE, C_VOID, (uint8_t)(wake * 255)));
            } else if (c != ' ') {
                char p[2] = {c, 0};
                text(p, x, 32, C_DIM);
            }
        }
        x += w;
    }
    if (s.dirty && (now / 400) & 1) cv.fillRect(x + 1, 24, 2, 16, C_EMBER); // cursor while writing

    // ---- vowel chart
    const int cx0 = 10, cy0 = 60, cw = 104, ch = 50;
    frame(cx0 - 6, cy0 - 6, cw + 12, ch + 12, C_CLOT, C_UMBER);
    // the vowel trapezoid
    int vx[5], vy[5];
    for (int i = 0; i < 5; ++i) chartXY(vox::VOWEL[i].f1, vox::VOWEL[i].f2, cx0, cy0, cw, ch, vx[i], vy[i]);
    cv.drawLine(vx[vox::V_I], vy[vox::V_I], vx[vox::V_U], vy[vox::V_U], C_DUSK);
    cv.drawLine(vx[vox::V_I], vy[vox::V_I], vx[vox::V_E], vy[vox::V_E], C_DUSK);
    cv.drawLine(vx[vox::V_E], vy[vox::V_E], vx[vox::V_A], vy[vox::V_A], C_DUSK);
    cv.drawLine(vx[vox::V_A], vy[vox::V_A], vx[vox::V_O], vy[vox::V_O], C_DUSK);
    cv.drawLine(vx[vox::V_O], vy[vox::V_O], vx[vox::V_U], vy[vox::V_U], C_DUSK);
    int vw = s.v.vowel;
    for (int i = 0; i < 5; ++i) {
        char l[2] = {"aeiou"[i], 0};
        bool on = i == vw && s.v.amp > 0.05f;
        diamond(vx[i], vy[i], on ? 3 : 1, on ? C_GLOW : C_RUST, on);
        text(l, vx[i] + (vx[i] < cx0 + cw / 2 ? -8 : 4), vy[i] - 4, on ? C_BONE : C_DIM);
    }
    // the mouth: where the formants are right now, with a fading trail
    int mx, my;
    chartXY(s.v.F[0], s.v.F[1], cx0, cy0, cw, ch, mx, my);
    if (s.v.amp > 0.03f) {
        s.trailX[s.trailN % 20] = mx;
        s.trailY[s.trailN % 20] = my;
        s.trailN++;
    }
    int tn = s.trailN < 20 ? s.trailN : 20;
    for (int i = 1; i < tn; ++i) {
        int a = (s.trailN - i) % 20, b = (s.trailN - i - 1) % 20;
        cv.drawLine(s.trailX[a], s.trailY[a], s.trailX[b], s.trailY[b], ramp((uint8_t)(220 - i * 10)));
    }
    float loud = fminf(1.f, s.v.amp * 1.2f);
    cv.fillCircle(mx, my, 1 + (int)(loud * 3), blend(C_GLOW, C_EMBER, (uint8_t)(loud * 255)));

    // ---- panel: the rune being voiced, large
    int px = 132;
    frame(px - 4, 54, 110, 64, C_CLOT, C_UMBER);
    char cc = cur >= 0 && cur < n ? s.text[cur] : 0;
    if (vox::isLetter(cc)) {
        char g[2] = {(char)(cc & ~32), 0};
        glyphsCentre(DAEDRIC_L, g, px + 22, 60, C_GLOW);
        char b[16];
        snprintf(b, sizeof b, "%c", vox::lower(cc));
        text(b, px + 52, 62, C_BONE, 2);
        text(vox::VOWEL_NAME[vox::vowelOf(cc)], px + 72, 66, C_EMBER);
    } else {
        glyphsCentre(DAEDRIC_L, "G", px + 22, 60, C_CLOT);
        text(s.dirty ? "writing" : "breath", px + 52, 66, C_DIM);
    }
    text(STYLE_NAME[s.style], px, 98, C_BONE);
    char b[24];
    snprintf(b, sizeof b, "%+d", s.pitch);
    textRight(b, px + 102, 98, C_EMBER);
    meter("pace", px, 108, px + 36, 66, s.pace);
    footer(s.dirty ? "enter: sing it   bksp erase" : "type a line  ent sing  tab style");
}
} // namespace

const Rite RITE_GLYPHSONG = {"GLYPHSONG", "glyphsong", "type a line; it is sung back in runes", enter, leave, onKey, draw};
