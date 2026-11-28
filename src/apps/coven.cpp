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
#include "../audio_exchange.h"
#include "../coven_protocol.h"
#include "../radio_sonification.h"
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
constexpr int MAXS = int(coven::MaxPeers) + 6;           // seats + familiars we track
constexpr uint32_t BEAT_MS = 1800; // the circle's pulse
constexpr uint32_t GONE_MS = coven::GoneMs;

struct Soul {
    bool used = false, member = false, talker = false;
    uint8_t mac[6];
    int8_t rssi = -80;
    uint8_t degree = 0;
    uint32_t seen = 0, spokeAt = 0, rangAt = 0, lineHash = 0;
    float hz = 300; // a familiar's speaking pitch
};

struct Params { float rootHz = 110, myHz = 220, fullness = 0; bool hush = false; };
struct Command {
    enum Kind : uint8_t { Bell, Speech } kind = Bell;
    float hz = 220, level = 0.3f;
    char text[96] = {};
};
struct VoiceView { int len = 0, idx = -1; };
struct Audio {
    vox::Voice v;
    vox::Reader rd;
    dsp::Osc padA, padB, mine, mineB, bell, bellMod;
    dsp::Decay bellEnv, bellModEnv;
    dsp::Delay<4096> dl;
    Params params;
    float padAmp = 0, dlLp = 0;
    uint32_t ctrl = 0;
};
struct State : audio_exchange::InternalState {
    uint8_t me[6] = {};
    coven::Session session;
    int rootPc = 9, oct = 3, scale = 0, degree = 4;
    bool eldest = true, hush = false, ready = false;
    uint8_t beat = 0;
    uint32_t beatAt = 0, sentAt = 0, callAt = 0, spokenAt = 0, droppedCommands = 0, logAt = 0;
    bool called = false, spoken = false;
    Soul soul[MAXS];
    char whisper[96] = {}, whisperBy[8] = {};
    uint32_t whisperAt = 0;
    int lastRinger = -1, ringer = -1;
    // Three independent ownership directions: UI controls/events -> audio;
    // audio telemetry -> UI. Audio state is never read by the UI.
    audio_exchange::Latest<Params> controls;
    audio_exchange::SpscQueue<Command, 8> commands;
    audio_exchange::Latest<VoiceView> telemetry;
    VoiceView voiceView;
    Audio audio;
} *S = nullptr;

bool macLess(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) < 0; }

const coven::Peer *memberPeer(const State &s, const uint8_t mac[6]) {
    for (const auto &peer : s.session.peers)
        if (peer.used && !memcmp(peer.mac, mac, 6)) return &peer;
    return nullptr;
}

// CV1 Session owns member liveness. The visual/musical Soul roster mirrors that
// authoritative lease instead of independently extending it when a queued
// packet is processed later. Familiars remain a separate transient population.
void syncMembers(State &s, uint32_t now) {
    for (auto &x : s.soul) {
        if (!x.used) continue;
        if (x.member) {
            const coven::Peer *peer = memberPeer(s, x.mac);
            if (!peer) { x = Soul{}; continue; }
            x.seen = peer->seen;
            x.degree = peer->packet.degree;
        } else if (radio_music::age(now, x.seen) >= GONE_MS) {
            x = Soul{};
        }
    }
}

int midiOf(int degree) {
    const Scale &sc = SCALES[S->scale];
    return S->rootPc + 12 * (S->oct + 1) + sc.steps[degree % 7] + 12 * (degree / 7);
}

void strike(float hz, float lvl) {
    Command c; c.hz = hz; c.level = lvl;
    if (!S->commands.push(c)) ++S->droppedCommands;
}
void publish() {
    State &s = *S;
    s.controls.publish({dsp::midiHz(midiOf(0) - 12), dsp::midiHz(midiOf(s.degree)),
                        fminf(1.f, (s.session.members() - 1) * 0.34f), s.hush});
}
void render(int16_t *out, int n) {
    Audio &s = S->audio;
    S->controls.consume(s.params);
    if (s.params.hush) s.rd.start("", 0);
    Command c;
    // Bound callback work even if UI floods commands. No allocation/wait.
    for (int k = 0; k < 7 && S->commands.pop(c); ++k) {
        if (c.kind == Command::Bell) {
            s.bell.hz(c.hz); s.bellMod.hz(c.hz * 1.4f);
            s.bellEnv.trigger(c.level); s.bellModEnv.trigger(1.f);
        } else if (!s.params.hush) {
            s.rd.hz = c.hz; s.rd.sylSec = 0.07f; s.rd.loop = false;
            s.rd.start(c.text);
        }
    }
    for (int i = 0; i < n; ++i) {
        if ((s.ctrl++ & 63) == 0) {
            s.padA.hz(s.params.rootHz * 0.5f);
            s.padB.hz(s.params.rootHz * 0.75f * 1.002f);
            s.mine.hz(s.params.myHz); s.mineB.hz(s.params.myHz * 1.004f);
            s.padAmp += ((0.18f + 0.22f * s.params.fullness) - s.padAmp) * 0.01f;
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
    S->telemetry.publish({s.rd.len, s.rd.idx});
}

void enter() {
    if (!S) S = new (std::nothrow) State();
    if (!S) { net::radioStop(); return; } // release a previous utility's retained stack
    S->ready = net::nowBegin();
    net::nowMac(S->me);
    S->session.begin(S->me, hal::ms());
    S->audio.bellEnv.time(3.2f); S->audio.bellModEnv.time(0.7f);
    S->audio.v.gain = 0.8f;
    S->beatAt = hal::ms();
    publish(); hal::setAudio(render);
}
void leave() {
    hal::setAudio(nullptr);
    net::radioStop();
    delete S; S = nullptr;
}
void send(uint8_t kind) {
    if (!S->ready) return;
    uint8_t bytes[coven::WireSize];
    S->session.frame(bytes, kind, hal::ms());
    net::nowSend(bytes, sizeof bytes);
}
void call(uint32_t now) {
    State &s = *S;
    if (s.called && uint32_t(now - s.callAt) < 700) return;
    s.called = true; s.callAt = now; send(1);
    strike(dsp::midiHz(midiOf(s.degree) + 12), 0.5f);
}

bool onKey(const hal::Key &k) {
    if (!S) return false;
    State &s = *S;
    if (k.fn && (k.nav == hal::N_LEFT || k.nav == hal::N_RIGHT)) {
        if (s.session.isLeader()) s.session.root = uint8_t((s.session.root + (k.nav == hal::N_RIGHT ? 1 : 11)) % 12);
        s.rootPc = s.session.root; publish(); return true;
    }
    if (k.ch) {
        const char *p = strchr(PIANO, k.ch);
        if (p) { s.degree = int(p - PIANO); s.session.degree = uint8_t(s.degree); publish(); return true; }
        if (k.ch >= '1' && k.ch < '1' + N_SCALES) {
            if (s.session.isLeader()) s.session.scale = uint8_t(k.ch - '1');
            s.scale = s.session.scale; // followers keep the circle's mode
            return true;
        }
    }
    switch (k.ch) {
    case 'z': if (s.oct > 1) s.oct--; break;
    case 'x': if (s.oct < 4) s.oct++; break;
    case 'm': s.hush = !s.hush; break;
    case ' ':
        call(hal::ms());
        break;
    }
    publish(); return true;
}

Soul *find(const uint8_t *mac, uint32_t now, bool member) {
    State &s = *S;
    Soul *free = nullptr, *oldest = nullptr;
    int familiars = 0;
    for (auto &x : s.soul) {
        if (x.used && !memcmp(x.mac, mac, 6)) return &x;
        if (!x.used && !free) free = &x;
        if (x.used && !x.member) {
            ++familiars;
            if (!oldest || radio_music::age(now, x.seen) > radio_music::age(now, oldest->seen)) oldest = &x;
        }
    }
    // Unrelated traffic must never evict a musical member.
    Soul *n = (!member && familiars >= 6) ? oldest : free ? free : oldest;
    if (!n) return nullptr;
    *n = Soul(); n->used = true; n->member = member; memcpy(n->mac, mac, 6);
    uint32_t h = mac[3] * 131u + mac[4] * 31u + mac[5];
    n->hz = 260.f + (h % 9) * 16.f; n->seen = now;
    return n;
}
void listen(uint32_t now) {
    State &s = *S;
    s.session.tick(now);
    net::NowPkt p;
    // At most eight frames per display tick: bounded unrelated-radio work.
    for (int budget = 0; budget < 8 && net::nowPoll(p); ++budget) {
        if (!memcmp(p.mac, s.me, 6)) continue;
        bool cv = p.len >= 3 && !memcmp(p.data, "CV1", 3);
        if (cv) {
            auto result = s.session.receive(p.mac, p.data, p.len, now, radio_music::age(now, p.receivedAt));
            if (result != coven::Receive::Presence && result != coven::Receive::Call) continue;
            Soul *x = find(p.mac, now, true);
            if (!x) continue;
            x->rssi = p.rssi; x->member = true; x->degree = p.data[6];
            if (result == coven::Receive::Call && (!s.called || uint32_t(now - s.callAt) >= 700)) {
                s.called = true; s.callAt = now;
                strike(dsp::midiHz(midiOf(x->degree) + 12), 0.5f);
            }
            continue;
        }
        if (!coven::validMac(p.mac)) continue;
        Soul *x = find(p.mac, now, false);
        if (!x || x->member) continue;
        x->seen = now; x->rssi = p.rssi;
        char clean[96];
        size_t n = coven::dyadText(p.data, p.len, clean, sizeof clean);
        if (n) {
            uint32_t hash = 2166136261u;
            for (size_t i = 0; i < n; ++i) hash = (hash ^ uint8_t(clean[i])) * 16777619u;
            if ((x->talker && (hash == x->lineHash || uint32_t(now - x->spokeAt) < 2500)) ||
                (s.spoken && uint32_t(now - s.spokenAt) < 1000)) continue;
            x->talker = true; x->lineHash = hash; x->spokeAt = now;
            s.spoken = true; s.spokenAt = s.whisperAt = now;
            memcpy(s.whisper, clean, n + 1);
            // Local anonymous seat label, never an address suffix.
            snprintf(s.whisperBy, sizeof s.whisperBy, "kin%u", unsigned(x - s.soul + 1));
            if (!s.hush) {
                Command c; c.kind = Command::Speech; c.hz = x->hz;
                memcpy(c.text, clean, n + 1);
                if (!s.commands.push(c)) ++s.droppedCommands;
            }
        } else if (p.len >= 3 && !memcmp(p.data, "DY1", 3)) {
            continue; // malformed text is not a musical event
        } else if (uint32_t(now - x->rangAt) > 2500) {
            x->rangAt = now;
            if (!s.hush) strike(x->hz * 4.f, 0.06f);
        }
    }
    syncMembers(s, now);
    s.rootPc = s.session.root; s.scale = s.session.scale;
    s.eldest = s.session.isLeader(); s.beatAt = s.session.beatAt;
    s.telemetry.consume(s.voiceView);
}

bool pointer(const InstrumentPointer &p) {
    if (!S || !p.down) return S != nullptr;
    State &s = *S;
    float dx = p.x - 62.f, dy = p.y - 64.f;
    float r2 = dx * dx + dy * dy;
    if (r2 < 12 * 12) { if (p.start) call(p.ms); return true; }
    if (r2 <= 48 * 48) {
        float angle = atan2f(dy, dx) - 1.5707963f;
        if (angle < 0) angle += dsp::PI2;
        s.degree = int(angle * 13.f / dsp::PI2 + 0.5f) % 13;
        s.session.degree = uint8_t(s.degree); publish(); return true;
    }
    if (p.x >= 128 && p.start) {
        if (p.y >= 36 && p.y < 47) {
            if (s.session.isLeader()) s.session.scale = uint8_t((s.session.scale + 1) % N_SCALES);
        } else if (p.y >= 47 && p.y <= 61) {
            if (s.session.isLeader()) s.session.root = uint8_t((s.session.root + 1) % 12);
        } else return false;
        s.rootPc = s.session.root; s.scale = s.session.scale; publish(); return true;
    }
    return false;
}

void draw(uint32_t now) {
    if (!S) { starved("COVEN", "coven"); return; }
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
    s.eldest = s.session.isLeader();
    int familiars = 0;
    for (auto &x : s.soul) familiars += x.used && !x.member;

    // the pulse: whoever's turn it is rings
    if (s.beat != s.session.beat) {
        s.beat = s.session.beat;
        s.lastRinger = s.ringer; s.ringer = seat[s.beat % ns];
        if (s.ringer == -1) strike(dsp::midiHz(midiOf(s.degree)), 0.3f);
        else {
            Soul &m = s.soul[s.ringer];
            strike(dsp::midiHz(midiOf(m.degree)), 0.22f); m.rangAt = now;
        }
    }
    if (uint32_t(now - s.sentAt) >= 1000) { s.sentAt = now; send(0); }
    publish();
    if (uint32_t(now - s.logAt) >= 2000) {
        s.logAt = now;
        const auto q = net::nowStatus();
        hal::debugf("coven ready=%d members=%u queue=%u high=%u rxdrop=%u invalid=%u replay=%u error_ms=%d events_drop=%u",
                    q.ready, unsigned(s.session.members()), unsigned(q.queueDepth), unsigned(q.queueHighWater),
                    unsigned(q.dropped), unsigned(s.session.counters.invalid), unsigned(s.session.counters.replay),
                    int(s.session.lastErrorMs), unsigned(s.droppedCommands));
    }

    // ---- the circle
    const int cx = 62, cy = 64, R = 32;
    uint32_t sinceCall = now - s.callAt;
    if (s.called && sinceCall < 1200) cv.drawCircle(cx, cy, R + sinceCall / 40, blend(C_GLOW, C_VOID, 255 - sinceCall * 255 / 1200));
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
    text(!s.ready ? "radio unavailable" : s.eldest ? "leader: mode+" : "following: lock", px, 38, s.eldest ? C_GLOW : C_EMBER);
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
        // reveal as spoken; the reader may still be on an older line for a
        // block, so never trust its index past this line's end
        int wl = strlen(s.whisper);
        int shown = s.hush || !s.voiceView.len ? wl : s.voiceView.idx + 1;
        if (shown < 0) shown = 0;
        if (shown > wl) shown = wl;
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
    footer(s.eldest ? "a-k note spc call Fn+L/R root" : "a-k note spc call root locked",
           s.eldest ? "circle note / centre call" : "local note / root+mode locked");
}
} // namespace

const Rite RITE_COVEN = {"COVEN", "coven", "a circle of devices, one drone", enter, leave, onKey, draw, pointer};
