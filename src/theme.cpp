#include "theme.h"
#ifdef GRIM_CYD
#include "cyd_controls.h"
#endif
#include <algorithm>
#include <cstdlib>
#include <stdio.h>
#include <string.h>

namespace theme {
namespace {

constexpr uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a) {
    if (a == 255) return fg;
    if (a == 0) return bg;
    uint32_t r = (((fg >> 11) & 31) * a + ((bg >> 11) & 31) * (255 - a)) / 255;
    uint32_t g = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (255 - a)) / 255;
    uint32_t b = ((fg & 31) * a + (bg & 31) * (255 - a)) / 255;
    return (r << 11) | (g << 5) | b;
}

constexpr void writeRamp(Palette &colors) {
    const uint16_t stops[] = {colors.background, colors.dusk, colors.clot, colors.blood,
                              colors.rust, colors.ember, colors.glow, colors.bone};
    for (int i = 0; i < 256; ++i) {
        const int segment = i * 7 / 256;
        const int weight = (i * 7) % 256;
        colors.intensity[i] = blend565(stops[segment + 1], stops[segment], weight);
    }
}

constexpr Palette withRamp(Palette colors) {
    writeRamp(colors);
    colors.gradientTop = colors.background;
    colors.gradientBottom = colors.ember;
    return colors;
}

constexpr uint16_t rgb565(uint32_t rgb) {
    return ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f);
}

constexpr auto Ember = withRamp({
    0x0020, 0x0840, 0x1820, 0x3861, 0x78e2, 0xaa05,
    0xc307, 0xe4cd, 0xb531, 0x6a47, 0x3923, 0x39a6, {},
});

constexpr auto Neon = withRamp({
    rgb565(0x030804), // background
    rgb565(0x0a130a), // inactive panels
    rgb565(0x101d10), // selected panels
    rgb565(0x27472a), // rules and outlines
    rgb565(0x458638), // low accents
    rgb565(0x6dbd3f), // mid accents
    rgb565(0x83f044), // active accents and glyphs
    rgb565(0xa5ff61), // highlights
    rgb565(0xd6efc9), // primary text
    rgb565(0x8ca983), // secondary text
    rgb565(0x2f512f), // studs and guides
    rgb565(0x47634a), // neutral geometry
    {},
});

Palette Custom;
Colourway savedColourway;
bool customAvailable = false;

uint32_t rgb888(uint16_t c) {
    const uint32_t r = ((c >> 11) & 31) * 255 / 31;
    const uint32_t g = ((c >> 5) & 63) * 255 / 63;
    const uint32_t b = (c & 31) * 255 / 31;
    return (r << 16) | (g << 8) | b;
}

uint32_t hues(uint16_t hue, int saturation, int value) {
    hue %= 360;
    const int chroma = value * saturation / 255;
    const int slope = chroma * (hue % 60) / 60;
    const int low = value - chroma;
    const int up = low + slope, down = value - slope;
    switch (hue / 60) {
        case 0: return (value << 16) | (up << 8) | low;
        case 1: return (down << 16) | (value << 8) | low;
        case 2: return (low << 16) | (value << 8) | up;
        case 3: return (low << 16) | (down << 8) | value;
        case 4: return (up << 16) | (low << 8) | value;
        default: return (value << 16) | (low << 8) | down;
    }
}

constexpr std::array<int, 3> adjust(uint32_t rgb, const Colourway &colourway) {
    std::array<int, 3> channels{int((rgb >> 16) & 255), int((rgb >> 8) & 255), int(rgb & 255)};
    const int gray = (77 * channels[0] + 150 * channels[1] + 29 * channels[2]) / 256;
    for (int &c : channels) {
        c = std::clamp(gray + (c - gray) * colourway.saturation / 100, 0, 255);
        if (colourway.brightness < 0) c = c * (100 + colourway.brightness) / 100;
        else c += (255 - c) * colourway.brightness / 100;
    }
    return channels;
}

constexpr uint16_t anchor(uint32_t rgb, const Colourway &colourway, const std::array<int, 3> &background) {
    const auto channels = adjust(rgb, colourway);
    uint32_t color = 0;
    for (int i = 0; i < 3; ++i) {
        // Contract or expand distances from the background without moving it.
        const int d = channels[i] - background[i];
        const int c = std::clamp(channels[i] + d * colourway.contrast / 160, 0, 255);
        color = (color << 8) | static_cast<uint32_t>(c);
    }
    return rgb565(color);
}

constexpr void fillPalette(Palette &out, const Colourway &colourway) {
    const auto background = adjust(colourway.rgb[0], colourway);
    const uint16_t bg = anchor(colourway.rgb[0], colourway, background);
    const uint16_t panel = anchor(colourway.rgb[1], colourway, background);
    const uint16_t text = anchor(colourway.rgb[2], colourway, background);
    const uint16_t accent = anchor(colourway.rgb[3], colourway, background);

    out.background = bg;
    out.ash = blend565(panel, bg, 153);
    out.dusk = panel;
    out.clot = blend565(accent, panel, 48);
    out.blood = blend565(accent, bg, 138);
    out.rust = blend565(accent, bg, 194);
    out.ember = accent;
    out.glow = blend565(text, accent, 77);
    out.bone = text;
    out.dim = blend565(text, bg, 174);
    out.umber = blend565(accent, panel, 58);
    out.stone = blend565(text, panel, 70);
    out.backgroundStyle = colourway.backgroundStyle;
    out.gradientMode = colourway.gradientMode;
    const uint16_t anchors[] = {bg, panel, text, accent};
    out.gradientTop = colourway.gradientStart <= ColourRole::Accent
                      ? anchors[static_cast<uint8_t>(colourway.gradientStart)] : bg;
    out.gradientBottom = colourway.gradientEnd <= ColourRole::Accent
                         ? anchors[static_cast<uint8_t>(colourway.gradientEnd)] : accent;
    writeRamp(out);
}

constexpr Colourway styled(std::array<uint32_t, 4> rgb, BackgroundStyle style,
                           GradientMode mode = GradientMode::Blend) {
    Colourway result{rgb};
    result.backgroundStyle = style;
    result.gradientMode = mode;
    return result;
}

struct Preset {
    const char *name;
    Colourway colours;
};

// Stored in ColourScheme order starting at Glacier. Ember and Neon keep their
// original semantic colours above; these presets use the Palette editor builder.
constexpr Preset ExtraPresets[] = {
    {"Glacier", styled({0x050e22, 0x142c47, 0xd9efff, 0x78c5ff}, BackgroundStyle::Gradient)},
    {"Violet", styled({0x160826, 0x3d328a, 0xf2ddff, 0xe17cf0}, BackgroundStyle::Gradient, GradientMode::Multitone)},
    {"Sunset", styled({0x190f24, 0x743553, 0xffeadb, 0xf39562}, BackgroundStyle::Gradient, GradientMode::Bands)},
    {"Terminal", styled({0x03120c, 0x143425, 0xd5fce6, 0x50e395}, BackgroundStyle::Grid)},
    {"Phosphor", styled({0x081315, 0x122f35, 0xbffaf9, 0x4de6e8}, BackgroundStyle::Scanlines)},
    {"Dust", styled({0x1d141b, 0x392630, 0xf3dcdf, 0xd6a1a1}, BackgroundStyle::Dither)},
    {"Hotline", styled({0x160e2c, 0x3d1a48, 0xffdff1, 0xfa65be}, BackgroundStyle::Diagonal)},
    {"Blueprint", styled({0x0d1750, 0x1a3375, 0xe0efff, 0x82aaff}, BackgroundStyle::Checker)},
    {"Pearl", styled({0xece4e4, 0xd6c9d2, 0x281c35, 0x774c90}, BackgroundStyle::Diamonds)},
    {"Abyss", styled({0x081524, 0x173342, 0xc5f4e9, 0x42d5b0}, BackgroundStyle::Waves)},
};
constexpr std::size_t ExtraCount = sizeof ExtraPresets / sizeof ExtraPresets[0];
constexpr auto FirstExtra = static_cast<uint8_t>(ColourScheme::Glacier);
static_assert(ExtraCount + 2 == PresetCount);
static_assert(FirstExtra + ExtraCount - 1 == static_cast<uint8_t>(ColourScheme::Abyss));

constexpr auto makeExtraPalettes() {
    std::array<Palette, ExtraCount> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        fillPalette(result[i], ExtraPresets[i].colours);
    }
    return result;
}

constexpr auto ExtraPalettes = makeExtraPalettes();

#ifdef GRIM_CYD
ColourScheme activeScheme = ColourScheme::Neon;
#else
ColourScheme activeScheme = ColourScheme::Ember;
#endif

} // namespace

#ifdef GRIM_CYD
const Palette *detail::activePalette = &Neon;
#else
const Palette *detail::activePalette = &Ember;
#endif

ColourScheme colourScheme() {
    return activeScheme;
}

ColourScheme presetScheme(std::size_t index) {
    if (index >= PresetCount) return ColourScheme::Neon;
    return static_cast<ColourScheme>(index < 2 ? index : index + 1); // Skip Custom.
}

bool validColourScheme(ColourScheme scheme) {
    return scheme <= ColourScheme::Abyss;
}

void setColourScheme(ColourScheme scheme) {
    if (!validColourScheme(scheme) || (scheme == ColourScheme::Custom && !customAvailable)) return;
    detail::activePalette = &presetPalette(scheme);
    activeScheme = scheme;
}

const char *colourSchemeName(ColourScheme scheme) {
    if (scheme >= ColourScheme::Glacier && validColourScheme(scheme)) {
        return ExtraPresets[static_cast<uint8_t>(scheme) - FirstExtra].name;
    }
    switch (scheme) {
        case ColourScheme::Neon: return "Neon";
        case ColourScheme::Custom: return "Custom";
        default: return "Ember";
    }
}

const Palette &presetPalette(ColourScheme scheme) {
    if (scheme >= ColourScheme::Glacier && validColourScheme(scheme)) {
        return ExtraPalettes[static_cast<uint8_t>(scheme) - FirstExtra];
    }
    if (scheme == ColourScheme::Custom && customAvailable) return Custom;
    return scheme == ColourScheme::Ember ? Ember : Neon;
}

Colourway presetColourway(ColourScheme scheme) {
    if (scheme >= ColourScheme::Glacier && validColourScheme(scheme)) {
        return ExtraPresets[static_cast<uint8_t>(scheme) - FirstExtra].colours;
    }
    if (scheme == ColourScheme::Custom && customAvailable) return savedColourway;
    const Palette &seed = presetPalette(scheme);
    return {{rgb888(seed.background), rgb888(seed.dusk), rgb888(seed.bone), rgb888(seed.ember)}};
}

Colourway generateColourway(uint16_t hue, GradientMode mode) {
    hue %= 360;
    if (mode == GradientMode::Blend) {
        return {{hues(hue, 135, 14), hues(hue, 140, 42),
                 hues(hue + 24, 40, 244), hues(hue, 193, 250)}};
    }

    Colourway result;
    if (mode == GradientMode::Multitone) {
        result.rgb = {hues(hue, 158, 22), hues(hue + 68, 195, 96),
                      hues(hue + 45, 31, 249), hues(hue + 148, 165, 184)};
    } else {
        result.rgb = {hues(hue + 208, 206, 46), hues(hue + 18, 220, 128),
                      hues(hue + 30, 24, 252), hues(hue + 124, 213, 218)};
    }
    result.backgroundStyle = BackgroundStyle::Gradient;
    result.gradientMode = mode;
    return result;
}

bool validColourway(const Colourway &colourway) {
    for (const auto rgb : colourway.rgb) {
        if (rgb > 0xffffff) return false;
    }
    return colourway.saturation <= 200 &&
           colourway.brightness >= -100 && colourway.brightness <= 100 &&
           colourway.contrast >= -100 && colourway.contrast <= 100 &&
           colourway.backgroundStyle <= BackgroundStyle::Waves &&
           colourway.gradientStart <= ColourRole::Accent &&
           colourway.gradientEnd <= ColourRole::Accent &&
           colourway.gradientMode <= GradientMode::Bands;
}

void buildPalette(Palette &out, const Colourway &colourway) {
    fillPalette(out, colourway);
}

bool hasCustomColourway() { return customAvailable; }
const Colourway &customColourway() { return savedColourway; }

void setCustomColourway(const Colourway &colourway) {
    if (!validColourway(colourway)) return;
    savedColourway = colourway;
    buildPalette(Custom, colourway);
    customAvailable = true;
}

uint16_t blend(uint16_t fg, uint16_t bg, uint8_t a) {
    return blend565(fg, bg, a);
}

int luma(uint16_t colour) {
    const int r = ((colour >> 11) & 31) * 255 / 31;
    const int g = ((colour >> 5) & 63) * 255 / 63;
    const int b = (colour & 31) * 255 / 31;
    return (299 * r + 587 * g + 114 * b) / 1000;
}

uint16_t readableInk(uint16_t preferred, uint16_t background) {
    const int tone = luma(background);
    const int delta = luma(preferred) - tone;
    if (delta >= 96 || delta <= -96) return preferred;
    return tone < 128 ? 0xffff : 0;
}

template <typename Surface>
static void drawBlend(Surface &canvas, int x, int y, int w, int h,
                      uint16_t start, uint16_t end, int maxBands) {
    const int bands = std::min(h, maxBands);
    for (int band = 0; band < bands; ++band) {
        const int top = band * h / bands;
        const int bottom = (band + 1) * h / bands;
        const uint8_t weight = bands > 1 ? band * 255 / (bands - 1) : 0;
        canvas.fillRect(x, y + top, w, bottom - top, blend565(end, start, weight));
    }
}

static int luminance(uint16_t colour) {
    const int red = ((colour >> 11) & 31) * 255 / 31;
    const int green = ((colour >> 5) & 63) * 255 / 63;
    const int blue = (colour & 31) * 255 / 31;
    return (77 * red + 150 * green + 29 * blue) / 256;
}

struct GradientStops {
    uint16_t start, middle, end;
};

static GradientStops gradientStops(const Palette &colours, bool behindArtwork, bool useMiddle) {
    GradientStops stops{colours.gradientTop, colours.dusk, colours.gradientBottom};
    if (behindArtwork) {
        const int background = luminance(colours.background);
        const bool lightBackdrop = background > 128 && luminance(colours.bone) + 45 < background;
        const uint16_t base = lightBackdrop ? 0xffff : 0;
        const int startDistance = lightBackdrop ? 255 - luminance(stops.start) : luminance(stops.start);
        const int endDistance = lightBackdrop ? 255 - luminance(stops.end) : luminance(stops.end);
        int span = std::max(startDistance, endDistance);
        if (useMiddle) {
            const int middle = luminance(stops.middle);
            span = std::max(span, lightBackdrop ? 255 - middle : middle);
        }
        constexpr int BackdropRange = 58;
        if (span > BackdropRange) {
            // Apply the same gain to each stop so the gradient keeps its hue
            // and relative levels while leaving room for the art above it.
            const auto gain = static_cast<uint8_t>(BackdropRange * 255 / span);
            stops.start = blend565(stops.start, base, gain);
            stops.middle = blend565(stops.middle, base, gain);
            stops.end = blend565(stops.end, base, gain);
        }
    }
    return stops;
}

template <typename Surface>
static void drawGradient(Surface &canvas, int x, int y, int w, int h, const Palette &colours,
                         bool behindArtwork) {
    const bool useMiddle = h >= 3 && colours.gradientMode != GradientMode::Blend;
    const auto [start, middleColour, end] = gradientStops(colours, behindArtwork, useMiddle);
    if (!useMiddle) {
        drawBlend(canvas, x, y, w, h, start, end, 48);
    } else if (colours.gradientMode == GradientMode::Multitone) {
        const int middle = h / 2;
        drawBlend(canvas, x, y, w, middle, start, middleColour, 24);
        drawBlend(canvas, x, y + middle, w, h - middle, middleColour, end, 24);
    } else {
        const int first = h / 3, second = 2 * h / 3;
        canvas.fillRect(x, y, w, first, start);
        canvas.fillRect(x, y + first, w, second - first, middleColour);
        canvas.fillRect(x, y + second, w, h - second, end);
    }
}

template <typename Surface>
static void drawBackdrop(Surface &canvas, int x, int y, int w, int h, const Palette &colours, bool behindArtwork) {
    if (w <= 0 || h <= 0) return;
    const uint16_t bg = colours.background;
    if (colours.backgroundStyle == BackgroundStyle::Gradient) {
        drawGradient(canvas, x, y, w, h, colours, behindArtwork);
        return;
    }

    canvas.fillRect(x, y, w, h, bg);
    switch (colours.backgroundStyle) {
        case BackgroundStyle::Grid: {
            const uint16_t line = blend565(colours.ember, bg, 39);
            for (int col = 8; col < w; col += 12) canvas.drawFastVLine(x + col, y, h, line);
            for (int row = 8; row < h; row += 12) canvas.drawFastHLine(x, y + row, w, line);
            break;
        }
        case BackgroundStyle::Scanlines: {
            const uint16_t line = blend565(colours.ember, bg, 40);
            for (int row = 2; row < h; row += 3) canvas.drawFastHLine(x, y + row, w, line);
            break;
        }
        case BackgroundStyle::Dither: {
            constexpr uint8_t bayer[4][4] = {
                {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5},
            };
            const uint16_t dot = blend565(colours.ember, bg, 63);
            for (int row = 1; row < h; row += 4) {
                const auto &thresholds = bayer[(row / 4) % 4];
                const int density = 2 + row * 11 / h;
                for (int col = 1; col < w; col += 4) {
                    if (thresholds[(col / 4) % 4] < density) canvas.drawPixel(x + col, y + row, dot);
                }
            }
            break;
        }
        case BackgroundStyle::Diagonal: {
            const uint16_t line = blend565(colours.ember, bg, 42);
            for (int offset = -(h - 1) / 14 * 14; offset < w; offset += 14) {
                const int first = std::max(0, -offset);
                const int last = std::min(h - 1, w - 1 - offset);
                canvas.drawLine(x + offset + first, y + first, x + offset + last, y + last, line);
            }
            break;
        }
        case BackgroundStyle::Checker: {
            constexpr int tile = 12;
            const uint16_t shade = blend565(colours.ember, bg, 29);
            for (int row = 0; row < h; row += tile) {
                for (int col = row / tile % 2 * tile; col < w; col += 2 * tile) {
                    canvas.fillRect(x + col, y + row, std::min(tile, w - col),
                                    std::min(tile, h - row), shade);
                }
            }
            break;
        }
        case BackgroundStyle::Diamonds: {
            constexpr int radius = 5, spacing = 16;
            const uint16_t line = blend565(colours.ember, bg, 47);
            for (int row = radius; row + radius < h; row += spacing) {
                for (int col = radius + row / spacing % 2 * (spacing / 2); col + radius < w; col += spacing) {
                    const int cx = x + col, cy = y + row;
                    canvas.drawLine(cx, cy - radius, cx + radius, cy, line);
                    canvas.drawLine(cx + radius, cy, cx, cy + radius, line);
                    canvas.drawLine(cx, cy + radius, cx - radius, cy, line);
                    canvas.drawLine(cx - radius, cy, cx, cy - radius, line);
                }
            }
            break;
        }
        case BackgroundStyle::Waves: {
            constexpr int step = 5;
            constexpr int wave[] = {0, 2, 4, 4, 4, 2, 0, -2, -4, -4, -4, -2};
            constexpr int cycle = sizeof(wave) / sizeof(wave[0]);
            const uint16_t line = blend565(colours.ember, bg, 44);
            for (int row = 4; row + 4 < h; row += 13) {
                for (int col = 0; col + 1 < w; col += step) {
                    const int phase = col / step % cycle;
                    const int end = std::min(col + step, w - 1);
                    const int from = wave[phase], to = wave[(phase + 1) % cycle];
                    canvas.drawLine(x + col, y + row + from, x + end,
                                    y + row + from + (to - from) * (end - col) / step, line);
                }
            }
            break;
        }
        default: break;
    }
}

void drawBackground(Canvas &canvas, int x, int y, int w, int h, const Palette &colours) {
    drawBackdrop(canvas, x, y, w, h, colours, false);
}

void drawSceneBackground(Canvas &canvas, int x, int y, int w, int h, const Palette &colours) {
    drawBackdrop(canvas, x, y, w, h, colours, true);
}

void drawBackground(Canvas &canvas) {
#ifdef GRIM_CYD
    const int margin = cyd_controls::viewportTop() > 0 ? FullBackdropMargin : 0;
#else
    constexpr int margin = 0;
#endif
    drawSceneBackground(canvas, 0, -margin, hal::W, hal::H + 2 * margin, palette());
}

#ifdef GRIM_CYD
namespace {
// Match the nearest source pixel used by the 4/3 sprite zoom. A display
// pixel selects floor((destination - origin) * 3/4 + 1/2).
constexpr int scaledEdge(int coordinate) {
    const int n = 4 * coordinate - 2;
    return n > 0 ? (n + 2) / 3 : n / 3;
}

struct ScaledBackdrop {
    lgfx::LGFXBase &display;
    int originY;
    int clipLeft, clipTop, clipRight, clipBottom;

    bool intersectsClip(int left, int top, int right, int bottom) const {
        return left < clipRight && right > clipLeft && top < clipBottom && bottom > clipTop;
    }

    void fillRect(int x, int y, int w, int h, uint16_t colour) {
        const int left = scaledEdge(x), top = originY + scaledEdge(y);
        const int right = scaledEdge(x + w), bottom = originY + scaledEdge(y + h);
        if (intersectsClip(left, top, right, bottom)) display.fillRect(left, top, right - left, bottom - top, colour);
    }

    void drawPixel(int x, int y, uint16_t colour) { fillRect(x, y, 1, 1, colour); }
    void drawFastVLine(int x, int y, int h, uint16_t colour) { fillRect(x, y, 1, h, colour); }
    void drawFastHLine(int x, int y, int w, uint16_t colour) { fillRect(x, y, w, 1, colour); }

    void drawLine(int x0, int y0, int x1, int y1, uint16_t colour) {
        if (!intersectsClip(scaledEdge(std::min(x0, x1)), originY + scaledEdge(std::min(y0, y1)),
                            scaledEdge(std::max(x0, x1) + 1), originY + scaledEdge(std::max(y0, y1) + 1))) return;
        // Rasterize at logical resolution, so diagonal patterns have the same
        // shape and line widths as the sprite on the other side of the clip.
        const bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
        if (steep) {
            std::swap(x0, y0);
            std::swap(x1, y1);
        }
        if (x0 > x1) {
            std::swap(x0, x1);
            std::swap(y0, y1);
        }
        const int dx = x1 - x0, dy = std::abs(y1 - y0), step = y0 < y1 ? 1 : -1;
        int error = dx / 2;
        for (int x = x0; x <= x1; ++x) {
            if (steep) drawPixel(y0, x, colour);
            else drawPixel(x, y0, colour);
            error -= dy;
            if (error < 0) {
                error += dx;
                y0 += step;
            }
        }
    }
};
} // namespace

void drawBackdropPatch(lgfx::LGFXBase &display, int originY, int x, int y, int w, int h, const Palette &colours) {
    if (w <= 0 || h <= 0) return;
    ScaledBackdrop surface{display, originY, x, y, x + w, y + h};
    display.setClipRect(x, y, w, h);
    drawBackdrop(surface, 0, -FullBackdropMargin, hal::W, hal::H + 2 * FullBackdropMargin, colours, true);
    display.clearClipRect();
}

void drawBackdropMargins(lgfx::LGFXBase &display, int originY, const Palette &colours) {
    const int end = originY + scaledEdge(hal::H);
    drawBackdropPatch(display, originY, 0, 0, display.width(), originY, colours);
    drawBackdropPatch(display, originY, 0, end, display.width(), display.height() - end, colours);
}
#endif

static int glyphIndex(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    return (c >= 'A' && c <= 'Z') ? c - 'A' : -1;
}

// Glyph pixels are blended against whatever is already in the canvas, so
// glyphs can sit on plasma, frames, anything.
static int drawGlyph(const DaedricFont &f, int idx, int x, int y, uint16_t fg) {
    Canvas &cv = hal::canvas();
    const DaedricGlyph &g = f.glyphs[idx];
    const uint8_t *d = f.data + g.offset;
    const int stride = (g.w + 3) / 4;
    static const uint8_t alpha[4] = {0, 90, 175, 255};
    for (int row = 0; row < f.h; ++row) {
        const uint8_t *line = d + row * stride;
        int py = y + row;
        if (py < 0 || py >= hal::H) continue;
        for (int col = 0; col < g.w; ++col) {
            uint8_t a = (line[col >> 2] >> (6 - 2 * (col & 3))) & 3;
            if (!a) continue;
            int px = x + col;
            if (px < 0 || px >= hal::W) continue;
            if (a == 3) cv.drawPixel(px, py, fg);
            else {
                uint16_t bg = cv.readPixel(px, py);
                cv.drawPixel(px, py, blend(fg, bg, alpha[a]));
            }
        }
    }
    return g.w;
}

int glyphWidth(const DaedricFont &f, const char *text, int spacing) {
    int w = 0;
    for (const char *p = text; *p; ++p) {
        int idx = glyphIndex(*p);
        w += (idx < 0 ? f.space : f.glyphs[idx].w) + spacing;
    }
    return w > 0 ? w - spacing : 0;
}

int glyphs(const DaedricFont &f, const char *text, int x, int y, uint16_t fg, int spacing) {
    int cx = x;
    for (const char *p = text; *p; ++p) {
        int idx = glyphIndex(*p);
        cx += (idx < 0 ? f.space : drawGlyph(f, idx, cx, y, fg)) + spacing;
    }
    return cx - x;
}

void glyphsCentre(const DaedricFont &f, const char *text, int cx, int y, uint16_t fg) {
    glyphs(f, text, cx - glyphWidth(f, text) / 2, y, fg);
}

float smoothstep(float e0, float e1, float x) {
    if (e1 <= e0) return x < e0 ? 0.f : 1.f;
    float t = (x - e0) / (e1 - e0);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return t * t * (3 - 2 * t);
}

// Runes fade out over the first ~60% of the reveal; the latin word fades in
// over the last ~55%, so they briefly cross-dissolve in place.
void wordDissolve(const DaedricFont &f, const char *s, int cx, int yRune, int yLatin,
                  float reveal, uint16_t runeCol, uint16_t latCol, uint16_t bg, int latSize) {
    float runeA = 1.f - smoothstep(0.0f, 0.6f, reveal);
    float latA = smoothstep(0.45f, 1.0f, reveal);
    if (runeA > 0.02f)
        glyphsCentre(f, s, cx, yRune, blend(runeCol, bg, (uint8_t)(runeA * 255)));
    if (latA > 0.02f)
        textCentre(s, cx, yLatin, blend(latCol, bg, (uint8_t)(latA * 255)), latSize);
}

void diamond(int cx, int cy, int r, uint16_t c, bool filled) {
    Canvas &cv = hal::canvas();
    if (filled) {
        cv.fillTriangle(cx - r, cy, cx, cy - r, cx + r, cy, c);
        cv.fillTriangle(cx - r, cy, cx, cy + r, cx + r, cy, c);
    } else {
        cv.drawLine(cx - r, cy, cx, cy - r, c);
        cv.drawLine(cx, cy - r, cx + r, cy, c);
        cv.drawLine(cx + r, cy, cx, cy + r, c);
        cv.drawLine(cx, cy + r, cx - r, cy, c);
    }
}

void frame(int x, int y, int w, int h, uint16_t line, uint16_t stud) {
    hal::canvas().drawRect(x, y, w, h, line);
    diamond(x, y, 2, stud);
    diamond(x + w - 1, y, 2, stud);
    diamond(x, y + h - 1, 2, stud);
    diamond(x + w - 1, y + h - 1, 2, stud);
}

void text(const char *s, int x, int y, uint16_t c, int size) {
    Canvas &cv = hal::canvas();
    cv.setFont(&fonts::Font0);
    cv.setTextSize(size);
    cv.setTextColor(c);
    cv.setTextDatum(textdatum_t::top_left);
    cv.drawString(s, x, y);
}
void textCentre(const char *s, int cx, int y, uint16_t c, int size) {
    text(s, cx - (int)strlen(s) * 6 * size / 2, y, c, size);
}
void textRight(const char *s, int rx, int y, uint16_t c, int size) {
    text(s, rx - (int)strlen(s) * 6 * size, y, c, size);
}

void header(const char *daedric, const char *latin) {
    Canvas &cv = hal::canvas();
    int w = glyphs(DAEDRIC_S, daedric, 4, 1, C_EMBER);
    text(latin, 4 + w + 6, 5, C_DIM);
    int b = hal::batteryPercent();
    if (b >= 0) {
        char buf[8];
        snprintf(buf, sizeof buf, "%d%%", b);
        textRight(buf, 214, 5, C_DIM);
        cv.drawRect(218, 4, 16, 8, C_DIM);
        cv.fillRect(234, 6, 2, 4, C_DIM);
        cv.fillRect(220, 6, 12 * (b > 100 ? 100 : b) / 100, 4, b < 20 ? C_BLOOD : C_RUST);
    }
    cv.drawFastHLine(0, 18, hal::W, C_CLOT);
    diamond(hal::W / 2, 18, 2, C_RUST);
}

void footer(const char *hint, const char *touchHint) {
#ifdef GRIM_CYD
    hint = touchHint;
    if (!hint) return;
#else
    (void)touchHint;
#endif
    Canvas &cv = hal::canvas();
    cv.drawFastHLine(0, hal::H - 11, hal::W, C_CLOT);
    textCentre(hint, hal::W / 2, hal::H - 9, C_DIM);
}

} // namespace theme
