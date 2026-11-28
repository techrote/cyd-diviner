// SPECTRA — the air sings. Every Wi-Fi access point (or BLE device) nearby
// becomes a voice in a slow choir: its address picks a note in the scale and
// keeps it, its signal strength sets how loud and bright it sings. Arrivals
// bloom in with a struck bell; departures sigh down a fifth as they fade.
// The left pane is a waterfall of the air, the choir's spectrum falling as ash.
//
// keys  w wifi  b ble   1-4 mode   z/x octave   ;/. select   ,// air
//       space hold (no new voices, none leave)
#include "../dsp.h"
#include "../audio_exchange.h"
#include "../radio_sonification.h"
#include "../net.h"
#include "../rite.h"
#include "../theme.h"
#include <stdio.h>
#include <new>
#include <string.h>

using namespace theme;

namespace {
struct Scale {
    const char *name;
    int8_t steps[7];
    int n;
};
const Scale SCALES[] = {
    {"dorian", {0, 2, 3, 5, 7, 9, 10}, 7},
    {"lydian", {0, 2, 4, 6, 7, 9, 11}, 7},
    {"pent", {0, 3, 5, 7, 10, 0, 0}, 5},
    {"phrygian", {0, 1, 3, 5, 7, 8, 10}, 7},
};
constexpr int N_SCALES = sizeof(SCALES) / sizeof(SCALES[0]);

constexpr int NV = 8;
constexpr uint32_t GONE_MS = 25000;
constexpr int COLS = 60, ROWS = 24; // waterfall: 2x4 px cells over 120x96

struct Voice {
    // roster (UI thread)
    bool used = false, leaving = false, displaced = false;
    char id[18] = {}, name[15] = {};
    uint32_t hash = 0, seen = 0, born = 0;
    int8_t rssi = -90;
    int note = 48;
    // UI-owned targets and a copy of complete audio telemetry.
    uint32_t generation = 0;
    float hz = 110, hzT = 110, amp = 0, ampT = 0, bright = 0.3f, swell = 1;
};

enum Band : uint8_t { B_WIFI, B_BLE };

struct Target {
    bool used = false, leaving = false;
    uint32_t generation = 0, hash = 0;
    float hz = 110, amp = 0, bright = 0.3f;
};
struct Params { Target voice[NV]; float rootHz = 55, air = 0.55f; };
struct VoiceView { uint32_t generation = 0; float hz = 110, amp = 0, swell = 1; };
struct View { VoiceView voice[NV]; float subAmp = 0; };
struct AudioVoice { dsp::Osc o1, o2, vib, breath; uint32_t generation = 0; float hz = 110, amp = 0, swell = 1; };
struct Audio {
    AudioVoice voice[NV];
    Params params;
    dsp::Osc sub, bell, bellMod;
    dsp::Decay bellEnv, bellModEnv;
    float subAmp = 0, crowd = 0, dlLp = 0;
    dsp::SVF tone;
    dsp::Delay16<4096> dl;
    uint32_t ctrl = 0;
};
struct State {
    Band band = B_WIFI;
    int scale = 0, root = 45, sel = 0;
    float air = 0.55f;
    bool hold = false;
    Voice v[NV];
    std::vector<net::AP> aps;
    std::vector<net::BleDev> devs;
    audio_exchange::Latest<Params> controls;
    audio_exchange::SpscQueue<float, 8> arrivals;
    audio_exchange::Latest<View> telemetry;
    Audio audio;
    float subAmp = 0;
    uint32_t droppedArrivals = 0;
    int scanState = -1;
    uint8_t wf[ROWS][COLS] = {};
    uint32_t lastRow = 0, logAt = 0, quietSince = 0;
} *S = nullptr;

int noteFor(uint32_t h) {
    const Scale &sc = SCALES[S->scale];
    return radio_music::note(h, S->root, sc.steps, unsigned(sc.n));
}
float level(int8_t rssi) { return radio_music::level(rssi); }
void publish() {
    Params p; p.rootHz = dsp::midiHz(S->root - 12); p.air = S->air;
    for (int i = 0; i < NV; ++i) {
        const Voice &v = S->v[i];
        p.voice[i] = {v.used, v.leaving, v.generation, v.hash, v.hzT, v.ampT, v.bright};
    }
    S->controls.publish(p);
}

void retuneAll() {
    for (auto &v : S->v)
        if (v.used && !v.leaving) {
            v.note = noteFor(v.hash);
            v.hzT = dsp::midiHz(v.note);
        }
}

// One sighting from a scan: refresh a voice, or give the signal a new one.
void sight(const char *id, const char *name, int8_t rssi, uint32_t now, uint32_t observed) {
    State &s = *S;
    if (!radio_music::fresh(now, observed, GONE_MS)) return;
    for (auto &v : s.v)
        if (v.used && !strcmp(v.id, id)) {
            if (v.displaced) return; // retire a displaced/band-switched slot before reuse
            v.seen = observed;
            v.rssi = rssi;
            if (v.leaving) { v.leaving = false; v.hzT = dsp::midiHz(v.note); } // came back
            v.ampT = level(rssi);
            v.bright = level(rssi);
            return;
        }
    if (s.hold) return;
    int slot = -1;
    for (int i = 0; i < NV; ++i) if (!s.v[i].used) { slot = i; break; }
    if (slot < 0) {
        // full choir: a much stronger newcomer displaces the faintest singer
        int weak = 0;
        for (int i = 1; i < NV; ++i) if (s.v[i].rssi < s.v[weak].rssi) weak = i;
        if (!s.v[weak].leaving && rssi > s.v[weak].rssi + 8) {
            s.v[weak].leaving = true; s.v[weak].displaced = true;
            s.v[weak].ampT = 0;
            s.v[weak].hzT = s.v[weak].hz * 0.6674f;
        }
        return;
    }
    Voice &v = s.v[slot];
    strncpy(v.id, id, sizeof v.id - 1);
    v.id[sizeof v.id - 1] = 0;
    strncpy(v.name, name && *name ? name : "(unnamed)", sizeof v.name - 1);
    v.name[sizeof v.name - 1] = 0;
    v.hash = radio_music::identityHash(id);
    v.note = noteFor(v.hash);
    v.hz = v.hzT = dsp::midiHz(v.note);
    v.amp = 0;
    v.ampT = level(rssi);
    v.bright = level(rssi);
    v.rssi = rssi;
    v.seen = observed; v.born = now; ++v.generation;
    v.leaving = false; v.displaced = false;
    v.used = true; // the following complete snapshot publishes this slot
    if (!s.arrivals.push(v.hzT * 2.f)) ++s.droppedArrivals; // arrival bell, an octave over its voice
}

void render(int16_t *out, int n) {
    Audio &s = S->audio;
    if (S->controls.consume(s.params)) {
        for (int k = 0; k < NV; ++k) {
            const Target &t = s.params.voice[k]; AudioVoice &v = s.voice[k];
            if (t.used && v.generation != t.generation) {
                v = AudioVoice{}; v.generation = t.generation; v.hz = t.hz;
                v.vib.hz(0.15f + (t.hash & 15) * 0.02f);
                v.breath.hz(0.04f + ((t.hash >> 4) & 15) * 0.006f);
            }
        }
    }
    float f = 0, latest = 0;
    for (int k = 0; k < 8 && S->arrivals.pop(f); ++k) latest = f;
    if (latest > 0) {
        while (latest > 700.f) latest *= 0.5f;
        s.bell.hz(latest); s.bellMod.hz(latest * 1.4f);
        s.bellEnv.trigger(0.45f); s.bellModEnv.trigger(1.f);
    }
    for (int i = 0; i < n; ++i) {
        if ((s.ctrl++ & 63) == 0) {
            int live = 0;
            for (int k = 0; k < NV; ++k) {
                const Target &t = s.params.voice[k]; AudioVoice &v = s.voice[k];
                if (!t.used) continue;
                live += !t.leaving;
                v.hz += (t.hz - v.hz) * (t.leaving ? 0.0009f : 0.004f);
                v.amp += (t.amp - v.amp) * (t.leaving ? 0.0035f : 0.006f);
                float w = 1.f + v.vib.sineStrided(64) * 0.0035f;
                v.o1.hz(v.hz * w); v.o2.hz(v.hz * 2.003f * w);
                v.swell = 0.62f + 0.38f * v.breath.sineStrided(64);
            }
            s.crowd += (live - s.crowd) * 0.002f;
            float c = s.crowd / NV;
            s.sub.hz(s.params.rootHz); s.subAmp = 0.10f + 0.25f * c;
            s.tone.set(500.f + 2200.f * c + 900.f * s.params.air, 0.2f);
        }
        float mix = 0;
        for (int k = 0; k < NV; ++k) {
            const Target &t = s.params.voice[k]; AudioVoice &v = s.voice[k];
            if (!t.used || v.amp < 0.0005f) continue;
            mix += (v.o1.sine() + v.o2.tri() * 0.35f * t.bright) * v.amp * v.swell;
        }
        mix = s.tone.lp(mix * 0.16f) + s.sub.sine() * s.subAmp;
        if (s.bellEnv.v > 0.0004f) {
            float m = s.bellMod.sine() * 0.3f * s.bellModEnv.tick();
            mix += s.bell.pmSine(m) * s.bellEnv.tick() * 0.25f;
        }
        float wet = s.dl.tap(4001) * 0.5f + s.dl.tap(2731) * 0.35f;
        s.dlLp += (wet - s.dlLp) * 0.2f;
        s.dl.put(mix + s.dlLp * (0.45f + 0.25f * s.params.air));
        out[i] = dsp::toPcm((mix * 0.75f + s.dlLp * (0.25f + 0.5f * s.params.air)) * 0.9f);
    }
    View view; view.subAmp = s.subAmp;
    for (int i = 0; i < NV; ++i) {
        const AudioVoice &v = s.voice[i]; view.voice[i] = {v.generation, v.hz, v.amp, v.swell};
    }
    S->telemetry.publish(view);
}

void startBand(Band b) {
    if (b == B_WIFI) net::wifiBegin(false); // non-associating ACTIVE scan (may send probes)
    else net::bleBegin();
}

void enter() {
    // radio first: its stack takes the big allocations, our state fits after
    startBand(B_WIFI);
    if (!S) S = new (std::nothrow) State();
    if (!S) { net::radioStop(); return; } // no retained radio on allocation failure
    S->band = B_WIFI;
    S->quietSince = hal::ms();
    S->audio.bellEnv.time(2.6f);
    S->audio.bellModEnv.time(0.5f);
    publish(); hal::setAudio(render);
}
void leave() {
    hal::setAudio(nullptr);
    net::radioStop();
    delete S;
    S = nullptr;
}

bool onKey(const hal::Key &k) {
    if (!S) return false;
    State &s = *S;
    if (k.ch == 'w' || k.ch == 'b') {
        Band want = k.ch == 'w' ? B_WIFI : B_BLE;
        if (want != s.band) {
            s.band = want;
            // a new band is a new choir: everyone currently singing departs
            for (auto &v : s.v) if (v.used) { v.leaving = true; v.displaced = true; v.ampT = 0; v.hzT = v.hz * 0.6674f; }
            std::vector<net::AP>().swap(s.aps);
            std::vector<net::BleDev>().swap(s.devs);
            startBand(want);
            s.quietSince = hal::ms();
        }
        return true;
    }
    if (k.ch >= '1' && k.ch < '1' + N_SCALES) { s.scale = k.ch - '1'; retuneAll(); return true; }
    switch (k.ch) {
    case 'z': if (s.root > 33) s.root -= 12; retuneAll(); return true;
    case 'x': if (s.root < 57) s.root += 12; retuneAll(); return true;
    case ' ': s.hold = !s.hold; return true;
    }
    switch (k.nav) {
    case hal::N_UP: s.sel = (s.sel + NV - 1) % NV; break;
    case hal::N_DOWN: s.sel = (s.sel + 1) % NV; break;
    case hal::N_LEFT: s.air = fmaxf(0.f, s.air - 0.1f); break;
    case hal::N_RIGHT: s.air = fminf(1.f, s.air + 0.1f); break;
    default: break;
    }
    return true;
}

int colFor(float hz) {
    // log-frequency across the pane: root-12 .. root+40 semitones
    float semis = 12.f * log2f(hz / dsp::midiHz(S->root - 12));
    int c = (int)(semis * (COLS - 1) / 52.f);
    return c < 0 ? 0 : c >= COLS ? COLS - 1 : c;
}

void draw(uint32_t now) {
    if (!S) { starved("SPECTRA", "spectra"); return; }
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("SPECTRA", "spectra");

    // Consume telemetry once per frame; no direct reads from audio-owned DSP.
    View view;
    if (s.telemetry.consume(view)) {
        s.subAmp = view.subAmp;
        for (int i = 0; i < NV; ++i) if (view.voice[i].generation == s.v[i].generation) {
            s.v[i].hz = view.voice[i].hz; s.v[i].amp = view.voice[i].amp; s.v[i].swell = view.voice[i].swell;
        }
    }
    // ---- observation (cached rows retain their actual last-observed time)
    char id[18];
    if (s.band == B_WIFI) {
        if ((s.scanState = net::wifiScan(s.aps)) >= 0)
            for (auto &a : s.aps) {
                snprintf(id, sizeof id, "%02x%02x%02x%02x%02x%02x", a.bssid[0], a.bssid[1], a.bssid[2], a.bssid[3],
                         a.bssid[4], a.bssid[5]);
                sight(id, a.ssid.empty() ? "(hidden)" : a.ssid.c_str(), a.rssi, now, a.lastSeen);
            }
    } else if ((s.scanState = net::bleScan(s.devs)) >= 0) {
        for (auto &d : s.devs) sight(d.addr.c_str(), d.name.c_str(), d.rssi, now, d.lastSeen);
    }
    for (auto &v : s.v) {
        if (!v.used) continue;
        if (!v.leaving && !s.hold && now - v.seen > GONE_MS) {
            v.leaving = true; // sigh down a fifth as it fades
            v.ampT = 0;
            v.hzT = v.hz * 0.6674f;
        }
        if (v.leaving && v.amp < 0.002f && v.ampT == 0) v.used = false;
    }

    publish();

    // ---- waterfall: the choir's spectrum falls as ash
    const int wx = 4, wy = 22;
    if (now - s.lastRow > 70) {
        s.lastRow = now;
        memmove(s.wf[1], s.wf[0], sizeof(s.wf) - sizeof(s.wf[0]));
        uint8_t *row = s.wf[0];
        for (int c = 0; c < COLS; ++c) row[c] = (rand() % 23 == 0) ? 10 + rand() % 18 : 0; // the noise floor
        // the root drone smoulders along the low edge, thicker as the crowd grows
        int ember = (int)(s.subAmp * 300.f);
        for (int c = 0; c < 3; ++c) row[c] = (uint8_t)(ember / (1 + c) + rand() % 12);
        for (auto &v : s.v) {
            if (!v.used) continue;
            int c0 = colFor(v.hz);
            float a = fminf(1.f, v.amp * 1.4f * v.swell);
            float flick = 0.8f + 0.2f * sinf(now * 0.004f + (v.hash & 1023) * 0.0061f); // small arg: fast, exact
            if (v.leaving) c0 = colFor(v.hz); // the sigh bends the trail
            for (int d = -3; d <= 3; ++d) {
                int c = c0 + d;
                if (c < 0 || c >= COLS) continue;
                int val = row[c] + (int)(a * flick * 255.f / (1 + d * d * 0.9f));
                row[c] = val > 255 ? 255 : val;
            }
            // the octave shimmer, faint
            int c2 = colFor(v.hz * 2.f);
            int val = row[c2] + (int)(a * v.bright * 90.f);
            row[c2] = val > 255 ? 255 : val;
        }
    }
    cv.drawRect(wx - 1, wy - 1, COLS * 2 + 2, ROWS * 4 + 2, C_DUSK);
    for (int r = 0; r < ROWS; ++r) {
        float fade = 1.f - r * 0.032f;
        for (int c = 0; c < COLS; ++c) {
            uint8_t val = s.wf[r][c];
            if (val < 6) continue;
            cv.fillRect(wx + c * 2, wy + r * 4, 2, 3, ramp((uint8_t)(val * fade)));
        }
    }
    // the singers themselves, riding the top edge
    for (int i = 0; i < NV; ++i) {
        Voice &v = s.v[i];
        if (!v.used) continue;
        int x = wx + colFor(v.hz) * 2 + 1;
        bool born = now - v.born < 1200;
        uint16_t c = i == s.sel ? C_GLOW : v.leaving ? C_BLOOD : C_EMBER;
        diamond(x, wy + 2, i == s.sel || born ? 3 : 2, c, !v.leaving);
        if (born) cv.drawCircle(x, wy + 2, 3 + (now - v.born) / 90, blend(C_GLOW, C_VOID, 255 - (now - v.born) / 5));
        if (i == s.sel) cv.drawFastVLine(x, wy + 6, ROWS * 4 - 6, blend(C_RUST, C_VOID, 90));
    }
    int live = 0;
    for (auto &v : s.v) live += v.used && !v.leaving;
    if (live) s.quietSince = now;
    if (!live) {
        // say what the radio is actually doing rather than just waiting
        uint8_t a = (uint8_t)(120 + 100 * sinf(now * 0.004f));
        int st = s.band == B_WIFI ? net::wifiScanStatus() : -1;
        const char *l1 = "listening to", *l2 = "the air...";
        if (s.band == B_BLE && s.scanState == net::BLE_UNAVAILABLE) { l1 = "BLE unavailable"; l2 = "try wifi / retry"; }
        else if (s.band == B_WIFI && st == -2) { l1 = "the radio"; l2 = "refuses (retry)"; }
        else if (now - s.quietSince > 15000) { l1 = "no one near"; l2 = s.band == B_WIFI ? "b: try ble" : "w: try wifi"; }
        textCentre(l1, wx + COLS, wy + 38, blend(C_BONE, C_VOID, a));
        textCentre(l2, wx + COLS, wy + 50, blend(C_BONE, C_VOID, a));
        if (hal::heapFree()) {
            char hb[24];
            snprintf(hb, sizeof hb, "heap %uk", (unsigned)(hal::heapFree() / 1024));
            textCentre(hb, wx + COLS, wy + 66, C_DIM);
        }
    }
    if (now - s.logAt > 2000) {
        s.logAt = now;
        hal::debugf("spectra band=%s scan=%d aps=%d devs=%d live=%d heap=%u big=%u", s.band == B_WIFI ? "wifi" : "ble",
                    net::wifiScanStatus(), (int)s.aps.size(), (int)s.devs.size(), live, (unsigned)hal::heapFree(),
                    (unsigned)hal::heapLargest());
    }

    // ---- panel
    char b[40];
    int px = 132;
    frame(px - 4, 22, 110, 96, C_CLOT, C_UMBER);
    text("band", px, 26, C_DIM);
    text(s.band == B_WIFI ? "wifi" : "ble", px + 36, 26, C_BONE);
    snprintf(b, sizeof b, "%d/%d", live, NV);
    textRight(b, px + 102, 26, C_EMBER);
    // the selected voice, or its empty seat
    Voice &v = s.v[s.sel];
    snprintf(b, sizeof b, "voice %d", s.sel + 1);
    text(b, px, 38, C_DIM);
    if (v.used) {
        text(v.name, px, 49, v.leaving ? C_BLOOD : C_GLOW);
        snprintf(b, sizeof b, "%s%d", noteName(v.note), v.note / 12 - 1);
        text(b, px, 60, C_BONE, 2);
        snprintf(b, sizeof b, "%ddBm", v.rssi);
        textRight(b, px + 102, 60, v.rssi > -65 ? C_EMBER : C_DIM);
        cv.drawRect(px + 40, 70, 62, 5, C_CLOT);
        cv.fillRect(px + 41, 71, (int)(60 * fminf(1.f, v.amp)), 3, v.leaving ? C_BLOOD : C_RUST);
    } else {
        text("(silent seat)", px, 49, C_DIM);
    }
    text("mode", px, 82, C_DIM);
    text(SCALES[s.scale].name, px + 36, 82, C_BONE);
    meter("air", px, 94, px + 36, 66, s.air);
    snprintf(b, sizeof b, "%s%d  %s", noteName(s.root), s.root / 12 - 1, s.hold ? "HOLD" : "flow");
    text(b, px, 106, s.hold ? C_GLOW : C_EMBER);
    footer("w/b band 1-4 mode z/x oct spc hold", "touch voice / drag air");
}
bool pointer(const InstrumentPointer &p) {
    if (!S || !p.down) return S != nullptr;
    State &s = *S;
    if (p.x >= 168 && p.x <= 238 && p.y >= 92 && p.y <= 106) {
        s.air = fmaxf(0.f, fminf(1.f, (p.x - 168) / 66.f)); publish(); return true;
    }
    if (p.x >= 128 && p.start) {
        if (p.y >= 22 && p.y <= 34) {
            hal::Key k; k.ch = s.band == B_WIFI ? 'b' : 'w'; onKey(k); return true;
        }
        if (p.y >= 78 && p.y < 92) { s.scale = (s.scale + 1) % N_SCALES; retuneAll(); publish(); return true; }
    }
    if (p.x >= 4 && p.x <= 124 && p.y >= 22 && p.y <= 118) {
        int nearest = -1, best = 1000;
        for (int i = 0; i < NV; ++i) if (s.v[i].used) {
            int d = abs((4 + colFor(s.v[i].hz) * 2 + 1) - p.x);
            if (d < best) { best = d; nearest = i; }
        }
        if (nearest >= 0) s.sel = nearest;
        return true;
    }
    return false;
}
} // namespace

const Rite RITE_SPECTRA = {"SPECTRA", "spectra", "the air sings: each signal a voice", enter, leave, onKey, draw, pointer};
