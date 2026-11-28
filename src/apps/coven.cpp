// COVEN — a circle of devices sharing one drone over ESP-NOW (channel 1).
// Every Cardputer running COVEN takes a seat: each holds its own note in the
// chord, the eldest (lowest address) keeps the root and the mode, and a slow
// shared pulse passes round the circle so each seat rings its bell in turn.
// Anything else talking on the channel becomes a familiar at the edge: DYAD
// dongles whisper their lines aloud in the animalese voice, TETHER beacons
// and other strangers tick softly.
//
// keys  a-k your note   1-4 mode (eldest only)   z/x octave
//       space call the circle (everyone rings)   m hush familiars
#include "../dsp.h"
#include "../net.h"
#include "../rite.h"
#include "../theme.h"
#include "../vox.h"
#include <new>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
struct Scale {
    const char *name;
    int8_t steps[7];
};
const Scale SCALES[] = {
    {"aeolian", {0, 2, 3, 5, 7, 8, 10}},
    {"dorian", {0, 2, 3, 5, 7, 9, 10}},
    {"phrygian", {0, 1, 3, 5, 7, 8, 10}},
    {"lydian", {0, 2, 4, 6, 7, 9, 11}},
};
constexpr int N_SCALES = 4;
const char PIANO[] = "awsedftgyhujk";
constexpr int MAXS = 10;           // seats + familiars we track
constexpr uint32_t BEAT_MS = 1800; // the circle's pulse
constexpr uint32_t GONE_MS = 15000;

// wire: "CV1" kind rootPc scale degree beat
struct Cv {
    char m[3];
    uint8_t kind; // 0 presence, 1 call
    uint8_t rootPc, scale, degree, beat;
};

struct Soul {
    bool used = false, member = false, talker = false;
    uint8_t mac[6];
    int8_t rssi = -80;
    uint8_t degree = 0;
    uint32_t seen = 0, spokeAt = 0, rangAt = 0;
    float hz = 300; // a familiar's speaking pitch
};

struct State {
    uint8_t me[6] = {};
    int rootPc = 9, oct = 3, scale = 0, degree = 4; // A, aeolian, the fifth
    bool eldest = true, hush = false;
    uint8_t beat = 0;
    uint32_t beatAt = 0, sentAt = 0, callAt = 0;
    Soul soul[MAXS];
    // the whisper being spoken
    char whisper[96] = {};
    char whisperBy[8] = {};
    uint32_t whisperAt = 0;
    int lastRinger = -1, ringer = -1; // seat index, -1 = me
    // sound
    vox::Voice v;
    vox::Reader rd;
    dsp::Osc padA, padB, mine, mineB;
    volatile float rootHz = 110, myHz = 220, fullness = 0;
    float padAmp = 0;
    dsp::Osc bell, bellMod;
    dsp::Decay bellEnv, bellModEnv;
    volatile float bellHz = 0, bellLvl = 0;
    dsp::Delay<4096> dl;
    float dlLp = 0;
    uint32_t ctrl = 0;
} *S = nullptr;

bool macLess(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) < 0; }

int midiOf(int degree) {
    const Scale &sc = SCALES[S->scale];
    return S->rootPc + 12 * (S->oct + 1) + sc.steps[degree % 7] + 12 * (degree / 7);
}

void strike(float hz, float lvl) {
    S->bellLvl = lvl;
    S->bellHz = hz; // published last; the audio thread consumes it
}

void render(int16_t *out, int n) {
    State &s = *S;
    for (int i = 0; i < n; ++i) {
        if ((s.ctrl++ & 63) == 0) {
            s.padA.hz(s.rootHz * 0.5f);
            s.padB.hz(s.rootHz * 0.75f * 1.002f);
            s.mine.hz(s.myHz);
            s.mineB.hz(s.myHz * 1.004f);
            s.padAmp += ((0.18f + 0.22f * s.fullness) - s.padAmp) * 0.01f;
            if (s.bellHz > 0) {
                s.bell.hz(s.bellHz);
                s.bellMod.hz(s.bellHz * 1.4f);
                s.bellEnv.trigger(s.bellLvl);
                s.bellModEnv.trigger(1.f);
                s.bellHz = 0;
            }
        }
        s.rd.step(s.v);
        float x = (s.padA.sine() + s.padB.sine() * 0.6f) * s.padAmp;
        x += (s.mine.tri() + s.mineB.sine()) * 0.07f;
        if (s.bellEnv.v > 0.0004f) x += s.bell.pmSine(s.bellMod.sine() * 0.3f * s.bellModEnv.tick()) * s.bellEnv.tick() * 0.3f;
        x += s.v.tick() * 0.9f;
        float wet = s.dl.tap(4095) * 0.5f + s.dl.tap(3170) * 0.3f;
        s.dlLp += (wet - s.dlLp) * 0.18f;
        s.dl.put(x + s.dlLp * 0.6f);
        out[i] = dsp::toPcm(x * 0.8f + s.dlLp * 0.5f);
    }
}

void enter() {
    if (!S) S = new (std::nothrow) State();
    if (!S) return;
    net::nowBegin();
    net::nowMac(S->me);
    S->bellEnv.time(3.2f);
    S->bellModEnv.time(0.7f);
    S->v.gain = 0.8f;
    S->beatAt = hal::ms();
    hal::setAudio(render);
}
void leave() {
    delete S;
    S = nullptr;
}

void send(uint8_t kind) {
    State &s = *S;
    Cv c = {{'C', 'V', '1'}, kind, (uint8_t)s.rootPc, (uint8_t)s.scale, (uint8_t)s.degree, s.beat};
    net::nowSend(&c, sizeof c);
}

bool onKey(const hal::Key &k) {
    if (!S) return false;
    State &s = *S;
    if (k.ch) {
        const char *p = strchr(PIANO, k.ch);
        if (p) { s.degree = (int)(p - PIANO); return true; }
        if (k.ch >= '1' && k.ch < '1' + N_SCALES) {
            if (s.eldest) s.scale = k.ch - '1'; // followers keep the circle's mode
            return true;
        }
    }
    switch (k.ch) {
    case 'z': if (s.oct > 1) s.oct--; break;
    case 'x': if (s.oct < 4) s.oct++; break;
    case 'm': s.hush = !s.hush; if (s.hush) s.rd.len = 0; break;
    case ' ':
        send(1);
        s.callAt = hal::ms();
        strike(dsp::midiHz(midiOf(s.degree) + 12), 0.5f);
        break;
    }
    return true;
}

Soul *find(const uint8_t *mac, uint32_t now) {
    State &s = *S;
    Soul *free = nullptr, *oldest = nullptr;
    for (auto &x : s.soul) {
        if (x.used && !memcmp(x.mac, mac, 6)) return &x;
        if (!x.used && !free) free = &x;
        if (x.used && (!oldest || x.seen < oldest->seen)) oldest = &x;
    }
    Soul *n = free ? free : oldest;
    *n = Soul();
    n->used = true;
    memcpy(n->mac, mac, 6);
    uint32_t h = mac[3] * 131u + mac[4] * 31u + mac[5];
    n->hz = 260.f + (h % 9) * 16.f; // each familiar has its own voice
    n->seen = now;
    return n;
}

void listen(uint32_t now) {
    State &s = *S;
    net::NowPkt p;
    while (net::nowPoll(p)) {
        if (!memcmp(p.mac, s.me, 6)) continue;
        Soul *x = find(p.mac, now);
        x->seen = now;
        x->rssi = p.rssi;
        if (p.len >= (int)sizeof(Cv) && !memcmp(p.data, "CV1", 3)) {
            Cv c;
            memcpy(&c, p.data, sizeof c);
            x->member = true;
            x->degree = c.degree % 13;
            if (c.kind == 1) { s.callAt = now; strike(dsp::midiHz(midiOf(x->degree) + 12), 0.5f); }
            if (macLess(p.mac, s.me)) {
                // an elder: follow its root, mode and pulse
                s.rootPc = c.rootPc % 12;
                s.scale = c.scale % N_SCALES;
                if (c.beat != s.beat) { s.beat = c.beat; s.beatAt = now; }
            }
        } else if (p.len > 4 && !memcmp(p.data, "DY1|", 4)) {
            // a DYAD line: the text is everything after the 10th '|'
            int bars = 0, at = 0;
            for (int i = 0; i < p.len && bars < 10; ++i) if (p.data[i] == '|' && ++bars == 10) at = i + 1;
            x->talker = true;
            if (at && at < p.len) {
                int n = p.len - at;
                if (n > (int)sizeof s.whisper - 1) n = sizeof s.whisper - 1;
                memcpy(s.whisper, p.data + at, n);
                s.whisper[n] = 0;
                snprintf(s.whisperBy, sizeof s.whisperBy, "~%02x%02x", p.mac[4], p.mac[5]);
                s.whisperAt = now;
                x->spokeAt = now;
                if (!s.hush) {
                    s.rd.hz = x->hz;
                    s.rd.sylSec = 0.07f;
                    s.rd.loop = false;
                    s.rd.start(s.whisper, n);
                }
            }
        } else {
            x->rangAt = now; // a stranger's tick
            if (!s.hush) strike(x->hz * 4.f, 0.06f);
        }
    }
    for (auto &x : s.soul)
        if (x.used && now - x.seen > GONE_MS) x.used = false;
}

void draw(uint32_t now) {
    if (!S) return;
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("COVEN", "coven");
    listen(now);

    // seats: me plus members, ordered by address (the eldest first)
    int seat[MAXS + 1], ns = 0;
    seat[ns++] = -1;
    for (int i = 0; i < MAXS; ++i)
        if (s.soul[i].used && s.soul[i].member) seat[ns++] = i;
    for (int i = 1; i < ns; ++i)
        for (int j = i; j > 0; --j) {
            const uint8_t *a = seat[j] < 0 ? s.me : s.soul[seat[j]].mac;
            const uint8_t *b = seat[j - 1] < 0 ? s.me : s.soul[seat[j - 1]].mac;
            if (macLess(a, b)) { int t = seat[j]; seat[j] = seat[j - 1]; seat[j - 1] = t; }
        }
    s.eldest = seat[0] == -1;
    int familiars = 0;
    for (auto &x : s.soul) familiars += x.used && !x.member;

    // the pulse: whoever's turn it is rings
    if (now - s.beatAt >= BEAT_MS) {
        s.beatAt += BEAT_MS;
        if (now - s.beatAt > BEAT_MS) s.beatAt = now;
        s.beat++;
        s.lastRinger = s.ringer;
        s.ringer = seat[s.beat % ns];
        if (s.ringer == -1) strike(dsp::midiHz(midiOf(s.degree)), 0.3f);
        else {
            Soul &m = s.soul[s.ringer];
            strike(dsp::midiHz(midiOf(m.degree)), 0.22f);
            m.rangAt = now;
        }
    }
    if (now - s.sentAt > 1000) { s.sentAt = now; send(0); }
    s.rootHz = dsp::midiHz(midiOf(0) - 12);
    s.myHz = dsp::midiHz(midiOf(s.degree));
    s.fullness = fminf(1.f, (ns - 1) * 0.34f);

    // ---- the circle
    const int cx = 62, cy = 64, R = 32;
    uint32_t sinceCall = now - s.callAt;
    if (sinceCall < 1200) cv.drawCircle(cx, cy, R + sinceCall / 40, blend(C_GLOW, C_VOID, 255 - sinceCall * 255 / 1200));
    cv.drawCircle(cx, cy, R, C_CLOT);
    cv.drawCircle(cx, cy, R + 14, C_DUSK);
    // runes turning slowly around the circle
    const char *ring = "COVENOFTHEWIRED";
    int rn = strlen(ring);
    for (int i = 0; i < rn; ++i) {
        float a = i * 6.2831853f / rn + now * 0.00012f;
        char g[2] = {ring[i], 0};
        glyphs(DAEDRIC_S, g, cx + (int)(cosf(a) * (R - 10)) - 4, cy + (int)(sinf(a) * (R - 10)) - 8, C_DUSK);
    }
    // seats: me at the bottom, the rest evenly round; lines make the star
    int sx[MAXS + 1], sy[MAXS + 1], myPos = 0;
    for (int i = 0; i < ns; ++i) if (seat[i] == -1) myPos = i;
    for (int i = 0; i < ns; ++i) {
        float a = 1.5708f + (i - myPos) * 6.2831853f / ns;
        sx[i] = cx + (int)(cosf(a) * R);
        sy[i] = cy + (int)(sinf(a) * R);
    }
    int step = ns >= 5 ? 2 : 1;
    if (ns >= 2)
        for (int i = 0; i < ns; ++i) {
            int j = (i + step) % ns;
            cv.drawLine(sx[i], sy[i], sx[j], sy[j], C_BLOOD);
        }
    // the pulse travelling from the last ringer to this one
    int ri = 0, li = 0;
    for (int i = 0; i < ns; ++i) { if (seat[i] == s.ringer) ri = i; if (seat[i] == s.lastRinger) li = i; }
    float t = fminf(1.f, (now - s.beatAt) / 400.f);
    if (ns > 1) cv.fillCircle(sx[li] + (int)((sx[ri] - sx[li]) * t), sy[li] + (int)((sy[ri] - sy[li]) * t), 2, C_GLOW);
    // candles
    for (int i = 0; i < ns; ++i) {
        uint32_t lit = seat[i] == -1 ? (s.ringer == -1 ? now - s.beatAt : 9999) : now - s.soul[seat[i]].rangAt;
        float glow = lit < 900 ? 1.f - lit / 900.f : 0.f;
        int h = 3 + (int)(glow * 5) + (int)(((now / 90) + i * 7) % 3);
        cv.fillRect(sx[i] - 1, sy[i], 3, 6, seat[i] == -1 ? C_BONE : C_STONE);
        for (int k = 0; k < h; ++k) {
            int wob = ((now / 70 + k * 3 + i) % 3) - 1;
            cv.drawPixel(sx[i] + (k > 1 ? wob : 0), sy[i] - 1 - k, ramp((uint8_t)(255 - k * 160 / h)));
        }
    }
    // familiars on the outer ring
    for (int i = 0; i < MAXS; ++i) {
        Soul &x = s.soul[i];
        if (!x.used || x.member) continue;
        float a = (x.mac[5] * 0.0246f) + (x.mac[4] & 7) * 0.8f + now * 0.00005f;
        int fx = cx + (int)(cosf(a) * (R + 14)), fy = cy + (int)(sinf(a) * (R + 14));
        bool speaking = x.talker && now - x.spokeAt < 4000;
        bool tick = now - x.rangAt < 300;
        diamond(fx, fy, speaking ? 3 : 2, speaking ? C_GLOW : tick ? C_EMBER : C_RUST, true);
        if (speaking) cv.drawCircle(fx, fy, 4 + (now / 120) % 4, C_RUST);
    }

    // ---- panel
    char b[40];
    int px = 132;
    frame(px - 4, 22, 110, 78, C_CLOT, C_UMBER);
    snprintf(b, sizeof b, "circle %d", ns);
    text(b, px, 26, C_BONE);
    snprintf(b, sizeof b, "kin %d", familiars);
    textRight(b, px + 102, 26, C_DIM);
    text(s.eldest ? "eldest" : "following", px, 38, s.eldest ? C_GLOW : C_EMBER);
    snprintf(b, sizeof b, "%s %s", noteName(s.rootPc), SCALES[s.scale].name);
    text(b, px, 50, C_BONE);
    int my = midiOf(s.degree);
    snprintf(b, sizeof b, "%s%d", noteName(my), my / 12 - 1);
    text("mine", px, 66, C_DIM);
    text(b, px + 36, 62, C_GLOW, 2);
    snprintf(b, sizeof b, "pulse %u", s.beat);
    text(b, px, 84, C_DIM);
    if (s.hush) textRight("hush", px + 102, 84, C_BLOOD);

    // ---- the whisper, appearing as it is spoken
    if (s.whisper[0] && now - s.whisperAt < 12000) {
        int shown = s.hush ? (int)strlen(s.whisper) : (s.rd.len ? s.rd.idx + 1 : (int)strlen(s.whisper));
        if (shown < 0) shown = 0;
        uint8_t fade = now - s.whisperAt < 9000 ? 255 : (uint8_t)(255 - (now - s.whisperAt - 9000) * 255 / 3000);
        text(s.whisperBy, 4, 104, blend(C_EMBER, C_VOID, fade));
        // show the tail that fits (34 chars after the tag)
        int from = shown > 33 ? shown - 33 : 0;
        char line[40];
        snprintf(line, sizeof line, "%.*s", shown - from, s.whisper + from);
        text(line, 40, 104, blend(C_BONE, C_VOID, fade));
    } else {
        textCentre(ns > 1 ? "the circle hums" : "waiting for the others...", 120, 104,
                   blend(C_DIM, C_VOID, (uint8_t)(150 + 100 * sinf(now * 0.003f))));
    }
    footer("a-k note  spc call  m hush  1-4 mode");
}
} // namespace

const Rite RITE_COVEN = {"COVEN", "coven", "a circle of devices, one drone", enter, leave, onKey, draw};
