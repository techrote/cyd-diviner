// GLYPHSONG — type a line and the grimoire sings it back, one syllable per
// letter in a small animalese voice: a e i o u sound as themselves, every
// consonant sings the vowel of its own name. The line is written in Daedric
// runes that wake into latin as they are voiced, and a vowel chart traces
// where the mouth is. Three ways to sing it:
//   speak  quick chirps with a sentence contour (? rises, . falls)
//   chant  slow and joined, each letter a scale degree, over a drone
//   choir  the chant doubled a fifth below
//   sing   word mode: words split into syllables, each held on
//          one note and pronounced (onset consonants, the vowel held — with
//          diphthongs like "day" or "time" — then the coda: n hums, s hisses,
//          t stops). The melody writes itself (seeded by the line, so a line
//          always gets the same tune; . resolves home, ? hangs on the fifth)
//          unless you write it: a digit after a word sets its note (1 = root,
//          8 = octave) and each _ holds the word a beat longer.
//
// keys  type to write   enter sing   bksp erase   tab style
//       fn+;/. pitch   fn+,// pace
#include "../dsp.h"
#include "../rite.h"
#include "../theme.h"
#include "../vox.h"
#include "../dsp_score.h"
#include <atomic>
#include <new>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
constexpr int MAXT = 40;
const char *const STYLE_NAME[] = {"speak", "chant", "choir", "sing"};
constexpr int N_STYLE = 4, ST_SING = 3;
using namespace glyphsong_score;
const int8_t AEOLIAN[7] = {0, 2, 3, 5, 7, 8, 10};

// UI-authored playback revision. Latest complete revision wins; oscillator,
// voice and score cursors are owned solely by the audio callback.
struct Control {
    int style = 0, pitch = 0;
    float pace = 0.5f;
    uint32_t revision = 0;
    char text[MAXT + 1] = {};
    SNote score[MAXN] = {};
    int scoreN = 0;
};
struct Telemetry {
    uint32_t revision = 0;
    int cur = -1, noteLeft = 0, noteLen = 1, letter = -1, vowel = vox::V_SCHWA;
    float amp = 0, f1 = 520, f2 = 1450;
};
struct State : audio_exchange::InternalState {
    char text[MAXT + 1] = "present day. present time?";
    int len = 0, caret = 0;
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
    Control control, audio;
    audio_exchange::Latest<Control> updates;
    audio_exchange::Latest<Telemetry> feedback;
    Telemetry telemetry;
    int cur = -1, noteLeft = 0, noteLen = 1;
    int songWait = 0;
    int pointerMode = 0;
    SNote view[MAXN];           // UI copy for the piano roll
    int viewN = 0;
    // UI
    int trailX[20] = {}, trailY[20] = {};
    int trailN = 0;
    uint32_t litAt[MAXT] = {};
    int lastIdx = -1;
} *S = nullptr;

float baseHz(int pitch) { return 300.f * powf(2.f, pitch / 12.f); }

float degHz(int deg, int pitch) {
    if (deg < 0) deg = 0;
    if (deg > 13) deg = 13;
    return baseHz(pitch) * 0.5f * powf(2.f, (AEOLIAN[deg % 7] + 12 * (deg / 7)) / 12.f);
}
float beatSec() { return 0.55f - S->audio.pace * 0.35f; }

// audio thread: one sample of word mode
void singStep() {
    State &s = *S;
    if (s.noteLeft > 0) s.noteLeft = s.noteLeft - 1;
    if (s.songWait > 0) { --s.songWait; return; }
    if (!s.audio.scoreN) return;
    int i = s.cur + 1;
    if (i >= s.audio.scoreN) { // the line again, after a breath
        s.cur = -1;
        s.songWait = (int)(1.4f * hal::RATE);
        return;
    }
    s.cur = i;
    const SNote &nt = s.audio.score[i];
    int len = (int)(nt.beats * beatSec() * hal::RATE);
    s.noteLen = len;
    s.noteLeft = len;
    s.songWait = len;
    if (nt.v1 == REST) s.v.hush();
    else s.v.sing(nt.onset, nt.v1, nt.v2, nt.coda, degHz(nt.deg, s.audio.pitch), len - len / 12); // a breath between notes
}

// chant: each letter is a scale degree, vowels nearer the root
float chantPitch(char c, int i, void *) {
    (void)i;
    int l = vox::lower(c) - 'a';
    int deg = (l * 3) % 9 - 2; // -2..6: spans a little over an octave
    int o = deg < 0 ? -1 : 0;
    int d = (deg + 7) % 7;
    return baseHz(S->audio.pitch) * 0.5f * powf(2.f, (AEOLIAN[d] + 12 * o) / 12.f);
}

void configure() {
    State &s = *S;
    bool chant = s.audio.style > 0;
    s.v.legato = s.v2.legato = chant ? 1.f : 0.f;
    s.v.vibT = s.v2.vibT = chant ? 0.006f : 0.f;
    s.v.vib.hz(5.2f);
    s.v2.vib.hz(4.7f);
    s.v2.gain = 0.7f;
    s.rd.pitchFn = chant ? chantPitch : nullptr;
    s.rd.hz = baseHz(s.audio.pitch);
    s.rd.sylSec = chant ? 0.42f - s.audio.pace * 0.26f : 0.12f - s.audio.pace * 0.075f;
    s.rd.gapSec = chant ? 1.8f : 1.1f;
    s.rd.loop = true;
    float root = baseHz(s.audio.pitch) * 0.25f;
    s.pedal.hz(root);
    s.pedal5.hz(root * 1.5f);
}

void publish() {
    State &s = *S;
    s.control.style = s.style;
    s.control.pitch = s.pitch;
    s.control.pace = s.pace;
    s.updates.publish(s.control);
}

void sing() {
    State &s = *S;
    s.len = strlen(s.text);
    s.caret = s.len;
    memcpy(s.control.text, s.text, sizeof s.text);
    s.control.scoreN = s.style == ST_SING ? compose(s.text, s.control.score) : 0;
    s.viewN = s.control.scoreN;
    memcpy(s.view, s.control.score, sizeof s.view);
    ++s.control.revision;
    publish();
    s.dirty = false;
}

void render(int16_t *out, int n) {
    State &s = *S;
    Control next;
    if (s.updates.consume(next)) {
        const bool restart = next.revision != s.audio.revision;
        s.audio = next;
        configure();
        if (restart) {
            s.cur = -1; s.noteLeft = 0; s.noteLen = 1;
            s.songWait = hal::RATE / 10;
            s.v.hush(); s.v2.hush();
            s.rd.start(s.audio.style == ST_SING ? "" : s.audio.text);
        }
    }
    bool choir = s.audio.style == 2;
    bool word = s.audio.style == ST_SING;
    for (int i = 0; i < n; ++i) {
        if (word) singStep();
        else s.rd.step(s.v, choir ? &s.v2 : nullptr);
        float x = s.v.tick();
        if (choir) x += s.v2.tick();
        s.pedalAmp += ((s.audio.style ? 0.07f : 0.f) - s.pedalAmp) * 0.0002f;
        if (s.pedalAmp > 0.001f) x += (s.pedal.sine() + s.pedal5.sine() * 0.5f) * s.pedalAmp;
        float wet = s.dl.tap(4090) * 0.45f + s.dl.tap(2900) * 0.3f;
        s.dlLp += (wet - s.dlLp) * 0.25f;
        float fb = s.audio.style ? 0.55f : 0.3f;
        s.dl.put(x + s.dlLp * fb);
        out[i] = dsp::toPcm(x * 0.85f + s.dlLp * (s.audio.style ? 0.5f : 0.25f));
    }
    Telemetry t;
    t.revision = s.audio.revision;
    t.cur = s.cur; t.noteLeft = s.noteLeft; t.noteLen = s.noteLen;
    t.letter = s.rd.idx; t.vowel = s.v.vowel; t.amp = s.v.amp;
    t.f1 = s.v.F[0]; t.f2 = s.v.F[1];
    s.feedback.publish(t);
}

void enter() {
    if (!S) S = new (std::nothrow) State();
    if (!S) { hal::setAudio(nullptr); return; }
    dsp::prepare();
    S->rd.rng.s ^= hal::ms() * 2654435761u;
    sing();
    hal::setAudio(render);
}
void leave() {
    hal::setAudio(nullptr);
    delete S;
    S = nullptr;
}

bool onKey(const hal::Key &k) {
    if (!S) return false;
    State &s = *S;
    const int length = strlen(s.text);
    if (s.caret > length) s.caret = length;
    if (k.ctrl && (k.nav == hal::N_LEFT || k.nav == hal::N_RIGHT)) {
        if (k.nav == hal::N_LEFT && s.caret > 0) --s.caret;
        if (k.nav == hal::N_RIGHT && s.caret < length) ++s.caret;
        s.dirty = true;
        return true;
    }
    // Fn+arrows tune; bare ; . , / remain text punctuation.
    if (k.fn) {
        switch (k.nav) {
        case hal::N_UP: if (s.pitch < 9) s.pitch++; publish(); return true;
        case hal::N_DOWN: if (s.pitch > -9) s.pitch--; publish(); return true;
        case hal::N_RIGHT: s.pace = fminf(1.f, s.pace + 0.1f); publish(); return true;
        case hal::N_LEFT: s.pace = fmaxf(0.f, s.pace - 0.1f); publish(); return true;
        default: break;
        }
    }
    if (k.nav == hal::N_ENTER) { sing(); return true; }
    if (k.nav == hal::N_TAB) { s.style = (s.style + 1) % N_STYLE; sing(); return true; }
    if (k.nav == hal::N_BACK) {
        if (s.caret > 0) {
            memmove(s.text + s.caret - 1, s.text + s.caret, length - s.caret + 1);
            --s.caret;
        }
        s.dirty = true;
        return true;
    }
    if (k.ch >= ' ' && k.ch < 127) {
        int n = length;
        if (!s.dirty && n) { s.text[0] = 0; n = s.caret = 0; }
        if (n < MAXT) {
            memmove(s.text + s.caret + 1, s.text + s.caret, n - s.caret + 1);
            s.text[s.caret++] = k.ch;
        }
        s.dirty = true;
    }
    return true;
}

bool onPointer(const InstrumentPointer &p) {
    if (!S) return false;
    State &s = *S;
    if (!p.down) { s.pointerMode = 0; return true; }
    if (p.start) {
        s.pointerMode = p.x >= 164 && p.y >= 104 && p.y < 122 ? 1 :
                        p.x >= 188 && p.x < 238 && p.y >= 54 && p.y < 103 ? 2 : 0;
        if (!s.pointerMode) return false;
    }
    if (s.pointerMode == 1) s.pace = fminf(1.f, fmaxf(0.f, (p.x - 168) / 66.f));
    else s.pitch = int(fminf(9.f, fmaxf(-9.f, 9.f - (p.y - 54) * (18.f / 48.f))));
    publish();
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

// Word mode's score: one bar per syllable, height = scale degree, the one
// being sung lit and a playhead sweeping through it.
void drawRoll(uint32_t now, int x0, int y0, int w, int h, int note) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    (void)now;
    if (!s.viewN) return;
    int total = 0, start[MAXN];
    for (int i = 0; i < s.viewN; ++i) { start[i] = total; total += s.view[i].beats; }
    int ppb = total ? w / total : w; // pixels per beat
    if (ppb > 18) ppb = 18;
    if (ppb < 7) ppb = 7;
    // playhead in beats (whole notes done + progress through this one)
    float head = 0;
    if (note >= 0 && note < s.viewN) {
        float frac = 1.f - (float)s.telemetry.noteLeft / (s.telemetry.noteLen > 0 ? s.telemetry.noteLen : 1);
        head = start[note] + s.view[note].beats * fminf(1.f, fmaxf(0.f, frac));
    }
    int scroll = 0; // keep the head a third of the way in on long lines
    if (total * ppb > w) {
        scroll = (int)(head * ppb) - w / 3;
        if (scroll < 0) scroll = 0;
        if (scroll > total * ppb - w) scroll = total * ppb - w;
    }
    // staff: the root and its octave
    auto yOf = [&](int deg) { return y0 + h - 3 - deg * (h - 6) / 13; };
    cv.drawFastHLine(x0, yOf(0) + 1, w, C_DUSK);
    cv.drawFastHLine(x0, yOf(7) + 1, w, C_DUSK);
    for (int i = 0; i < s.viewN; ++i) {
        const SNote &nt = s.view[i];
        if (nt.v1 == REST) continue;
        int a = x0 + start[i] * ppb - scroll, b = a + nt.beats * ppb - 2;
        if (b < x0 || a > x0 + w) continue;
        if (a < x0) a = x0;
        if (b > x0 + w) b = x0 + w;
        int y = yOf(nt.deg);
        uint16_t c = i == note ? C_GLOW : i < note ? C_BLOOD : C_RUST;
        cv.fillRect(a, y, b - a, 3, c);
        if (i == note) cv.drawRect(a - 1, y - 1, b - a + 2, 5, C_EMBER);
    }
    if (note >= 0) {
        int hx = x0 + (int)(head * ppb) - scroll;
        if (hx >= x0 && hx <= x0 + w) cv.drawFastVLine(hx, y0, h, blend(C_BONE, C_VOID, 140));
    }
}

void draw(uint32_t now) {
    if (!S) { starved("GLYPHSONG", "glyphsong"); return; }
    State &s = *S;
    s.feedback.consume(s.telemetry);
    const bool current = s.telemetry.revision == s.control.revision;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("GLYPHSONG", "glyphsong");

    bool word = s.style == ST_SING;
    // in word mode the "current letter" is the whole syllable being sung
    int cur = -1, spanA = -1, spanB = -1, note = -1;
    if (word && !s.dirty && current) {
        note = s.telemetry.cur;
        if (note >= 0 && note < s.viewN && s.view[note].v1 != REST) {
            spanA = s.view[note].at;
            spanB = spanA + s.view[note].len;
            cur = spanA;
        }
        if (note != s.lastIdx && spanA >= 0)
            for (int i = spanA; i < spanB && i < MAXT; ++i) s.litAt[i] = now;
        s.lastIdx = note;
    } else {
        cur = s.dirty || !current ? -1 : s.telemetry.letter;
        if (cur >= 0 && cur != s.lastIdx && cur < MAXT) s.litAt[cur] = now;
        s.lastIdx = cur;
        spanA = cur;
        spanB = cur + 1;
    }

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
    int focus = s.dirty ? s.caret : cur >= 0 ? cur : n - 1, fx = 0;
    for (int i = 0; i < focus && i < n; ++i) fx += widths[i];
    int x = total <= 228 ? 120 - total / 2 : 120 - fx;
    if (total > 228 && x > 6) x = 6;
    if (total > 228 && x + total < 234) x = 234 - total;
    int caretX = x;
    for (int i = 0; i < s.caret && i < n; ++i) caretX += widths[i];
    for (int i = 0; i < n; ++i) {
        char c = s.text[i];
        int w = widths[i];
        if (x + w >= 0 && x < 240) {
            uint32_t age = now - s.litAt[i];
            bool fresh = s.litAt[i] && age < 1400;
            float wake = fresh ? 1.f - age / 1400.f : 0.f;
            if (vox::isLetter(c)) {
                char g[2] = {(char)(c & ~32), 0};
                bool on = i >= spanA && i < spanB;
                uint16_t col = on ? C_GLOW : blend(C_EMBER, C_BLOOD, (uint8_t)(wake * 255));
                glyphs(DAEDRIC_S, g, x, 24 - (on ? 2 : 0), col);
                char l[2] = {vox::lower(c), 0};
                if (fresh) text(l, x + w / 2 - 4, 43, blend(C_BONE, C_VOID, (uint8_t)(wake * 255)));
            } else if (c != ' ') {
                char p[2] = {c, 0};
                text(p, x, 32, C_DIM);
            }
        }
        x += w;
    }
    if (s.dirty && (now / 400) & 1) cv.fillRect(caretX + 1, 24, 2, 16, C_EMBER); // cursor while writing

    // ---- vowel chart (or, in word mode, the score as a piano roll)
    const int cx0 = 10, cy0 = 60, cw = 104, ch = 50;
    frame(cx0 - 6, cy0 - 6, cw + 12, ch + 12, C_CLOT, C_UMBER);
    if (word) {
        drawRoll(now, cx0, cy0, cw, ch, note);
    } else {
    // the vowel trapezoid
    int vx[5], vy[5];
    for (int i = 0; i < 5; ++i) chartXY(vox::VOWEL[i].f1, vox::VOWEL[i].f2, cx0, cy0, cw, ch, vx[i], vy[i]);
    cv.drawLine(vx[vox::V_I], vy[vox::V_I], vx[vox::V_U], vy[vox::V_U], C_DUSK);
    cv.drawLine(vx[vox::V_I], vy[vox::V_I], vx[vox::V_E], vy[vox::V_E], C_DUSK);
    cv.drawLine(vx[vox::V_E], vy[vox::V_E], vx[vox::V_A], vy[vox::V_A], C_DUSK);
    cv.drawLine(vx[vox::V_A], vy[vox::V_A], vx[vox::V_O], vy[vox::V_O], C_DUSK);
    cv.drawLine(vx[vox::V_O], vy[vox::V_O], vx[vox::V_U], vy[vox::V_U], C_DUSK);
    int vw = s.telemetry.vowel;
    for (int i = 0; i < 5; ++i) {
        char l[2] = {"aeiou"[i], 0};
        bool on = i == vw && s.telemetry.amp > 0.05f;
        diamond(vx[i], vy[i], on ? 3 : 1, on ? C_GLOW : C_RUST, on);
        text(l, vx[i] + (vx[i] < cx0 + cw / 2 ? -8 : 4), vy[i] - 4, on ? C_BONE : C_DIM);
    }
    // the mouth: where the formants are right now, with a fading trail
    int mx, my;
    chartXY(s.telemetry.f1, s.telemetry.f2, cx0, cy0, cw, ch, mx, my);
    if (s.telemetry.amp > 0.03f) {
        s.trailX[s.trailN % 20] = mx;
        s.trailY[s.trailN % 20] = my;
        s.trailN++;
    }
    int tn = s.trailN < 20 ? s.trailN : 20;
    for (int i = 1; i < tn; ++i) {
        int a = (s.trailN - i) % 20, b = (s.trailN - i - 1) % 20;
        cv.drawLine(s.trailX[a], s.trailY[a], s.trailX[b], s.trailY[b], ramp((uint8_t)(220 - i * 10)));
    }
    float loud = fminf(1.f, s.telemetry.amp * 1.2f);
    cv.fillCircle(mx, my, 1 + (int)(loud * 3), blend(C_GLOW, C_EMBER, (uint8_t)(loud * 255)));
    }

    // ---- panel: the rune being voiced, large
    int px = 132;
    frame(px - 4, 54, 110, 64, C_CLOT, C_UMBER);
    char cc = cur >= 0 && cur < n ? s.text[cur] : 0;
    if (word && spanA >= 0) {
        // the syllable, its vowel and its note
        char syl[12] = {};
        int sl = spanB - spanA < 11 ? spanB - spanA : 11;
        for (int i = 0; i < sl; ++i) syl[i] = vox::lower(s.text[spanA + i]);
        text(syl, px, 60, C_GLOW, sl <= 8 ? 2 : 1);
        const SNote &nt = s.view[note];
        char vn[12];
        snprintf(vn, sizeof vn, nt.v2 != nt.v1 ? "%s>%s" : "%s", vox::VOWEL_NAME[nt.v1], vox::VOWEL_NAME[nt.v2]);
        text(vn, px, 80, C_EMBER);
        int midi = (int)roundf(69.f + 12.f * log2f(degHz(nt.deg, s.pitch) / 440.f));
        char nb[24];
        snprintf(nb, sizeof nb, "%s%d", noteName(midi), midi / 12 - 1);
        textRight(nb, px + 102, 80, C_BONE);
    } else if (vox::isLetter(cc)) {
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
    footer(s.dirty ? "enter: sing it   bksp erase"
                   : word ? "word3 = note  word_ = hold  tab" : "type a line  ent sing  tab style", "tap text / drag pitch + pace");
}
} // namespace

const Rite RITE_GLYPHSONG = {"GLYPHSONG", "glyphsong", "type a line; it is sung back in runes", enter, leave, onKey, draw, onPointer};
