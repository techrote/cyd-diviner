// TILT — a marble in a singing bowl. Tilt the Cardputer and the marble rolls;
// where it rests is the music. Left/right walks a four-voice chord up and
// down the scale like a slow harp, far/near opens and closes the filter,
// speed shimmers. The marble chimes when it strikes the rim (pitch from where
// it hits) and a shake scatters bells and sparks.
//
// keys  1-4 mode   z/x octave   c level here   f flip axes   r recentre
//       arrows nudge the marble (and stand in for tilt without an IMU)
//       space lock
#include "../dsp.h"
#include "../rite.h"
#include "../theme.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace theme;

namespace {
struct Scale {
    const char *name;
    int8_t steps[7];
};
const Scale SCALES[] = {
    {"dorian", {0, 2, 3, 5, 7, 9, 10}},
    {"lydian", {0, 2, 4, 6, 7, 9, 11}},
    {"aeolian", {0, 2, 3, 5, 7, 8, 10}},
    {"hira", {0, 2, 3, 7, 8, 12, 14}}, // hirajoshi, padded to 7 with octaves
};
constexpr int N_SCALES = 4;
constexpr int NV = 4, NB = 4, NSPARK = 14, NTRAIL = 16;

struct Voice {
    dsp::Osc a, b;
    float hz = 110, hzT = 110;
    int note = 0;
};
struct Bell {
    dsp::Osc car, mod;
    dsp::Decay env, modEnv;
};
struct Spark {
    float x, y, vx, vy;
    uint8_t life;
};

struct State {
    int scale = 0, root = 45; // A2
    int flip = 0;
    bool lock = false, imu = false;
    // marble, in the unit disc
    float mx = 0, my = 0, vx = 0, vy = 0;
    float ax = 0, ay = 0, calX = 0, calY = 0, lastRawX = 0, lastRawY = 0, nudgeX = 0, nudgeY = 0;
    float shake = 0;
    uint32_t shakeAt = 0, rimAt = 0;
    float rimAng = 0;
    int degLit = -1;
    float trX[NTRAIL] = {}, trY[NTRAIL] = {};
    int trN = 0;
    Spark sp[NSPARK] = {};
    // sound
    Voice v[NV];
    volatile float cutoffT = 800, shimmer = 0;
    float cutoff = 800;
    dsp::SVF filt;
    dsp::Osc trem;
    Bell bl[NB];
    int nextBell = 0;
    volatile float bellQ[4] = {};
    volatile float bellLvl[4] = {};
    volatile int bellW = 0;
    int bellR = 0;
    dsp::Delay<4096> dl;
    float dlLp = 0;
    uint32_t ctrl = 0;
} *S = nullptr;

float degHz(int deg, int octUp) {
    const Scale &sc = SCALES[S->scale];
    int o = deg >= 0 ? deg / 7 : -((6 - deg) / 7);
    int d = deg - 7 * o;
    return dsp::midiHz(S->root + sc.steps[d] + 12 * (o + octUp));
}

void ring(float hz, float lvl) {
    // UI -> audio: a tiny ring of pending strikes
    int w = S->bellW;
    S->bellQ[w & 3] = hz;
    S->bellLvl[w & 3] = lvl;
    S->bellW = w + 1;
}

void render(int16_t *out, int n) {
    State &s = *S;
    for (int i = 0; i < n; ++i) {
        if ((s.ctrl++ & 31) == 0) {
            for (auto &v : s.v) {
                v.hz += (v.hzT - v.hz) * 0.03f; // a harp-glide between degrees
                float d = 0.0025f + 0.006f * s.shimmer;
                v.a.hz(v.hz * (1.f + d));
                v.b.hz(v.hz * (1.f - d));
            }
            s.cutoff += (s.cutoffT - s.cutoff) * 0.05f;
            s.filt.set(s.cutoff, 0.45f);
            s.trem.hz(3.f + 5.f * s.shimmer);
            while (s.bellR != s.bellW) {
                int r = s.bellR & 3;
                Bell &b = s.bl[s.nextBell];
                s.nextBell = (s.nextBell + 1) % NB;
                b.car.hz(s.bellQ[r]);
                b.mod.hz(s.bellQ[r] * 2.76f);
                b.env.trigger(s.bellLvl[r]);
                b.modEnv.trigger(1.f);
                ++s.bellR;
            }
        }
        float pad = 0;
        for (int k = 0; k < NV; ++k) pad += (s.v[k].a.saw() + s.v[k].b.saw()) * (k == 0 ? 0.32f : 0.22f);
        pad = s.filt.lp(pad) * (1.f - 0.25f * s.shimmer * (0.5f + 0.5f * s.trem.sine()));
        float bells = 0;
        for (auto &b : s.bl) {
            if (b.env.v < 0.0004f) continue;
            bells += b.car.pmSine(b.mod.sine() * 0.18f * b.modEnv.tick()) * b.env.tick();
        }
        float x = pad * 0.5f + bells * 0.3f;
        float wet = s.dl.tap(3800) * 0.5f + s.dl.tap(2470) * 0.3f;
        s.dlLp += (wet - s.dlLp) * 0.2f;
        s.dl.put(x + s.dlLp * 0.58f);
        out[i] = dsp::toPcm(x * 0.85f + s.dlLp * 0.5f);
    }
}

void retarget() {
    State &s = *S;
    // left/right: chord position across two octaves of the scale
    float cp = (s.mx + 1.f) * 0.5f * 9.f;
    int base = (int)roundf(cp);
    for (int k = 0; k < NV; ++k) {
        int deg = base + 2 * k; // stacked thirds
        s.v[k].note = deg;
        s.v[k].hzT = degHz(deg, k == 0 ? -1 : 0);
    }
    // far (up the screen) = brighter
    float br = (1.f - s.my) * 0.5f;
    float sp = sqrtf(s.vx * s.vx + s.vy * s.vy);
    s.shimmer = fminf(1.f, sp * 0.9f);
    s.cutoffT = 180.f + br * br * 2600.f + s.shimmer * 600.f;
}

void enter() {
    if (!S) S = new (std::nothrow) State();
    if (!S) return;
    for (auto &b : S->bl) {
        b.env.time(2.8f);
        b.modEnv.time(0.6f);
    }
    float x = 0, y = 0, z = 1;
    S->imu = hal::accel(x, y, z);
    S->lastRawX = x; // so the first frame isn't read as a shake
    S->lastRawY = y;
    retarget();
    for (auto &v : S->v) v.hz = v.hzT;
    hal::setAudio(render);
}
void leave() {
    delete S;
    S = nullptr;
}

bool onKey(const hal::Key &k) {
    if (!S) return false;
    State &s = *S;
    if (k.ch >= '1' && k.ch < '1' + N_SCALES) { s.scale = k.ch - '1'; return true; }
    switch (k.nav) {
    case hal::N_UP: s.nudgeY -= 0.5f; return true;
    case hal::N_DOWN: s.nudgeY += 0.5f; return true;
    case hal::N_LEFT: s.nudgeX -= 0.5f; return true;
    case hal::N_RIGHT: s.nudgeX += 0.5f; return true;
    default: break;
    }
    switch (k.ch) {
    case 'z': if (s.root > 33) s.root -= 12; break;
    case 'x': if (s.root < 57) s.root += 12; break;
    case 'c': s.calX = s.lastRawX; s.calY = s.lastRawY; break;
    case 'f': s.flip = (s.flip + 1) & 3; s.calX = s.calY = 0; break; // level was in the old axes
    case 'r': s.mx = s.my = s.vx = s.vy = 0; break;
    case ' ': s.lock = !s.lock; break;
    }
    return true;
}

void physics(uint32_t now) {
    State &s = *S;
    float rx = 0, ry = 0, rz = 1;
    s.imu = hal::accel(rx, ry, rz);
    // orientation: four ways to hold it, cycled with f
    float tx = rx, ty = ry;
    if (s.flip & 1) { float t = tx; tx = ty; ty = t; }
    if (s.flip & 2) { tx = -tx; ty = -ty; }
    // shake = how hard the reading jumped since last frame
    float jerk = fabsf(tx - s.lastRawX) + fabsf(ty - s.lastRawY);
    s.lastRawX = tx;
    s.lastRawY = ty;
    s.ax += ((tx - s.calX) - s.ax) * 0.35f;
    s.ay += ((ty - s.calY) - s.ay) * 0.35f;
    if (s.lock) return;
    const float dt = 0.033f;
    float gx = s.ax * 3.2f + s.nudgeX * 12.f, gy = s.ay * 3.2f + s.nudgeY * 12.f;
    s.nudgeX *= 0.6f;
    s.nudgeY *= 0.6f;
    if (s.imu && jerk > 0.55f && now - s.shakeAt > 250) {
        // a shake: kick the marble and scatter bells
        s.shakeAt = now;
        s.shake = fminf(1.f, jerk);
        s.vx += ((rand() % 200) - 100) * 0.02f * s.shake;
        s.vy += ((rand() % 200) - 100) * 0.02f * s.shake;
        for (int b = 0; b < 3; ++b) ring(degHz(rand() % 10, 1), 0.25f + 0.2f * s.shake);
        for (auto &p : s.sp) {
            float a = (rand() % 628) / 100.f, v = 0.6f + (rand() % 100) / 60.f;
            p = {s.mx, s.my, cosf(a) * v, sinf(a) * v, 255};
        }
    }
    s.vx = (s.vx + gx * dt) * 0.985f;
    s.vy = (s.vy + gy * dt) * 0.985f;
    s.mx += s.vx * dt;
    s.my += s.vy * dt;
    float r = sqrtf(s.mx * s.mx + s.my * s.my);
    const float R = 0.92f; // marble radius kept off the rim line
    if (r > R) {
        float nx = s.mx / r, ny = s.my / r;
        float vn = s.vx * nx + s.vy * ny;
        if (vn > 0) {
            s.vx -= 1.6f * vn * nx;
            s.vy -= 1.6f * vn * ny;
            if (vn > 0.12f && now - s.rimAt > 90) {
                // chime: the rim is marked with 14 degrees, two octaves
                float ang = atan2f(ny, nx);
                int deg = (int)roundf((ang + 3.14159f) / 6.28318f * 14.f) % 14;
                ring(degHz(deg, 1), fminf(0.55f, 0.1f + vn * 0.4f));
                s.rimAt = now;
                s.rimAng = ang;
                s.degLit = deg;
            }
        }
        s.mx = nx * R;
        s.my = ny * R;
    }
}

void draw(uint32_t now) {
    if (!S) { starved("TILT", "tilt"); return; }
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("TILT", "tilt");
    physics(now);
    retarget();

    const int cx = 64, cy = 72, R = 46;
    // the bowl: hammered rings, darker toward the centre
    for (int i = 4; i >= 1; --i) cv.drawCircle(cx, cy, R * i / 4, i == 4 ? C_RUST : C_DUSK);
    cv.drawFastHLine(cx - 4, cy, 9, C_CLOT);
    cv.drawFastVLine(cx, cy - 4, 9, C_CLOT);
    // degree marks round the rim
    for (int d = 0; d < 14; ++d) {
        float a = -3.14159f + d * 6.28318f / 14.f;
        int px = cx + (int)(cosf(a) * (R + 3)), py = cy + (int)(sinf(a) * (R + 3));
        bool lit = d == s.degLit && now - s.rimAt < 700;
        diamond(px, py, lit ? 2 : 1, lit ? C_GLOW : (d % 7 == 0 ? C_EMBER : C_BLOOD), true);
    }
    // where gravity points (the tilt itself)
    int gx = cx + (int)(fmaxf(-1.f, fminf(1.f, s.ax * 1.6f)) * R * 0.8f);
    int gy = cy + (int)(fmaxf(-1.f, fminf(1.f, s.ay * 1.6f)) * R * 0.8f);
    cv.drawLine(cx, cy, gx, gy, C_UMBER);
    // rim ripple
    uint32_t ra = now - s.rimAt;
    if (ra < 600) {
        int hx = cx + (int)(cosf(s.rimAng) * R), hy = cy + (int)(sinf(s.rimAng) * R);
        cv.drawCircle(hx, hy, 2 + ra / 50, blend(C_GLOW, C_VOID, (uint8_t)(255 - ra * 255 / 600)));
    }
    // trail + marble
    int px = cx + (int)(s.mx * R), py = cy + (int)(s.my * R);
    s.trX[s.trN % NTRAIL] = px;
    s.trY[s.trN % NTRAIL] = py;
    s.trN++;
    int tn = s.trN < NTRAIL ? s.trN : NTRAIL;
    for (int i = 1; i < tn; ++i) {
        int a = (s.trN - i) % NTRAIL, b = (s.trN - i - 1) % NTRAIL;
        cv.drawLine((int)s.trX[a], (int)s.trY[a], (int)s.trX[b], (int)s.trY[b], ramp((uint8_t)(200 - i * 12)));
    }
    cv.fillCircle(px, py, 5, s.lock ? C_BONE : C_EMBER);
    cv.drawCircle(px, py, 5, C_RUST);
    cv.fillCircle(px - 2, py - 2, 1, C_GLOW); // the shine
    // sparks
    for (auto &p : s.sp) {
        if (!p.life) continue;
        p.x += p.vx * 0.033f;
        p.y += p.vy * 0.033f;
        p.vy += 0.03f;
        p.life = p.life > 12 ? p.life - 12 : 0;
        cv.drawPixel(cx + (int)(p.x * R), cy + (int)(p.y * R), ramp(p.life));
    }

    // ---- panel
    char b[40];
    int qx = 132;
    frame(qx - 4, 22, 110, 96, C_CLOT, C_UMBER);
    text("chord", qx, 26, C_DIM);
    for (int k = 0; k < NV; ++k) {
        const Scale &sc = SCALES[s.scale];
        int deg = s.v[k].note, o = deg >= 0 ? deg / 7 : -((6 - deg) / 7);
        int midi = s.root + sc.steps[deg - 7 * o] + 12 * (o + (k == 0 ? -1 : 0));
        snprintf(b, sizeof b, "%s%d", noteName(midi), midi / 12 - 1);
        text(b, qx + (k % 2) * 50, 38 + (k / 2) * 11, k == 0 ? C_EMBER : C_BONE);
    }
    meter("light", qx, 62, qx + 40, 62, (1.f - s.my) * 0.5f);
    meter("speed", qx, 73, qx + 40, 62, s.shimmer);
    text("mode", qx, 85, C_DIM);
    text(SCALES[s.scale].name, qx + 40, 85, C_BONE);
    bool shook = now - s.shakeAt < 600;
    snprintf(b, sizeof b, "%s %s%d", shook ? "SHAKE" : s.lock ? "LOCK " : s.imu ? "imu  " : "keys ", noteName(s.root),
             s.root / 12 - 1);
    text(b, qx, 97, shook ? C_GLOW : C_EMBER);
    snprintf(b, sizeof b, "flip %d", s.flip);
    text(b, qx, 107, C_DIM);
    footer(s.imu ? "tilt me  c level  f flip  spc lock" : "no imu: arrows roll it  spc lock");
}
} // namespace

const Rite RITE_TILT = {"TILT", "tilt", "a marble in a singing bowl", enter, leave, onKey, draw};
