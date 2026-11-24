// VISIONS — pixel visions in the blood ramp: plasma, an Oblivion-gate
// tunnel, ash-fire, Daedric glyph rain, a heptagram kaleidoscope, and feedback trails.
// Each one hums a little; s toggles sound.
//
// keys 1-6 select   l layers   tab/enter next   ,/ speed   space pause   s sound   f trail format
#ifdef GRIM_CYD
#include "../cyd_controls.h"
#endif
#include "../daedric_glyphs.h"
#include "../dsp.h"
#include "../rite.h"
#include "../theme.h"
#include <algorithm>
#include <array>
#include <math.h>
#include <memory>
#include <new>
#include <stdio.h>
#include <string.h>
#if defined(GRIM_DEVICE) && defined(GRIM_CYD_DIAGNOSTICS)
#include <Arduino.h>
#include <esp_heap_caps.h>
#endif

using namespace theme;

namespace {
constexpr int VW = 120, VH = 58, OY = 19; // half-res field, drawn 2x2
#ifdef GRIM_CYD
static_assert(OY == cyd_controls::VisionsArtTop);
#endif
const char *NAMES[] = {"plasma", "gate", "ashfire", "rain", "kaleido", "trails"};
enum Vision { Plasma, Gate, Ashfire, Rain, Kaleido, Trails, N_VIS };
constexpr Vision LayerBase[N_VIS] = {Plasma, Gate, Ashfire, Ashfire, Plasma, Gate};
const char *const LayerTitles[] = {"plasma/kaleido", "gate/trails", "ashfire/rain"};
constexpr int LayerCount = sizeof(LayerTitles) / sizeof(*LayerTitles);
constexpr int ArtHeight = hal::H - OY;
#if defined(GRIM_DEVICE) && defined(GRIM_CYD_DIAGNOSTICS)
constexpr auto HeapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
const char *const LayerNames[] = {"plasma-kaleido", "gate-trails", "ashfire-rain"};
#endif

constexpr uint16_t to444(uint16_t colour) {
    return ((colour >> 4) & 0xf00) | ((colour >> 3) & 0x0f0) | ((colour >> 1) & 0x00f);
}

constexpr uint16_t to565(uint16_t colour) {
    const int r = colour >> 8, g = (colour >> 4) & 15, b = colour & 15;
    return ((r * 2 + (r >> 3)) << 11) | ((g * 4 + (g >> 2)) << 5) | (b * 2 + (b >> 3));
}

struct TrailHistory {
    // Small independent allocations tolerate fragmented internal RAM on CYD.
    static constexpr int BandRows = 29;
    static_assert(ArtHeight % BandRows == 0 && hal::W % 2 == 0);
    std::array<std::unique_ptr<uint8_t[]>, ArtHeight / BandRows> bands;
    bool rgb444 = true;
    bool indexed = false;
    bool fresh = true;
    bool decayNext = true;

    size_t stride() const {
        if (indexed) return hal::W;
        return rgb444 ? hal::W * 3 / 2 : hal::W * 2;
    }
    bool ready() const { return bands.front() != nullptr; }
    void release() { for (auto &band : bands) band.reset(); }
    uint8_t *row(int y) { return bands[y / BandRows].get() + (y % BandRows) * stride(); }

    uint16_t read(const uint8_t *row, int x) const {
        if (!rgb444) return row[2 * x] | (row[2 * x + 1] << 8);
        const uint8_t *pair = row + (x / 2) * 3;
        return x % 2 ? (pair[1] >> 4) | (pair[2] << 4) : pair[0] | ((pair[1] & 15) << 8);
    }

    void write(uint8_t *row, int x, uint16_t colour) const {
        if (indexed) {
            row[x] = std::max<int>(row[x], colour);
            return;
        }
        if (!rgb444) {
            row[2 * x] = colour & 255;
            row[2 * x + 1] = colour >> 8;
            return;
        }
        uint8_t *pair = row + (x / 2) * 3;
        if (x % 2) {
            pair[1] = (pair[1] & 15) | ((colour & 15) << 4);
            pair[2] = colour >> 4;
        } else {
            pair[0] = colour & 255;
            pair[1] = (pair[1] & 240) | (colour >> 8);
        }
    }

    bool allocate(bool useIndexed = false) {
        release();
        indexed = useIndexed;
        const size_t bandBytes = stride() * BandRows;
#if defined(GRIM_DEVICE) && defined(GRIM_CYD_DIAGNOSTICS)
        const auto before = heap_caps_get_free_size(HeapCaps);
#endif
        for (auto &band : bands) {
            band.reset(new (std::nothrow) uint8_t[bandBytes]);
            if (!band) {
                release();
                return false;
            }
            memset(band.get(), 0, bandBytes);
        }
#if defined(GRIM_DEVICE) && defined(GRIM_CYD_DIAGNOSTICS)
        const auto after = heap_caps_get_free_size(HeapCaps);
        Serial.printf("Diviner trails format=%s payload=%u heap_before=%u heap_after=%u heap_delta=%d largest=%u\n",
                      indexed ? "LUT8" : rgb444 ? "RGB444" : "RGB565", unsigned(bandBytes * bands.size()),
                      unsigned(before), unsigned(after), int(before) - int(after),
                      unsigned(heap_caps_get_largest_free_block(HeapCaps)));
#endif
        if (!indexed) {
            const uint16_t background = rgb444 ? to444(C_VOID) : C_VOID;
            for (int y = 0; y < ArtHeight; ++y) {
                uint8_t *pixels = row(y);
                for (int x = 0; x < hal::W; ++x) write(pixels, x, background);
            }
        }
        fresh = true;
        decayNext = true;
        return true;
    }
};

struct Tunnel {
    uint8_t ang[VH][VW], dist[VH][VW];
};

struct Fire {
    uint8_t values[VH + 1][VW]{};
    bool seeded = false;
};

struct State {
    Vision vis = Plasma;
    float t = 0, speed = 1.f;
    bool paused = false, sound = true, layered = true;
    Vision base() const { return layered ? LayerBase[vis] : vis; }
    TrailHistory history;
    std::unique_ptr<Tunnel> tunnel;
    std::unique_ptr<Fire> fire;
    struct Drop {
        float y, v;
        char g;
        uint8_t len;
    } drops[24]{};
    dsp::Noise visualRng;
    // sound
    dsp::Noise rng;
    dsp::SVF windF;
    dsp::Osc lfo, tone, tone2;
    volatile int which = 0;
} *S = nullptr;

int8_t sin8(uint8_t a) { // -127..127
    static int8_t t[256];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 256; ++i) t[i] = (int8_t)(127 * sinf(i * 6.2831853f / 256));
        built = true;
    }
    return t[a];
}

void cell(int x, int y, uint16_t c) { hal::canvas().fillRect(x * 2, OY + y * 2, 2, 2, c); }

void plasma(bool layered) {
    int t = (int)(S->t * 40);
    for (int y = 0; y < VH; ++y)
        for (int x = 0; x < VW; ++x) {
            int v = sin8(x * 3 + t) + sin8(y * 5 - t * 2 / 3) + sin8((x + y) * 2 + t / 2) +
                    sin8((int)(sqrtf((x - 60) * (x - 60) + (y - 29) * (y - 29) * 4) * 4) - t);
            uint8_t u = (uint8_t)((v + 508) * 255 / 1016);
            const int intensity = sin8(u + t / 3) + 128;
            cell(x, y, ramp(layered ? intensity * 3 / 5 : intensity));
        }
}

void buildTunnel(Tunnel &b) {
    for (int y = 0; y < VH; ++y)
        for (int x = 0; x < VW; ++x) {
            float dx = x - VW / 2 + 0.5f, dy = (y - VH / 2 + 0.5f) * 1.1f;
            float d = sqrtf(dx * dx + dy * dy) + 0.5f;
            b.dist[y][x] = (uint8_t)((int)(900.f / d) & 255);
            b.ang[y][x] = (uint8_t)(128 * atan2f(dy, dx) / 3.14159f);
        }
}

void gate(const Tunnel &b) {
    int t = (int)(S->t * 60);
    for (int y = 0; y < VH; ++y)
        for (int x = 0; x < VW; ++x) {
            uint8_t d = b.dist[y][x], a = b.ang[y][x];
            uint8_t u = d + t, v = a * 2 + t / 4;
            int pat = ((u >> 4) ^ (v >> 4)) & 1 ? 150 : 40;
            pat += sin8(u * 2 + v) / 3;
            int dx = x - VW / 2, dy = y - VH / 2;
            int fade = dx * dx + dy * dy * 4; // darker in the throat
            int k = fade > 2400 ? 255 : fade * 255 / 2400;
            int val = pat * k / 255;
            cell(x, y, ramp((uint8_t)(val < 0 ? 0 : val > 255 ? 255 : val)));
        }
    // the gate's rim
    hal::canvas().drawEllipse(120, OY + 58, 112, 56, C_BLOOD);
}

void ashfire(Fire &fire, bool layered) {
    auto &f = fire.values;
    if (!S->paused || !fire.seeded) {
        auto &rng = S->visualRng;
        for (int x = 0; x < VW; ++x) f[VH][x] = (rng.next() % 100) < 55 ? 255 : 60;
        for (int y = 0; y < VH; ++y) {
            for (int x = 0; x < VW; ++x) {
                const int sx = std::clamp(x + int(rng.next() % 3) - 1, 0, VW - 1);
                const int y2 = std::min(y + 2, VH);
                int v = (f[y + 1][sx] * 2 + f[y + 1][x] + f[y2][x]) / 4;
                v -= 3 + (rng.next() & 3);
                f[y][x] = v < 0 ? 0 : v;
            }
        }
        fire.seeded = true;
    }
    for (int y = 0; y < VH; ++y)
        for (int x = 0; x < VW; ++x) cell(x, y, ramp(layered ? f[y][x] * 3 / 4 : f[y][x]));
}

void glyphRain(bool styled) {
    Canvas &cv = hal::canvas();
    const int lanes = 24, lw = 10;
    for (int i = 0; i < lanes; ++i) {
        const int x = i * lw;
        auto &d = S->drops[i];
        if (d.v == 0 || d.y > 140) {
            d.y = -(float)(S->visualRng.next() % 80);
            d.v = 0.6f + (S->visualRng.next() % 100) / 70.f;
            d.len = 3 + S->visualRng.next() % 5;
        }
        if (!S->paused) d.y += d.v * S->speed;
        for (int j = 0; j < d.len; ++j) {
            int y = (int)d.y - j * 16;
            if (y < OY - 14 || y > 134) continue;
            char g[2] = {(char)('A' + ((i * 7 + j * 3 + (int)(d.y / 16)) % 26)), 0};
            uint16_t c = C_GLOW;
            if (j) {
                const uint16_t bg = styled ? cv.readPixel(x + lw / 2, std::clamp(y + DAEDRIC_S.h / 2, OY, hal::H - 1))
                                           : C_VOID;
                c = blend(C_RUST, bg, 255 - j * 255 / d.len);
            }
            glyphs(DAEDRIC_S, g, x, y, c);
        }
    }
}

void kaleido() {
    Canvas &cv = hal::canvas();
    float t = S->t;
    const int cx = 120, cy = OY + 58;
    for (int ring = 0; ring < 5; ++ring) {
        float r = 12 + ring * 11 + 4 * sinf(t * 1.3f + ring);
        float rot = t * (ring & 1 ? -0.4f : 0.3f) + ring * 0.4f;
        int step = 2 + ring % 2;
        int px[7], py[7];
        for (int i = 0; i < 7; ++i) {
            float a = rot + i * 6.2831853f / 7;
            px[i] = cx + (int)(cosf(a) * r * 1.6f);
            py[i] = cy + (int)(sinf(a) * r);
        }
        uint16_t c = ramp((uint8_t)(90 + ring * 30 + 40 * sinf(t + ring)));
        for (int i = 0; i < 7; ++i) cv.drawLine(px[i], py[i], px[(i + step) % 7], py[(i + step) % 7], c);
        for (int i = 0; i < 7; ++i) diamond(px[i], py[i], 1 + (ring == 4), C_GLOW);
    }
    // seven mirrored sparks
    for (int i = 0; i < 7; ++i) {
        float a = t * 0.9f + i * 6.2831853f / 7;
        float r = 30 + 22 * sinf(t * 2.1f);
        cv.fillCircle(cx + (int)(cosf(a) * r * 1.6f), cy + (int)(sinf(a) * r), 2, C_BONE);
    }
}

int fadeChannel(int channel, int target, int step) {
    return channel + std::clamp(target - channel, -step, step);
}

uint16_t fade565(uint16_t colour, uint16_t target) {
    return (fadeChannel(colour >> 11, target >> 11, 2) << 11) |
           (fadeChannel((colour >> 5) & 63, (target >> 5) & 63, 4) << 5) |
           fadeChannel(colour & 31, target & 31, 2);
}

uint16_t fade444(uint16_t colour, uint16_t target) {
    return (fadeChannel(colour >> 8, target >> 8, 1) << 8) |
           (fadeChannel((colour >> 4) & 15, (target >> 4) & 15, 1) << 4) |
           fadeChannel(colour & 15, target & 15, 1);
}

struct TrailPoint { int x, y; };

TrailPoint comet(float t, int index) {
    const float phase = index * 2.0943951f;
    return {hal::W / 2 + int(52 * sinf(t * 1.6f + phase) + 12 * sinf(t * 3.1f - phase)),
            ArtHeight / 2 + int(37 * cosf(t * 1.25f - phase) + 9 * sinf(t * 2.6f + phase))};
}

void stamp(TrailHistory &history, TrailPoint point, uint16_t colour, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        const int y = point.y + dy;
        if (y < 0 || y >= ArtHeight) continue;
        uint8_t *pixels = history.row(y);
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = point.x + dx;
            if (x >= 0 && x < hal::W && dx * dx + dy * dy <= radius * radius) history.write(pixels, x, colour);
        }
    }
}

void feedbackTrails(Canvas &cv, State &s) {
    auto &history = s.history;
    if (!history.ready()) {
        if (!s.layered) cv.fillRect(0, OY, hal::W, ArtHeight, C_VOID);
        textCentre("not enough memory for trails", 120, 72, C_DIM);
        return;
    }

    const uint16_t background = C_VOID;
    if (!s.paused || history.fresh) {
        const uint16_t colours[] = {C_EMBER, C_GLOW, C_BONE};
        constexpr uint8_t strengths[] = {207, 231, 255};
        for (int i = 0; i < 3; ++i) {
            const TrailPoint head = comet(s.t, i);
            const TrailPoint previous = history.fresh ? head : comet(s.t - 0.033f * s.speed, i);
            const int dx = head.x - previous.x, dy = head.y - previous.y;
            const int steps = std::max(1, std::max(abs(dx), abs(dy)));
            const uint16_t glow = blend(colours[i], background, 170);
            const uint16_t outer = history.indexed ? strengths[i] - 32 : history.rgb444 ? to444(glow) : glow;
            const uint16_t core = history.indexed ? strengths[i] : history.rgb444 ? to444(colours[i]) : colours[i];
            for (int step = 0; step <= steps; ++step) {
                stamp(history, {previous.x + dx * step / steps, previous.y + dy * step / steps}, outer, 3);
            }
            stamp(history, head, core, 2);
        }
        history.fresh = false;
    }

    // Decay the history and unpack one scanline at a time into the existing canvas.
    const bool decay = !s.paused && history.decayNext;
    if (!s.paused) history.decayNext = !history.decayNext;
    if (history.indexed) {
        // The intensity also controls opacity. Zero leaves the Gate pixel untouched.
        for (int y = 0; y < ArtHeight; ++y) {
            uint8_t *intensities = history.row(y);
            for (int x = 0; x < hal::W; ++x) {
                auto &intensity = intensities[x];
                if (!intensity) continue;
                if (decay) intensity = std::max(0, intensity - 12);
                if (intensity) {
                    const int cy = OY + y;
                    cv.drawPixel(x, cy, blend(ramp(intensity), cv.readPixel(x, cy), intensity));
                }
            }
        }
        return;
    }

    lgfx::rgb565_t line[hal::W];
    const uint16_t target = history.rgb444 ? to444(background) : background;
    for (int y = 0; y < ArtHeight; ++y) {
        uint8_t *pixels = history.row(y);
        for (int x = 0; x < hal::W; ++x) {
            uint16_t colour = history.read(pixels, x);
            if (decay && colour != target) {
                colour = history.rgb444 ? fade444(colour, target) : fade565(colour, target);
                history.write(pixels, x, colour);
            }
            line[x] = history.rgb444 ? to565(colour) : colour;
        }
        cv.pushImage(0, OY + y, hal::W, 1, line);
    }
}

void render(int16_t *out, int n) {
    State &s = *S;
    for (int i = 0; i < n; ++i) {
        float x = 0;
        if (s.sound) {
            float l = s.lfo.sine();
            if ((i & 31) == 0) s.windF.set(250.f + 700.f * (0.5f + 0.5f * l) * (1 + s.which * 0.3f), 0.9f);
            x = s.windF.bp(s.rng.white()) * 0.16f + (s.tone.sine() + s.tone2.sine()) * 0.03f * (1.2f + l);
        }
        out[i] = dsp::toPcm(x);
    }
}

void retone() {
    static const float ROOTS[N_VIS] = {55.f, 41.2f, 36.7f, 61.7f, 49.f, 65.4f};
    const Vision base = S->base();
    S->tone.hz(ROOTS[base]);
    S->tone2.hz(ROOTS[base] * 1.498f);
    S->which = base;
}

void prepareVision() {
    State &s = *S;
    const Vision base = s.base();
    const bool indexed = s.layered && base == Gate;
    const bool wantsHistory = indexed || base == Trails;
    if (base != Gate) s.tunnel.reset();
    if (base != Ashfire) s.fire.reset();
    if (!wantsHistory || s.history.indexed != indexed) s.history.release();

    if (base == Gate && !s.tunnel) {
        s.tunnel.reset(new (std::nothrow) Tunnel);
        if (s.tunnel) buildTunnel(*s.tunnel);
    }
    if (base == Ashfire && !s.fire) s.fire.reset(new (std::nothrow) Fire);
    if (wantsHistory && !s.history.ready() && (base != Gate || s.tunnel)) s.history.allocate(indexed);
#if defined(GRIM_DEVICE) && defined(GRIM_CYD_DIAGNOSTICS)
    Serial.printf("Diviner layers effect=%s heap=%u largest=%u\n",
                  s.layered ? LayerNames[base] : NAMES[base], unsigned(heap_caps_get_free_size(HeapCaps)),
                  unsigned(heap_caps_get_largest_free_block(HeapCaps)));
#endif
}

void enter() {
#ifdef GRIM_CYD
    static State state;
    S = &state;
#else
    if (!S) S = new State();
#endif
    prepareVision();
    S->lfo.hz(0.07f);
    retone();
    hal::setAudio(render);
}
void leave() {
    S->history.release();
    S->tunnel.reset();
    S->fire.reset();
}

bool onKey(const hal::Key &k) {
    State &s = *S;
    const Vision previous = s.vis;
    const bool direct = k.ch >= '1' && k.ch < '1' + N_VIS;
    const int count = s.layered ? LayerCount : N_VIS;
    if (direct) s.vis = static_cast<Vision>(k.ch - '1');
    else if (k.nav == hal::N_TAB || k.nav == hal::N_ENTER || k.nav == hal::N_DOWN) s.vis = static_cast<Vision>((s.base() + 1) % count);
    else if (k.nav == hal::N_UP) s.vis = static_cast<Vision>((s.base() + count - 1) % count);
    else if (k.nav == hal::N_RIGHT) s.speed = fminf(4.f, s.speed + 0.25f);
    else if (k.nav == hal::N_LEFT) s.speed = fmaxf(0.25f, s.speed - 0.25f);
    else if (k.ch == ' ') s.paused = !s.paused;
    else if (k.ch == 's') s.sound = !s.sound;
    else if (k.ch == 'l') s.layered = !s.layered;
    else if (k.ch == 'f') {
        if (s.base() == Trails) {
            const bool hadHistory = s.history.ready();
            s.history.rgb444 = !s.history.rgb444;
            if (!s.history.allocate() && hadHistory) {
                s.history.rgb444 = !s.history.rgb444;
                s.history.allocate();
            }
        } else {
            s.history.rgb444 = !s.history.rgb444;
        }
    }
    if (direct || s.vis != previous || k.ch == 'l') prepareVision();
    retone();
    return true;
}

void draw(uint32_t) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    const Palette &colours = palette();
    const Vision base = s.base();
    const bool sparseArt = base == Rain || base == Kaleido;
    const bool styledArt = sparseArt && colours.backgroundStyle != BackgroundStyle::Flat;
    if (styledArt) {
        // A single backdrop keeps the gradient continuous across the heading.
        drawBackground(cv);
        cv.setClipRect(0, OY, hal::W, VH * 2);
    } else if (sparseArt) {
        cv.fillRect(0, OY, hal::W, VH * 2, C_VOID);
        cv.setClipRect(0, OY, hal::W, ArtHeight);
    }
    if (!s.paused) s.t += 0.033f * s.speed;
    if ((base == Gate && !s.tunnel) || (base == Ashfire && !s.fire)) {
        cv.fillRect(0, OY, hal::W, ArtHeight, C_VOID);
        textCentre("not enough memory for this vision", 120, 72, C_DIM);
    } else {
        switch (base) {
        case Plasma: plasma(s.layered); break;
        case Gate: gate(*s.tunnel); break;
        case Ashfire: ashfire(*s.fire, s.layered); break;
        case Rain: glyphRain(styledArt); break;
        case Kaleido: kaleido(); break;
        case Trails: feedbackTrails(cv, s); break;
        case N_VIS: break;
        }
        if (s.layered) {
            cv.setClipRect(0, OY, hal::W, ArtHeight);
            switch (base) {
            case Plasma: kaleido(); break;
            case Gate: feedbackTrails(cv, s); break;
            case Ashfire: glyphRain(true); break;
            default: break;
            }
        }
    }
    if (sparseArt || s.layered) cv.clearClipRect();
    if (!styledArt) drawSceneBackground(cv, 0, 0, hal::W, OY, colours);
    header("VISIONS", "visions");
    const char *title = s.layered ? LayerTitles[base] : NAMES[base];
    if (base == Trails) title = s.history.rgb444 ? "trails RGB444" : "trails RGB565";
    textRight(title, 198, 5, C_DIM);
}
} // namespace

#ifdef GRIM_CYD
bool visionsUsesRgb444() { return !S || S->history.rgb444; }
bool visionsAreLayered() { return S && S->layered; }
#endif

const Rite RITE_VISIONS = {"VISIONS", "visions", "plasma, fire, rain, feedback trails", enter, leave, onKey, draw};
