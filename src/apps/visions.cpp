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
#include "../perf_profile.h"
#include <algorithm>
#include <array>
#include <math.h>
#include <memory>
#include <new>
#include <optional>
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
        if (perf_profile::quiet()) Serial.printf("Diviner trails format=%s payload=%u heap_before=%u heap_after=%u heap_delta=%d largest=%u\n",
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

struct RainDrop {
    float y, v;
    uint8_t len;
};

struct State {
    Vision vis = Plasma;
    float t = 0, speed = 1.f;
    bool paused = false, sound = true, layered = true;
    Vision base() const { return layered ? LayerBase[vis] : vis; }
    TrailHistory history;
    std::unique_ptr<Tunnel> tunnel;
    std::unique_ptr<Fire> fire;
#ifdef GRIM_CYD
    bool nativeFullscreen = false;
#endif
    RainDrop drops[24]{};
    dsp::Noise visualRng;
    // sound
    dsp::Noise rng;
    dsp::SVF windF;
    dsp::Osc lfo, tone, tone2;
    volatile int which = 0;
} *S = nullptr;

const int8_t *sineValues() {
    static int8_t t[256];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 256; ++i) t[i] = (int8_t)(127 * sinf(i * 6.2831853f / 256));
        built = true;
    }
    return t;
}

int8_t sin8(uint8_t a) { return sineValues()[a]; }

void cell(int x, int y, uint16_t c) { hal::canvas().fillRect(x * 2, OY + y * 2, 2, 2, c); }

uint8_t plasmaIntensity(int x, int y, int t) {
    const int dx = x - VW / 2, dy = (y - VH / 2) * 2;
    const int wave = sin8(x * 3 + t) + sin8(y * 5 - t * 2 / 3) + sin8((x + y) * 2 + t / 2) +
                     sin8(int(sqrtf(dx * dx + dy * dy) * 4) - t);
    const int phase = (wave + 508) * 255 / 1016;
    return sin8(phase + t / 3) + 128;
}

void plasma(bool layered) {
    int t = (int)(S->t * 40);
    for (int y = 0; y < VH; ++y)
        for (int x = 0; x < VW; ++x) {
            const int intensity = plasmaIntensity(x, y, t);
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

template <size_t Rows, size_t Cols>
void stepFire(uint8_t (&field)[Rows][Cols], dsp::Noise &rng) {
    const int floor = Rows - 1;
    for (auto &spark : field[floor]) spark = (rng.next() % 100) < 55 ? 255 : 60;
    for (int y = 0; y < floor; ++y) {
        for (int x = 0; x < int(Cols); ++x) {
            const int sx = std::clamp(x + int(rng.next() % 3) - 1, 0, int(Cols) - 1);
            const int y2 = std::min(y + 2, floor);
            int v = (field[y + 1][sx] * 2 + field[y + 1][x] + field[y2][x]) / 4;
            v -= 3 + (rng.next() & 3);
            field[y][x] = v < 0 ? 0 : v;
        }
    }
}

void ashfire(Fire &fire, bool layered) {
    auto &f = fire.values;
    if (!S->paused || !fire.seeded) {
        stepFire(f, S->visualRng);
        fire.seeded = true;
    }
    for (int y = 0; y < VH; ++y)
        for (int x = 0; x < VW; ++x) cell(x, y, ramp(layered ? f[y][x] * 3 / 4 : f[y][x]));
}

template <size_t Lanes>
void glyphRain(RainDrop (&drops)[Lanes], dsp::Noise &rng, float step, bool styled) {
    Canvas &cv = hal::canvas();
    const int lw = hal::W / Lanes;
    for (int i = 0; i < int(Lanes); ++i) {
        const int x = i * lw;
        auto &d = drops[i];
        if (d.v == 0 || d.y > 140) {
            d.y = -(float)(rng.next() % 80);
            d.v = 0.6f + (rng.next() % 100) / 70.f;
            d.len = 3 + rng.next() % 5;
        }
        d.y += d.v * step;
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

void glyphRain(bool styled) {
    glyphRain(S->drops, S->visualRng, S->paused ? 0.f : S->speed, styled);
}

#ifdef GRIM_CYD
using cyd_controls::MenuBackdrop;
constexpr int MenuFireRows = 16;
constexpr int MenuFireCols = 60;
struct MenuFire {
    uint8_t values[MenuFireRows + 1][MenuFireCols]{};
};
std::unique_ptr<MenuFire> menuFire;
RainDrop menuRain[16]{};
dsp::Noise menuNoise;
MenuBackdrop previousBackdrop = MenuBackdrop::Theme;
uint16_t backdropKey = 0;
constexpr int MenuBandRows = 8, MenuBandWidth = 320;
constexpr size_t MenuBandPixels = 2 * MenuBandRows * MenuBandWidth;
std::unique_ptr<lgfx::swap565_t[]> menuBandBuffer;
uint32_t previousRainMs = 0;
uint32_t previousFireMs = 0;
struct MenuMote {
    int16_t x, y;
    uint8_t alpha;
};
std::array<MenuMote, 18> menuMotes;
size_t menuMoteCount = 0;

void advanceMenuRain(int height, uint32_t now) {
    const float step = std::min<uint32_t>(now - previousRainMs, 100) * .024f;
    previousRainMs = now;
    for (auto &drop : menuRain) {
        if (!drop.v || drop.y >= height + (drop.len - 1) * DAEDRIC_S.h) {
            drop.y = drop.v ? -float(menuNoise.next() % 80) : float(menuNoise.next() % height);
            drop.v = 0.6f + (menuNoise.next() % 100) / 70.f;
            drop.len = 2 + menuNoise.next() % 3;
        }
        drop.y += drop.v * step;
    }
}

void drawMenuRain(Canvas &cv, int first, int rows) {
    constexpr int lanes = sizeof(menuRain) / sizeof(*menuRain);
    const int pitch = DAEDRIC_S.h;
    for (int i = 0; i < lanes; ++i) {
        const auto &drop = menuRain[i];
        const int head = int(drop.y);
        for (int j = 0; j < drop.len; ++j) {
            const int y = head - j * pitch - first;
            if (y >= rows || y + pitch <= 0) continue;
            const int index = (i * 7 + j * 3 + std::max(0, head) / pitch) % 26;
            const char g[] = {char('A' + index), 0};
            const int x = std::min(i * cv.width() / lanes, cv.width() - DAEDRIC_S.glyphs[index].w);
            glyphs(cv, DAEDRIC_S, g, x, y, C_EMBER, 2, 84 - j * 84 / drop.len);
        }
    }
}

void clearMenuForeground(Canvas &cv) {
    const auto quantise = [&](uint16_t c) {
        return cv.getColorDepth() == 8 ? cv.color8to16(cv.color16to8(c)) : c;
    };
    const uint16_t inks[] = {C_VOID, C_ASH, C_DUSK, C_CLOT, C_BLOOD, C_RUST, C_EMBER,
                             C_GLOW, C_BONE, C_DIM, C_UMBER, C_STONE, 0, 0xffff};
    constexpr uint16_t candidates[] = {0xf81f, 0x07ff, 0xffe0, 0x001f, 0xf800, 0x07e0,
                                       0xf80f, 0x061f, 0x07ef, 0xfbe0, 0x7fe0, 0x701f,
                                       0xf9e0, 0x03ff, 0x781f, 0xdbff, 0x6ffe, 0x79ef};
    for (const auto key : candidates) {
        if (std::none_of(std::begin(inks), std::end(inks), [&](uint16_t ink) { return quantise(ink) == quantise(key); })) {
            cv.fillScreen(key);
            backdropKey = cv.readPixel(0, 0);
            return;
        }
    }
}

#endif

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

#ifdef GRIM_CYD
constexpr int NativeEdge = 320;
constexpr int BandHeight = 8;
constexpr int NativeHalf = NativeEdge / 2 + 1;

struct PolarSample { uint8_t radius, angle; };

constexpr int integerRoot(int squared) {
    int lo = 0, hi = NativeEdge;
    while (lo + 1 < hi) {
        const int mid = (lo + hi) / 2;
        if (mid * mid <= squared) lo = mid;
        else hi = mid;
    }
    return lo;
}

constexpr auto buildPolarSamples() {
    std::array<std::array<PolarSample, NativeHalf>, NativeHalf> samples{};
    for (int y = 0; y < NativeHalf; ++y) {
        for (int x = 0; x < NativeHalf; ++x) {
            const int major = x > y ? x : y, minor = x > y ? y : x;
            const int fraction = major ? minor * 256 / major : 0;
            const int octant = (32 * fraction + (11 * fraction * (256 - fraction) >> 8)) >> 8;
            samples[y][x] = {static_cast<uint8_t>(integerRoot(x * x + y * y)),
                             static_cast<uint8_t>(y > x ? 64 - octant : octant)};
        }
    }
    return samples;
}

// Both LCD orientations share the same quadrant table in flash.
constexpr auto NativePolar = buildPolarSamples();

constexpr auto buildTunnelPhase() {
    std::array<uint8_t, 256> phase{};
    for (int r = 0; r < int(phase.size()); ++r) phase[r] = 1800 / (r + 1);
    return phase;
}

constexpr auto NativeTunnel = buildTunnelPhase();

constexpr int NativeTrailLength = 36;
constexpr int NativeHeads = 3;
using TrailStep = std::array<TrailPoint, NativeHeads>;
std::array<TrailStep, NativeTrailLength> nativePath;
int nativePathEnd = 0, nativePathCount = 0;
uint32_t nativePreviousMs = 0;

void moveNativeTrails(float t, int w, int h) {
    auto &points = nativePath[nativePathEnd];
    for (int i = 0; i < NativeHeads; ++i) {
        const float phase = i * 2.0943951f;
        points[i] = {w / 2 + int(w * (0.34f * sinf(t * 1.6f + phase) + 0.09f * sinf(t * 3.1f - phase))),
                     h / 2 + int(h * (0.34f * cosf(t * 1.25f - phase) + 0.09f * sinf(t * 2.6f + phase)))};
    }
    nativePathEnd = (nativePathEnd + 1) % NativeTrailLength;
    nativePathCount = std::min(nativePathCount + 1, NativeTrailLength);
}

struct NativeKaleido {
    TrailPoint rings[5][7], sparks[7];
    uint16_t ink[5];

    void animate(float t, int w, int h) {
        for (int ring = 0; ring < 5; ++ring) {
            const float r = 0.09f + ring * 0.077f + 0.025f * sinf(t * 1.3f + ring);
            const float rotation = t * (ring & 1 ? -0.4f : 0.3f) + ring * 0.4f;
            ink[ring] = ramp(90 + ring * 30 + 40 * sinf(t + ring));
            for (int i = 0; i < 7; ++i) {
                const float angle = rotation + i * 6.2831853f / 7;
                rings[ring][i] = {w / 2 + int(cosf(angle) * r * w), h / 2 + int(sinf(angle) * r * h)};
            }
        }
        const float radius = 0.23f + 0.16f * sinf(t * 2.1f);
        for (int i = 0; i < 7; ++i) {
            const float a = t * 0.9f + i * 6.2831853f / 7;
            sparks[i] = {w / 2 + int(cosf(a) * radius * w), h / 2 + int(sinf(a) * radius * h)};
        }
    }

    void draw(Canvas &band, int top) const {
        const int bottom = top + band.height();
        for (int ring = 0; ring < 5; ++ring) {
            for (int i = 0; i < 7; ++i) {
                const auto a = rings[ring][i], b = rings[ring][(i + 2 + ring % 2) % 7];
                if (std::max(a.y, b.y) >= top && std::min(a.y, b.y) < bottom) {
                    band.drawLine(a.x, a.y - top, b.x, b.y - top, ink[ring]);
                }
                if (a.y + 2 >= top && a.y - 2 < bottom) band.fillCircle(a.x, a.y - top, 2, C_GLOW);
            }
        }
        for (const auto point : sparks) {
            if (point.y + 3 >= top && point.y - 3 < bottom) band.fillCircle(point.x, point.y - top, 3, C_BONE);
        }
    }
};

void prepareNativeRain(State &s, float elapsed) {
    for (auto &drop : s.drops) {
        if (!drop.v || drop.y > hal::H + 15) {
            drop.y = drop.v ? -float(s.visualRng.next() % 80) : float(s.visualRng.next() % hal::H);
            drop.v = 0.6f + (s.visualRng.next() % 100) / 70.f;
            drop.len = 3 + s.visualRng.next() % 5;
        }
        drop.y += drop.v * elapsed * 30.f * s.speed;
    }
}

void drawNativeRain(Canvas &band, int top, int height, const State &s) {
    const int bottom = top + band.height();
    const int lanes = sizeof(s.drops) / sizeof(*s.drops);
    const int pitch = DAEDRIC_M.h + 4;
    for (int i = 0; i < lanes; ++i) {
        const auto &drop = s.drops[i];
        const int x = i * band.width() / lanes;
        const int head = int(drop.y * height / hal::H);
        for (int j = 0; j < drop.len; ++j) {
            const int y = head - j * pitch;
            if (y >= bottom || y + DAEDRIC_M.h <= top) continue;
            const char letter[] = {char('A' + (i * 7 + j * 3 + std::max(0, head) / pitch) % 26), 0};
            const int sampleY = std::clamp(y + DAEDRIC_M.h / 2 - top, 0, band.height() - 1);
            const auto behind = band.readPixel(x, sampleY);
            const auto ink = j ? blend(C_RUST, behind, 255 - j * 255 / drop.len) : C_GLOW;
            glyphs(band, DAEDRIC_M, letter, x, y - top, ink);
        }
    }
}

void drawNativeTrails(Canvas &band, int top, const State &s) {
    const int bottom = top + band.height();
    const uint16_t colours[] = {C_EMBER, C_GLOW, C_BONE};
    const int first = (nativePathEnd + NativeTrailLength - nativePathCount) % NativeTrailLength;
    for (int i = 0; i < nativePathCount; ++i) {
        const int strength = 24 + (NativeTrailLength - nativePathCount + i) * 230 / (NativeTrailLength - 1);
        const auto &points = nativePath[(first + i) % NativeTrailLength];
        const auto &next = nativePath[(first + std::min(i + 1, nativePathCount - 1)) % NativeTrailLength];
        const int radius = 2 + strength / 64;
        for (int j = 0; j < NativeHeads; ++j) {
            const auto a = points[j], b = next[j];
            if (std::max(a.y + radius, b.y) < top || std::min(a.y - radius, b.y) >= bottom) continue;
            const int sampleY = std::clamp(a.y - top, 0, band.height() - 1);
            const auto behind = s.layered ? band.readPixel(a.x, sampleY) : C_VOID;
            const auto trail = s.layered ? ramp(strength) : colours[j];
            uint16_t ink = blend(trail, behind, strength);
            if (!s.layered && s.history.rgb444) ink = to565(to444(ink));
            band.drawLine(a.x, a.y - top, b.x, b.y - top, ink);
            band.fillCircle(a.x, a.y - top, radius, ink);
        }
    }
}

template <size_t Rows, size_t Cols, typename Paint>
void interpolateHeat(const uint8_t (&heat)[Rows][Cols], int y, int width, int height, Paint paint) {
    const int fy = y * ((int(Rows) - 2) << 8) / (height - 1);
    const auto &upper = heat[fy >> 8], &lower = heat[(fy >> 8) + 1];
    const int wy = fy & 255;
    const int step = ((int(Cols) - 1) << 16) / (width - 1);
    int fx = 0;
    for (int x = 0; x < width; ++x, fx += step) {
        const int ix = std::min(fx >> 16, int(Cols) - 2), wx = (fx - (ix << 16)) >> 8;
        const int a = upper[ix] * (256 - wx) + upper[ix + 1] * wx;
        const int b = lower[ix] * (256 - wx) + lower[ix + 1] * wx;
        paint(x, (a * (256 - wy) + b * wy) >> 16);
    }
}

struct NativeField {
    Vision base;
    int width, height, tick;
    const Fire *fire;
    const int8_t *sines = sineValues();
    std::array<int8_t, NativeEdge> horizontal{};
    std::array<lgfx::swap565_t, 256> shades;

    NativeField(const State &s, int w, int h)
        : NativeField(s.base(), w, h, int(s.t * (s.base() == Gate ? 60.f : 40.f)), s.layered, s.fire.get()) {}

    NativeField(Vision effect, int w, int h, int phase, bool layered, const Fire *heat, bool dimmed = false)
        : base(effect), width(w), height(h), tick(phase), fire(heat) {
        for (int i = 0; i < 256; ++i) {
            const int intensity = !layered ? i : base == Plasma ? i * 3 / 5 : base == Ashfire ? i * 3 / 4 : i;
            const auto colour = ramp(intensity);
            shades[i] = lgfx::rgb565_t{dimmed ? blend(colour, C_VOID, 67) : colour};
        }
        if (base == Plasma) {
            for (int x = 0; x < width; ++x) horizontal[x] = wave(x + x / 2 + tick);
        }
    }

    int wave(int phase) const { return sines[static_cast<uint8_t>(phase)]; }

    void plasma(lgfx::swap565_t *out, int y) const {
        const int mid = width / 2;
        const auto &polar = NativePolar[abs(y - height / 2)];
        const int vertical = wave(2 * y + y / 2 - tick * 2 / 3);
        const int diagonal = y + tick / 2;
        const int phase = tick / 3;
        for (int x = 0; x < width; ++x) {
            const int r = polar[abs(x - mid)].radius;
            const int sum = horizontal[x] + vertical + wave(x + diagonal) + wave(r * 2 - tick);
            out[x] = shades[wave(((sum + 508) >> 2) + phase) + 128];
        }
    }

    void gate(lgfx::swap565_t *out, int y) const {
        const int mid = width / 2, dy = y - height / 2;
        const auto &polar = NativePolar[abs(dy)];
        const int ring = std::min(width, height) * 45 / 100;
        const lgfx::swap565_t rim{static_cast<uint16_t>(lgfx::getSwap16(C_BLOOD))};
        for (int x = 0; x < width; ++x) {
            const int dx = x - mid;
            const auto p = polar[abs(dx)];
            int angle = dx < 0 ? 128 - p.angle : p.angle;
            if (dy < 0) angle = -angle;
            const uint8_t u = NativeTunnel[p.radius] + tick;
            const uint8_t v = 2 * angle + tick / 4;
            const int checker = ((u ^ v) & 16) ? 150 : 40;
            const int light = checker + wave(u * 2 + v) / 3;
            const int intensity = std::max(0, light) * std::min(int(p.radius) * 5, 256) >> 8;
            out[x] = p.radius == ring ? rim : shades[intensity];
        }
    }

    void ashfire(lgfx::swap565_t *out, int y) const {
        interpolateHeat(fire->values, y, width, height, [&](int x, int heat) { out[x] = shades[heat]; });
    }

    void draw(Canvas &band, int top, int rows) const {
        if (base != Gate && base != Plasma && (base != Ashfire || !fire)) {
            drawSceneBackground(band, 0, -top, width, height, palette());
            return;
        }
        auto *out = static_cast<lgfx::swap565_t *>(band.getBuffer());
        for (int row = 0; row < rows; ++row, out += width) {
            if (base == Plasma) plasma(out, top + row);
            else if (base == Gate) gate(out, top + row);
            else ashfire(out, top + row);
        }
    }
};

constexpr int MenuFlameHeight = 64;

void drawMenuFire(lgfx::swap565_t *line, int y, int width) {
    const auto &colours = palette().intensity;
    const int strength = 105 * y / (MenuFlameHeight - 1);
    interpolateHeat(menuFire->values, y, width, MenuFlameHeight, [&](int x, int heat) {
        if (heat < 12) return;
        const auto bg = static_cast<uint16_t>(lgfx::getSwap16(line[x].get()));
        line[x] = lgfx::rgb565_t{blend(colours[heat], bg, strength)};
    });
}

void drawMenuMotes(lgfx::swap565_t *pixels, int width, int y, int rows, int top, bool portrait) {
    const auto edge = [portrait](int coordinate) {
        if (portrait) return coordinate;
        const int scaled = 4 * coordinate - 2;
        return scaled >= 0 ? (scaled + 2) / 3 : scaled / 3;
    };
    for (size_t i = 0; i < menuMoteCount; ++i) {
        const auto &mote = menuMotes[i];
        const int first = std::max(y, top + edge(mote.y));
        const int end = std::min(y + rows, top + edge(mote.y + 1));
        if (first >= end) continue;
        for (int row = first; row < end; ++row) {
            auto *line = pixels + (row - y) * width;
            for (int x = edge(mote.x); x < edge(mote.x + 1); ++x) {
                const auto bg = static_cast<uint16_t>(lgfx::getSwap16(line[x].get()));
                line[x] = lgfx::rgb565_t{blend(C_EMBER, bg, mote.alpha)};
            }
        }
    }
}
#endif

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
    if (perf_profile::quiet()) Serial.printf("Diviner layers effect=%s heap=%u largest=%u\n",
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
#ifdef GRIM_CYD
    S->nativeFullscreen = false;
#endif
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

#ifdef GRIM_CYD
void drawVisionsHeader(Canvas &cv, const State &s, Vision base) {
    constexpr const char *names[] = {"Plasma", "Gate", "Ashfire", "Rain", "Kaleido", "Trails"};
    constexpr const char *pairs[] = {"Plasma + Kaleido", "Gate + Trails", "Ashfire + Rain"};
    static_assert(sizeof(names) / sizeof(*names) == N_VIS);
    static_assert(sizeof(pairs) / sizeof(*pairs) == LayerCount);

    const uint16_t surface = blend(C_DUSK, C_VOID, 160);
    const uint16_t ink = readableInk(C_BONE, surface);
    const uint16_t detail = readableInk(C_DIM, surface);
    const uint16_t accent = readableInk(C_EMBER, surface);
    const uint16_t rule = blend(ink, surface, 58);
    cv.fillRect(0, 0, hal::W, OY, surface);
    cv.drawFastHLine(0, OY - 1, hal::W, rule);
    cv.drawFastVLine(39, 4, 11, rule);
    cv.drawFastVLine(221, 4, 11, rule);

    cv.drawLine(8, 5, 4, 9, detail);
    cv.drawLine(4, 9, 8, 13, detail);
    text("Back", 12, 5, detail);
    glyphs(cv, DAEDRIC_S, "VISIONS", 44, 1, accent);

    const char *const details[] = {!s.sound ? "Muted" : nullptr, s.paused ? "Paused" : nullptr,
                                   base == Trails ? (s.history.rgb444 ? "RGB444" : "RGB565") : nullptr};
    constexpr int LabelRight = 218;
    const bool showDetails = !s.sound || s.paused || base == Trails;
    textRight(s.layered ? pairs[base] : names[base], LabelRight, showDetails ? 1 : 5, ink);
    int detailRight = LabelRight;
    for (const char *label : details) {
        if (!label) continue;
        textRight(label, detailRight, 9, detail);
        detailRight -= static_cast<int>(strlen(label) + 1) * 6;
    }

    // This target shares the artwork's fullscreen action.
    for (int y : {4, 13}) {
        cv.drawFastHLine(226, y, 4, detail);
        cv.drawFastHLine(232, y, 4, detail);
    }
    for (int x : {226, 235}) {
        cv.drawFastVLine(x, 4, 4, detail);
        cv.drawFastVLine(x, 10, 4, detail);
    }
}
#endif

void draw(uint32_t) {
    State &s = *S;
#ifdef GRIM_CYD
    if (s.nativeFullscreen) {
        s.nativeFullscreen = false;
        prepareVision();
    }
#endif
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
#ifdef GRIM_CYD
    drawVisionsHeader(cv, s, base);
#else
    if (!styledArt) drawSceneBackground(cv, 0, 0, hal::W, OY, colours);
    header("VISIONS", "visions");
    const char *title = s.layered ? LayerTitles[base] : NAMES[base];
    if (base == Trails) title = s.history.rgb444 ? "trails RGB444" : "trails RGB565";
    textRight(title, 198, 5, C_DIM);
#endif
}
} // namespace

#ifdef GRIM_CYD
bool visionsUsesRgb444() { return !S || S->history.rgb444; }
bool visionsAreLayered() { return S && S->layered; }
bool visionsPaused() { return S && S->paused; }
bool visionsSoundEnabled() { return !S || S->sound; }
int visionsChoice() { return S ? S->base() : Plasma; }

void drawVisionsNative(lgfx::LGFXBase &display, uint32_t now) {
    State &s = *S;
    const int width = display.width(), height = display.height();
    auto *const pixels = static_cast<uint8_t *>(hal::canvas().getBuffer());
    if (!pixels || width > NativeEdge || height > NativeEdge) {
        display.fillScreen(C_VOID);
        return;
    }

    const bool fresh = !s.nativeFullscreen;
    if (fresh) {
        s.nativeFullscreen = true;
        s.history.release();
        s.tunnel.reset();
        nativePathEnd = nativePathCount = 0;
        nativePreviousMs = now;
    }
    const float elapsed = s.paused ? 0.f : std::min<uint32_t>(now - nativePreviousMs, 100) * 0.001f;
    nativePreviousMs = now;
    s.t += elapsed * s.speed;

    const Vision base = s.base();
    const bool hasTrails = base == Trails || (s.layered && base == Gate);
    const bool hasRain = base == Rain || (s.layered && base == Ashfire);
    const bool hasKaleido = base == Kaleido || (s.layered && base == Plasma);
    if (hasTrails && (fresh || !s.paused)) moveNativeTrails(s.t, width, height);
    if (hasRain) prepareNativeRain(s, elapsed);
    if (base == Ashfire && s.fire && (!s.paused || !s.fire->seeded)) {
        stepFire(s.fire->values, s.visualRng);
        s.fire->seeded = true;
    }
    NativeKaleido kaleido;
    if (hasKaleido) kaleido.animate(s.t, width, height);
    const NativeField field(s, width, height);

    // Borrow two strips of the ordinary canvas. Even its RGB332 fallback
    // has 32,400 bytes, enough for both native RGB565 strips (10,240 bytes).
    static_assert(2 * NativeEdge * BandHeight * sizeof(lgfx::swap565_t) <= hal::W * hal::H);
    Canvas bands[2];
    const int bandBytes = width * BandHeight * sizeof(lgfx::swap565_t);
    bands[0].setBuffer(pixels, width, BandHeight, 16);
    bands[1].setBuffer(pixels + bandBytes, width, BandHeight, 16);
    display.setAddrWindow(0, 0, width, height);
    for (int y = 0; y < height; y += BandHeight) {
        Canvas &band = bands[(y / BandHeight) % 2];
        const int rows = std::min(BandHeight, height - y);
        field.draw(band, y, rows);
        if (hasRain) drawNativeRain(band, y, height, s);
        if (hasKaleido) kaleido.draw(band, y);
        if (hasTrails) drawNativeTrails(band, y, s);
        display.waitDMA();
        display.writePixelsDMA(static_cast<const lgfx::swap565_t *>(band.getBuffer()), width * rows);
    }
    display.waitDMA();
}

void releaseVisionsMenuBackdrop() {
    menuFire.reset();
    menuBandBuffer.reset();
    previousBackdrop = MenuBackdrop::Theme;
}

void drawVisionsMenuBackdrop(uint32_t now) {
    const auto backdrop = cyd_controls::menuBackdrop();
    menuMoteCount = 0;
    if (backdrop == MenuBackdrop::Theme) {
        if (previousBackdrop != backdrop) releaseVisionsMenuBackdrop();
        theme::drawBackground(hal::canvas());
        return;
    }
    if (previousBackdrop != backdrop) {
        if (!menuBandBuffer) menuBandBuffer.reset(new (std::nothrow) lgfx::swap565_t[MenuBandPixels]);
        menuFire.reset();
        previousBackdrop = backdrop;
        if (backdrop == MenuBackdrop::Ashfire) {
            menuFire.reset(new (std::nothrow) MenuFire);
            if (menuFire) for (int step = 0; step < MenuFireRows; ++step) stepFire(menuFire->values, menuNoise);
            previousFireMs = now;
        }
        if (backdrop == MenuBackdrop::Rain) {
            for (auto &drop : menuRain) drop = {};
            previousRainMs = now;
        }
    }

    if (backdrop == MenuBackdrop::Rain) {
        advanceMenuRain(cyd_controls::displayHeight(), now);
    } else if (menuFire && now - previousFireMs >= 46) {
        previousFireMs = now - (now - previousFireMs) % 46;
        stepFire(menuFire->values, menuNoise);
    }
    clearMenuForeground(hal::canvas());
}

void queueVisionsMenuMote(int x, int y, uint8_t alpha) {
    if (alpha && x >= 0 && x < hal::W && menuMoteCount < menuMotes.size()) {
        menuMotes[menuMoteCount++] = {static_cast<int16_t>(x), static_cast<int16_t>(y), alpha};
    }
}

int menuBackdropKey() { return previousBackdrop == MenuBackdrop::Theme ? -1 : backdropKey; }

void renderNativeMenuBackdrop(lgfx::LGFXBase &display, uint32_t now) {
    Canvas &foreground = hal::canvas();
    const bool portrait = cyd_controls::isPortrait();
    const int width = display.width(), height = display.height();
    const int bottom = cyd_controls::viewportBottom();
    const int top = cyd_controls::viewportTop(), sceneBottom = top + cyd_controls::viewportHeight();
    const int rowsPerBand = menuBandBuffer ? MenuBandRows : 1;
    lgfx::swap565_t local[MenuBandWidth];
    lgfx::swap565_t source[hal::W];
    const auto key = lgfx::getSwap16(backdropKey);
    std::array<uint8_t, MenuBandWidth> xs;
    for (int x = 0; x < width; ++x) xs[x] = portrait ? x : (3 * x + 2) / 4;

    std::optional<NativeField> plasma;
    if (previousBackdrop == MenuBackdrop::Plasma) plasma.emplace(Plasma, width, height, (now / 40) % 1536, false, nullptr, true);

    auto *const scratch = menuBandBuffer ? menuBandBuffer.get() : local;
    Canvas bands[2];
    bands[0].setBuffer(scratch, width, rowsPerBand, 16);
    if (menuBandBuffer) bands[1].setBuffer(scratch + width * rowsPerBand, width, rowsPerBand, 16);
    display.setAddrWindow(0, 0, width, bottom);
    int previousRow = -1;
    for (int y = 0; y < bottom; y += rowsPerBand) {
        Canvas &band = bands[menuBandBuffer ? y / rowsPerBand % 2 : 0];
        const int rows = std::min(rowsPerBand, bottom - y);
        auto *const dest = static_cast<lgfx::swap565_t *>(band.getBuffer());
        if (plasma) {
            for (int row = 0; row < rows; ++row) plasma->plasma(dest + row * width, y + row);
        } else {
            theme::drawSceneBackground(band, 0, -y, width, height, palette());
            if (previousBackdrop == MenuBackdrop::Rain) {
                drawMenuRain(band, y, rows);
            } else if (menuFire) {
                const int flameTop = bottom - MenuFlameHeight;
                for (int row = std::max(0, flameTop - y); row < rows; ++row) {
                    drawMenuFire(dest + row * width, y + row - flameTop, width);
                }
            }
        }
        drawMenuMotes(dest, width, y, rows, top, portrait);
        for (int row = std::max(0, top - y); row < std::min(rows, sceneBottom - y); ++row) {
            const int sy = portrait ? y + row - top : (3 * (y + row - top) + 2) / 4;
            if (sy != previousRow) foreground.readRect(0, sy, hal::W, 1, source);
            previousRow = sy;
            auto *line = dest + row * width;
            for (int x = 0; x < width; ++x) {
                if (source[xs[x]].get() != key) line[x] = source[xs[x]];
            }
        }
        if (menuBandBuffer) {
            display.waitDMA();
            display.writePixelsDMA(dest, width * rows);
        } else {
            display.writePixels(dest, width * rows);
        }
    }
    display.waitDMA();
}
#endif

const Rite RITE_VISIONS = {"VISIONS", "visions", "plasma, fire, rain, feedback trails", enter, leave, onKey, draw};

#ifdef GRIM_PERF
bool perfConfigureVision(unsigned effect, bool layered, unsigned format, uint32_t seed) {
    if (!S || effect >= N_VIS) return false;
    // Rebuild visual history outside measurement; preserve the callback state.
    hal::setAudio(nullptr);
    S->history.release(); S->tunnel.reset(); S->fire.reset();
    S->vis = static_cast<Vision>(effect); S->layered = layered;
    S->history.rgb444 = format == 0; S->paused = false; S->sound = true; S->t = 0; S->speed = 1;
    S->visualRng.s = seed; S->rng.s = seed ^ 0xA5A5A5A5;
    memset(S->drops, 0, sizeof(S->drops));
    S->tone.phase = S->tone2.phase = S->lfo.phase = 0;
    S->windF = dsp::SVF{};
    prepareVision(); retone();
    // Describe actual allocations after prepareVision(), rather than assuming
    // that the requested trail format or auxiliary allocation succeeded.
    const Vision base = S->base();
    unsigned allocatedBands = 0;
    size_t historyBytes = 0;
    for (const auto &band : S->history.bands) if (band) {
        ++allocatedBands;
        historyBytes += S->history.stride() * TrailHistory::BandRows;
    }
    const bool historyReady = S->history.ready();
    const bool historyRequested = base == Trails || (S->layered && base == Gate);
    const char *historyFormat = !historyReady ? "NONE" : S->history.indexed ? "LUT8" : S->history.rgb444 ? "RGB444" : "RGB565";
    char settings[1536];
    snprintf(settings, sizeof(settings),
        "\"requested_effect_index\":%u,\"requested_effect\":\"%s\",\"requested_layered\":%s,\"requested_trail_format\":\"%s\","
        "\"actual_base_effect_index\":%u,\"actual_base_effect\":\"%s\",\"actual_layered\":%s,\"audio_enabled\":%s,\"vision_seed\":%u,"
        "\"history_requested\":%s,\"history_ready\":%s,\"history_format\":\"%s\",\"history_payload_bytes\":%u,"
        "\"history_width_px\":%u,\"history_height_px\":%u,\"history_stride_bytes\":%u,\"history_band_rows\":%u,\"history_allocated_bands\":%u,"
        "\"tunnel_required\":%s,\"tunnel_allocated\":%s,\"tunnel_payload_bytes\":%u,\"fire_required\":%s,\"fire_allocated\":%s,\"fire_payload_bytes\":%u,"
        "\"native_fullscreen_at_receipt\":%s,\"receipt_scope\":\"after_configuration_before_next_frame\","
        "\"native_fullscreen_history_method\":\"bounded_geometric_paths; first fullscreen frame releases pixel history and tunnel\"",
        effect, NAMES[effect], layered ? "true" : "false", format == 0 ? "RGB444" : "RGB565",
        unsigned(base), NAMES[base], S->layered ? "true" : "false", S->sound ? "true" : "false", unsigned(seed),
        historyRequested ? "true" : "false", historyReady ? "true" : "false", historyFormat, unsigned(historyBytes),
        historyReady ? unsigned(hal::W) : 0u, allocatedBands * TrailHistory::BandRows,
        allocatedBands ? unsigned(S->history.stride()) : 0u, unsigned(TrailHistory::BandRows), allocatedBands,
        base == Gate ? "true" : "false", S->tunnel ? "true" : "false", S->tunnel ? unsigned(sizeof(Tunnel)) : 0u,
        base == Ashfire ? "true" : "false", S->fire ? "true" : "false", S->fire ? unsigned(sizeof(Fire)) : 0u,
        S->nativeFullscreen ? "true" : "false");
    perf_profile::sceneSettings("visions", settings);
    hal::setAudio(render);
    return true;
}
#endif
