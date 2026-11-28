// GLYPHSONG deterministic bounded syllable/score composer. MIT; see NOTICE.md.
#pragma once
#include "vox.h"
#include <string.h>
namespace glyphsong_score {
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
inline bool isV(const char *w, int i, int n) {
    char c = w[i] | 32;
    if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') return true;
    return c == 'y' && i > 0 && !(i + 1 < n && isV(w, i + 1, n)); // y as a vowel, not "yes"
}

// what a vowel group sounds like: (first vowel, glide-to vowel).
// magicE: a silent final e follows (time, make); rE: and the consonant
// before it is r (are, more, fire), which colours it differently;
// atEnd: the group ends the word (you, me).
inline void vowelSound(const char *g, int n, bool magicE, bool rE, bool atEnd, uint8_t &v1, uint8_t &v2) {
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
inline bool validOnset(const char *c, int n) {
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
inline int onsetSplit(const char *w, int cs, int ce) {
    for (int l = ce - cs < 3 ? ce - cs : 3; l > 1; --l)
        if (validOnset(w + ce - l, l)) return ce - l;
    return ce - cs >= 1 ? ce - 1 : ce;
}

inline uint8_t clusterMask(const char *c, int n) {
    uint8_t m = 0;
    for (int i = 0; i < n; ++i) {
        char x = c[i] | 32;
        if (x == 'h' && i > 0 && strchr("sctp", c[i - 1] | 32)) { m |= vox::M_FRIC; continue; } // sh ch th ph
        m |= vox::consMask(c[i]);
    }
    return m;
}

// Split the line into syllables and give each a note. Returns the count.
inline int compose(const char *t, SNote *out) {
    int n = 0, len = 0;
    if (!t || !out) return 0;
    while (len < 40 && t[len]) ++len;
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
                out[k].deg = userDeg + (k - first) > 13 ? 13 : userDeg + (k - first); // later syllables climb
                out[k].fixed = 1;
            }
            deg = out[n - 1].deg;
        }
        if (n > first) {
            int beats = out[n - 1].beats + hold;
            out[n - 1].beats = uint8_t(beats > 48 ? 48 : beats);
        }
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

} // namespace glyphsong_score
