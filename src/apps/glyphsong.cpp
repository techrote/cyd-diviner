// GLYPHSONG — type a line and the grimoire sings it back, one syllable per
// letter in a small animalese voice: a e i o u sound as themselves, every
// consonant sings the vowel of its own name. The line is written in Daedric
// runes that wake into latin as they are voiced, and a vowel chart traces
// where the mouth is. Three ways to sing it:
//   speak  quick chirps with a sentence contour (? rises, . falls)
//   chant  slow and joined, each letter a scale degree, over a drone
//   choir  the chant doubled a fifth below
//   sing   word mode, a tiny UTAU: words split into syllables, each held on
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
#include <atomic>
#include <new>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
constexpr int MAXT = 40;
const char *const STYLE_NAME[] = {"speak", "chant", "choir", "sing"};
constexpr int N_STYLE = 4, ST_SING = 3;
constexpr int MAXN = 48;
constexpr uint8_t REST = 255;

// one sung syllable (or a rest) of the word-mode score
struct SNote {
    uint8_t onset, coda, v1, v2; // v1 == REST: silence
    int8_t deg;                  // scale degree, 0 = root (two octaves up to 13)
    uint8_t beats;
    uint8_t at, len;             // span of the line this syllable came from
    uint8_t fixed;               // the writer chose this note: phrase endings leave it alone
};

// ---- word mode: text -> syllables -> score ------------------------------
bool isV(const char *w, int i, int n) {
    char c = w[i] | 32;
    if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') return true;
    return c == 'y' && i > 0 && !(i + 1 < n && isV(w, i + 1, n)); // y as a vowel, not "yes"
}

// what a vowel group sounds like: (first vowel, glide-to vowel).
// magicE: a silent final e follows (time, make); rE: and the consonant
// before it is r (are, more, fire), which colours it differently;
// atEnd: the group ends the word (you, me).
void vowelSound(const char *g, int n, bool magicE, bool rE, bool atEnd, uint8_t &v1, uint8_t &v2) {
    using namespace vox;
    char a = g[0] | 32, b = n > 1 ? g[1] | 32 : 0;
    auto set = [&](int x, int y) { v1 = x; v2 = y; };
    if (b) {
        if (a == 'o' && b == 'u' && atEnd) return set(V_U, V_U); // you
        if (a == 'o' && b == 'u') return set(V_A, V_U);           // out, sound
        if ((a == 'e' && (b == 'e' || b == 'a' || b == 'y')) || (a == 'i' && b == 'e')) return set(V_I, V_I);
        if (a == 'o' && b == 'o') return set(V_U, V_U);
        if ((a == 'o' && (b == 'u' || b == 'w'))) return set(V_O, V_U);
        if ((a == 'a' || a == 'e') && (b == 'i' || b == 'y')) return set(V_E, V_I);
        if (a == 'o' && (b == 'i' || b == 'y')) return set(V_O, V_I);
        if (a == 'a' && (b == 'u' || b == 'w')) return set(V_O, V_O);
        if (a == 'o' && (b == 'a' || b == 'e')) return set(V_O, V_O);
        if ((a == 'u' && b == 'e') || (a == 'e' && b == 'w')) return set(V_U, V_U);
    }
    if (magicE && rE) { // are, there, fire, more, sure
        if (a == 'i' || a == 'y') return set(V_A, V_I);
        return set(a == 'a' ? V_A : a == 'e' ? V_E : a == 'o' ? V_O : V_U, a == 'a' ? V_A : a == 'e' ? V_E : a == 'o' ? V_O : V_U);
    }
    if (magicE) { // the silent e changes the vowel before it: time, make, home
        if (a == 'a') return set(V_E, V_I);
        if (a == 'i' || a == 'y') return set(V_A, V_I);
        if (a == 'o') return set(V_O, V_U);
        if (a == 'u') return set(V_U, V_U);
        if (a == 'e') return set(V_I, V_I);
    }
    if (atEnd && a == 'e') return set(V_I, V_I); // me, he, we, be
    switch (a) {
    case 'a': return set(V_A, V_A);
    case 'e': return set(V_E, V_E);
    case 'i': return set(V_I, V_I);
    case 'o': return set(V_O, V_O);
    case 'u': return set(V_U, V_U);
    default: return set(V_I, V_I); // y
    }
}

// Maximal onset: the next syllable starts with the longest tail of the
// consonant cluster that can begin an English syllable (str, pl, th...),
// the rest closes the syllable before (card-pu-ter, lis-ten, sin-ging).
bool validOnset(const char *c, int n) {
    if (n == 1) return true;
    static const char *const OK[] = {"bl", "br", "cl", "cr", "dr", "fl", "fr", "gl", "gr", "pl", "pr", "sc", "sk",
                                     "sl", "sm", "sn", "sp", "st", "sw", "tr", "tw", "th", "sh", "ch", "ph", "wh",
                                     "str", "spr", "spl", "scr", "thr", "shr", nullptr};
    for (int i = 0; OK[i]; ++i) {
        int l = strlen(OK[i]);
        if (l != n) continue;
        bool m = true;
        for (int k = 0; k < l; ++k) m &= (c[k] | 32) == OK[i][k];
        if (m) return true;
    }
    return false;
}
int onsetSplit(const char *w, int cs, int ce) {
    for (int l = ce - cs < 3 ? ce - cs : 3; l > 1; --l)
        if (validOnset(w + ce - l, l)) return ce - l;
    return ce - cs >= 1 ? ce - 1 : ce;
}

uint8_t clusterMask(const char *c, int n) {
    uint8_t m = 0;
    for (int i = 0; i < n; ++i) {
        char x = c[i] | 32;
        if (x == 'h' && i > 0 && strchr("sctp", c[i - 1] | 32)) { m |= vox::M_FRIC; continue; } // sh ch th ph
        m |= vox::consMask(c[i]);
    }
    return m;
}

// Split the line into syllables and give each a note. Returns the count.
int compose(const char *t, SNote *out) {
    int n = 0, len = strlen(t);
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; ++i) h = (h ^ (uint8_t)t[i]) * 16777619u;
    auto rnd = [&]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return h; };
    int deg = 2 + rnd() % 3;
    int i = 0;
    while (i < len && n < MAXN) {
        char c = t[i];
        if (!vox::isLetter(c)) {
            if (c == ',' || c == '.' || c == '?' || c == '!' || c == ';') {
                // the phrase that just ended resolves: home, the fifth, or up high
                int k = n - 1;
                while (k >= 0 && out[k].v1 == REST) --k;
                if (k >= 0 && c != ',') {
                    if (!out[k].fixed) out[k].deg = c == '?' ? 4 : c == '!' ? 7 : 0;
                    out[k].beats += 1;
                }
                SNote r = {0, 0, REST, REST, 0, (uint8_t)(c == ',' ? 1 : 2), (uint8_t)i, 1, 0};
                out[n++] = r;
            }
            ++i;
            continue;
        }
        // one word
        int ws = i;
        while (i < len && vox::isLetter(t[i])) ++i;
        int we = i, wn = we - ws;
        const char *w = t + ws;
        int userDeg = -1, hold = 0;
        while (i < len && t[i] >= '1' && t[i] <= '9') userDeg = t[i++] - '1';
        while (i < len && t[i] == '_') { ++hold; ++i; }
        // vowel groups
        int gs[16], ge[16], ng = 0;
        for (int k = 0; k < wn && ng < 16;) {
            if (isV(w, k, wn)) {
                gs[ng] = k;
                while (k < wn && isV(w, k, wn)) ++k;
                ge[ng++] = k;
            } else ++k;
        }
        bool magicE = false, rE = false;
        if (ng > 1 && (w[wn - 1] | 32) == 'e' && ge[ng - 1] == wn && gs[ng - 1] == wn - 1 && !isV(w, wn - 2, wn)) {
            --ng; // silent final e (house, time)...
            magicE = ge[ng - 1] - gs[ng - 1] == 1; // ...which changes a lone vowel before it
            rE = (w[wn - 2] | 32) == 'r';
        }
        if (!ng) { gs[0] = 0; ge[0] = wn; ng = 1; } // "hmm", "brr": hum it
        bool isThe = wn == 3 && (w[0] | 32) == 't' && (w[1] | 32) == 'h' && (w[2] | 32) == 'e';
        // where each syllable starts: maximal onset between vowel groups
        int split[16];
        split[0] = 0;
        for (int g = 1; g < ng; ++g) split[g] = onsetSplit(w, ge[g - 1], gs[g]);
        int first = n;
        for (int g = 0; g < ng && n < MAXN; ++g) {
            int end = g + 1 < ng ? split[g + 1] : wn;
            SNote s;
            s.onset = clusterMask(w + split[g], gs[g] - split[g]);
            s.coda = clusterMask(w + ge[g], (g + 1 < ng ? split[g + 1] : (magicE ? wn - 1 : wn)) - ge[g]);
            vowelSound(w + gs[g], ge[g] - gs[g], magicE && g == ng - 1, rE, g == ng - 1 && ge[g] == wn, s.v1, s.v2);
            if (isThe) s.v1 = s.v2 = vox::V_SCHWA; // "thuh", as it is sung
            // the melody walks mostly by step
            int step = (int)(rnd() % 7) - 3;
            step = step == -3 ? -2 : step == 3 ? 2 : step;
            deg += step;
            if (deg < 0) deg = 1;
            if (deg > 9) deg = 8;
            s.deg = deg;
            s.beats = 1;
            s.at = ws + split[g];
            s.len = end - split[g];
            s.fixed = 0;
            out[n++] = s;
        }
        if (userDeg >= 0) {
            deg = userDeg;
            for (int k = first; k < n; ++k) {
                out[k].deg = userDeg + (k - first); // later syllables climb
                out[k].fixed = 1;
            }
            deg = out[n - 1].deg;
        }
        if (n > first) out[n - 1].beats += hold;
    }
    // the line's last note resolves home if no punctuation did
    int k = n - 1;
    while (k >= 0 && out[k].v1 == REST) --k;
    if (k >= 0 && k == n - 1) {
        if (!out[k].fixed) out[k].deg = 0;
        out[k].beats += 1;
    }
    return n;
}
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
    // word mode: the UI composes into pend[], the audio thread takes it
    SNote pend[MAXN], song[MAXN];
    std::atomic<int> pendN{-1};
    int songN = 0;
    volatile int cur = -1;      // note being sung
    volatile int noteLeft = 0, noteLen = 1;
    int songWait = 0;
    SNote view[MAXN];           // UI copy for the piano roll
    int viewN = 0;
    // UI
    int trailX[20] = {}, trailY[20] = {};
    int trailN = 0;
    uint32_t litAt[MAXT] = {};
    int lastIdx = -1;
} *S = nullptr;

float baseHz() { return 300.f * powf(2.f, S->pitch / 12.f); }

float degHz(int deg) {
    if (deg < 0) deg = 0;
    if (deg > 13) deg = 13;
    return baseHz() * 0.5f * powf(2.f, (AEOLIAN[deg % 7] + 12 * (deg / 7)) / 12.f);
}
float beatSec() { return 0.55f - S->pace * 0.35f; }

// audio thread: one sample of word mode
void singStep() {
    State &s = *S;
    int pn = s.pendN.load(std::memory_order_acquire);
    if (pn >= 0) {
        memcpy(s.song, s.pend, sizeof(SNote) * pn);
        s.songN = pn;
        s.cur = -1;
        s.songWait = hal::RATE / 10;
        s.pendN.compare_exchange_strong(pn, -1, std::memory_order_acq_rel);
    }
    if (s.noteLeft > 0) s.noteLeft = s.noteLeft - 1;
    if (s.songWait > 0) { --s.songWait; return; }
    if (!s.songN) return;
    int i = s.cur + 1;
    if (i >= s.songN) { // the line again, after a breath
        s.cur = -1;
        s.songWait = (int)(1.4f * hal::RATE);
        return;
    }
    s.cur = i;
    const SNote &nt = s.song[i];
    int len = (int)(nt.beats * beatSec() * hal::RATE);
    s.noteLen = len;
    s.noteLeft = len;
    s.songWait = len;
    if (nt.v1 == REST) s.v.hush();
    else s.v.sing(nt.onset, nt.v1, nt.v2, nt.coda, degHz(nt.deg), len - len / 12); // a breath between notes
}

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
    s.v.vibT = s.v2.vibT = chant ? 0.006f : 0.f;
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
    State &s = *S;
    s.len = strlen(s.text);
    configure();
    if (s.style == ST_SING) {
        s.rd.stop();
        s.viewN = compose(s.text, s.view);
        memcpy(s.pend, s.view, sizeof(SNote) * s.viewN);
        s.pendN.store(s.viewN, std::memory_order_release);
    } else {
        s.pendN.store(0, std::memory_order_release); // silence word mode
        s.rd.post(s.text, s.len);
    }
    s.dirty = false;
}

void render(int16_t *out, int n) {
    State &s = *S;
    bool choir = s.style == 2;
    bool word = s.style == ST_SING;
    for (int i = 0; i < n; ++i) {
        if (word) singStep();
        else s.rd.step(s.v, choir ? &s.v2 : nullptr);
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
    if (k.nav == hal::N_TAB) { s.style = (s.style + 1) % N_STYLE; sing(); return true; }
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
        float frac = 1.f - (float)s.noteLeft / (s.noteLen > 0 ? s.noteLen : 1);
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
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("GLYPHSONG", "glyphsong");

    bool word = s.style == ST_SING;
    // in word mode the "current letter" is the whole syllable being sung
    int cur = -1, spanA = -1, spanB = -1, note = -1;
    if (word && !s.dirty) {
        note = s.cur;
        if (note >= 0 && note < s.viewN && s.view[note].v1 != REST) {
            spanA = s.view[note].at;
            spanB = spanA + s.view[note].len;
            cur = spanA;
        }
        if (note != s.lastIdx && spanA >= 0)
            for (int i = spanA; i < spanB && i < MAXT; ++i) s.litAt[i] = now;
        s.lastIdx = note;
    } else {
        cur = s.dirty ? -1 : s.rd.idx;
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
    if (s.dirty && (now / 400) & 1) cv.fillRect(x + 1, 24, 2, 16, C_EMBER); // cursor while writing

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
        int midi = (int)roundf(69.f + 12.f * log2f(degHz(nt.deg) / 440.f));
        char nb[8];
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
                   : word ? "word3 = note  word_ = hold  tab" : "type a line  ent sing  tab style");
}
} // namespace

const Rite RITE_GLYPHSONG = {"GLYPHSONG", "glyphsong", "type a line; it is sung back in runes", enter, leave, onKey, draw};
