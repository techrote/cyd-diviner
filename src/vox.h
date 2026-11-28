// Vox — a tiny formant voice that speaks text the way Animal Crossing
// villagers do: every letter is one quick pitched syllable. A buzzy glottal
// saw runs through three resonant band-passes tuned to a vowel (a e i o u
// sound as themselves; a consonant sings the vowel of its letter-name — b is
// "ee", f is "eh", q is "oo" — after a short onset: a burst for plosives, a
// hiss for fricatives, a hum for nasals, a glide for liquids).
// Shared by GLYPHSONG and COVEN. Everything here runs on the audio thread.
#pragma once
#include "dsp.h"
#include <string.h>

namespace vox {

enum { V_A, V_E, V_I, V_O, V_U, V_SCHWA };
enum Onset : uint8_t { O_NONE, O_PLOSIVE, O_FRIC, O_NASAL, O_LIQUID };

struct Formants {
    int16_t f1, f2, f3;
};
// sung-ish formants, nudged up a little for a small, bright creature voice
inline const Formants VOWEL[6] = {
    {850, 1300, 2700}, // a  "ah"
    {560, 1900, 2650}, // e  "eh"
    {330, 2300, 3050}, // i  "ee"
    {560, 920, 2500},  // o  "oh"
    {360, 820, 2350},  // u  "oo"
    {520, 1450, 2550}, // schwa
};
inline const char *const VOWEL_NAME[6] = {"ah", "eh", "ee", "oh", "oo", "uh"};

// a..z -> vowel (low nibble) | onset (high nibble)
inline const uint8_t LETTER[26] = {
    V_A,                  V_I | O_PLOSIVE << 4, V_I | O_FRIC << 4,    V_I | O_PLOSIVE << 4, // a b c d
    V_E,                  V_E | O_FRIC << 4,    V_I | O_PLOSIVE << 4, V_A | O_FRIC << 4,    // e f g h
    V_I,                  V_A | O_PLOSIVE << 4, V_A | O_PLOSIVE << 4, V_E | O_LIQUID << 4,  // i j k l
    V_E | O_NASAL << 4,   V_E | O_NASAL << 4,   V_O,                  V_I | O_PLOSIVE << 4, // m n o p
    V_U | O_PLOSIVE << 4, V_A | O_LIQUID << 4,  V_E | O_FRIC << 4,    V_I | O_PLOSIVE << 4, // q r s t
    V_U,                  V_I | O_FRIC << 4,    V_U | O_LIQUID << 4,  V_E | O_FRIC << 4,    // u v w x
    V_A | O_LIQUID << 4,  V_I | O_FRIC << 4,                                                // y z
};

inline bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
inline int vowelOf(char c) { return isLetter(c) ? LETTER[lower(c) - 'a'] & 15 : V_SCHWA; }

struct Voice {
    dsp::Osc glot, vib;
    dsp::SVF b1, b2, b3, hiss;
    dsp::Noise nz;
    float f0 = 220, f0T = 220, vibDepth = 0;
    float F[3] = {520, 1450, 2550}, FT[3] = {520, 1450, 2550};
    float amp = 0, noise = 0, noiseK = 0.999f;
    int len = 0, pos = 0;
    float legato = 0; // 0 = clipped syllables, 1 = joined (chant)
    float gain = 1;
    uint32_t ctl = 0;
    volatile int vowel = V_SCHWA; // for the UI

    // Start one syllable for letter c at pitch hz, lasting `samples`.
    void say(char c, float hz, int samples) {
        int vw = vowelOf(c);
        uint8_t on = isLetter(c) ? LETTER[lower(c) - 'a'] >> 4 : O_NONE;
        vowel = vw;
        FT[0] = VOWEL[vw].f1;
        FT[1] = VOWEL[vw].f2;
        FT[2] = VOWEL[vw].f3;
        hiss.set(3500, 0.55f);
        switch (on) {
        case O_PLOSIVE: noise = 0.9f; noiseK = 0.9965f; break; // ~10 ms burst
        case O_FRIC: noise = 0.55f; noiseK = 0.9993f; break;   // ~60 ms hiss
        case O_NASAL: F[0] = 260; F[1] = 1100; break;          // start from a hum
        case O_LIQUID: F[1] = VOWEL[vw].f2 * 0.6f; break;      // glide up into it
        default: break;
        }
        f0T = hz;
        if (legato < 0.5f) f0 = hz * 1.03f; // a tiny scoop, like a chirp
        len = samples;
        pos = 0;
    }
    void hush() { len = 0; }

    inline float tick() {
        if ((ctl++ & 15) == 0) {
            float glide = legato > 0.5f ? 0.02f : 0.12f;
            f0 += (f0T - f0) * glide;
            for (int k = 0; k < 3; ++k) F[k] += (FT[k] - F[k]) * 0.22f;
            glot.hz(f0 * (1.f + vibDepth * vib.sine()));
            b1.set(F[0], 0.9f);
            b2.set(F[1], 0.92f);
            b3.set(F[2], 0.9f);
        }
        // envelope: 5 ms attack, hold, release over the last part
        float e = 0;
        if (pos < len) {
            const int att = hal::RATE / 200;
            float rel = 0.32f - 0.24f * legato;
            int rs = (int)(len * (1.f - rel));
            e = pos < att ? (float)pos / att : pos > rs ? (float)(len - pos) / (len - rs) : 1.f;
            ++pos;
        }
        amp += (e - amp) * 0.03f;
        float src = glot.saw();
        float v = b1.bp(src) + b2.bp(src) * 0.8f + b3.bp(src) * 0.3f;
        float out = v * amp * 0.3f;
        if (noise > 0.002f) {
            out += hiss.bp(nz.white()) * noise * 0.55f;
            noise *= noiseK;
        }
        return out * gain;
    }
};

// Reads a line of text into a Voice one syllable at a time. Spoken style
// gives each letter its own pitch around `hz` with a sentence contour
// (questions rise, statements fall); the caller can override pitch per
// letter via `pitchFn` (chant mode quantises to a scale).
struct Reader {
    char txt[128] = {};
    int len = 0;
    volatile int idx = -1; // letter being voiced, -1 idle
    int wait = 0;
    bool loop = false;
    float hz = 300, sylSec = 0.075f, gapSec = 1.2f;
    float (*pitchFn)(char c, int i, void *ctx) = nullptr;
    void *ctx = nullptr;
    dsp::Noise rng;

    void start(const char *s, int n = -1) {
        len = n < 0 ? (int)strlen(s) : n;
        if (len > (int)sizeof txt - 1) len = sizeof txt - 1;
        memcpy(txt, s, len);
        txt[len] = 0;
        idx = -1;
        wait = hal::RATE / 20;
    }
    bool busy() const { return idx >= 0 || wait > 0; }

    float spokenPitch(char c, int i) {
        // stable per letter (so a word always sounds like itself) + a wobble
        float semis = (float)((lower(c) * 7) % 5) - 2.f + (rng.next() % 100) * 0.012f - 0.6f;
        // sentence contour: find the end of this sentence
        int j = i;
        while (j < len && txt[j] != '.' && txt[j] != '?' && txt[j] != '!') ++j;
        int toEnd = j - i;
        if (j < len && toEnd < 4) {
            if (txt[j] == '?') semis += (4 - toEnd) * 1.6f;
            else if (txt[j] == '.') semis -= (4 - toEnd) * 0.9f;
        }
        if (j < len && txt[j] == '!') semis += 2.f;
        return hz * powf(2.f, semis / 12.f);
    }

    // call once per sample before voice.tick()
    inline void step(Voice &v, Voice *v2 = nullptr, float v2ratio = 0.667f) {
        if (wait > 0) { --wait; return; }
        if (!len) return;
        int i = idx + 1;
        if (i >= len) {
            idx = -1;
            if (loop) { wait = (int)(gapSec * hal::RATE); idx = -1; return; }
            len = 0;
            return;
        }
        idx = i;
        char c = txt[i];
        int syl = (int)(sylSec * hal::RATE);
        if (isLetter(c)) {
            float p = pitchFn ? pitchFn(c, i, ctx) : spokenPitch(c, i);
            int on = v.legato > 0.5f ? syl : syl * 4 / 5; // spoken blips leave a gap
            v.say(c, p, on);
            if (v2) v2->say(c, p * v2ratio, on);
            wait = syl;
        } else if (c == ' ') {
            wait = syl / 2; // a breath between words
        } else if (c == ',' || c == '.' || c == '?' || c == '!' || c == ';') {
            wait = syl * 4;
        } else {
            wait = syl / 2;
        }
    }
};

} // namespace vox
