// Daedric / Lain skin shared by every rite: palette (from ~/daedric-login,
// same as the Launcher fork), Daedric glyph text, studded frames, chrome.
#pragma once
#include "daedric_glyphs.h"
#include "hal.h"
#include <array>
#include <cstddef>

namespace theme {

enum class ColourScheme : uint8_t {
    Ember = 0, Neon = 1, Custom = 2, Glacier, Violet, Sunset, Terminal,
    Phosphor, Dust, Hotline, Blueprint, Pearl, Abyss
};
constexpr std::size_t PresetCount = 12;
enum class ColourRole : uint8_t { Background, Panel, Text, Accent };
enum class BackgroundStyle : uint8_t {
    Flat = 0, Gradient, Grid, Scanlines, Dither, Diagonal, Checker, Diamonds, Waves
};
enum class GradientMode : uint8_t { Blend = 0, Multitone, Bands };

struct Colourway {
    std::array<uint32_t, 4> rgb{}; // 0xRRGGBB, ordered by ColourRole
    uint8_t saturation = 100;     // 0..200
    int8_t brightness = 0;      // -100..100
    int8_t contrast = 0;        // -100..100, relative to the background
    BackgroundStyle backgroundStyle = BackgroundStyle::Flat;
    ColourRole gradientStart = ColourRole::Background;
    ColourRole gradientEnd = ColourRole::Accent;
    GradientMode gradientMode = GradientMode::Blend;
};

struct Palette {
    uint16_t background, ash, dusk, clot, blood, rust, ember, glow, bone, dim, umber, stone;
    std::array<uint16_t, 256> intensity;
    BackgroundStyle backgroundStyle = BackgroundStyle::Flat;
    uint16_t gradientTop = 0, gradientBottom = 0;
    GradientMode gradientMode = GradientMode::Blend;
};

namespace detail {
extern const Palette *activePalette;
}

inline const Palette &palette() { return *detail::activePalette; }
ColourScheme colourScheme();
ColourScheme presetScheme(std::size_t index);
bool validColourScheme(ColourScheme scheme);
void setColourScheme(ColourScheme scheme);
const char *colourSchemeName(ColourScheme scheme);
const Palette &presetPalette(ColourScheme scheme);
Colourway presetColourway(ColourScheme scheme);
Colourway generateColourway(uint16_t hue, GradientMode mode = GradientMode::Blend);
void buildPalette(Palette &out, const Colourway &colourway);
bool validColourway(const Colourway &colourway);
bool hasCustomColourway();
const Colourway &customColourway();
void setCustomColourway(const Colourway &colourway);
void drawBackground(Canvas &canvas, int x, int y, int w, int h, const Palette &colours);
void drawSceneBackground(Canvas &canvas, int x, int y, int w, int h, const Palette &colours);
void drawBackground(Canvas &canvas);
#ifdef GRIM_CYD
constexpr int FullBackdropMargin = 23;
void drawBackdropMargins(lgfx::LGFXBase &display, int originY, const Palette &colours);
void drawBackdropPatch(lgfx::LGFXBase &display, int originY, int x, int y, int w, int h, const Palette &colours);
#endif

uint16_t blend(uint16_t fg, uint16_t bg, uint8_t a); // a 0..255
int luma(uint16_t colour);
uint16_t readableInk(uint16_t preferred, uint16_t background);
inline uint16_t ramp(uint8_t v) { return palette().intensity[v]; }

int glyphWidth(const DaedricFont &f, const char *text, int spacing = 2);
int glyphs(Canvas &canvas, const DaedricFont &f, const char *text, int x, int y, uint16_t fg,
           int spacing = 2, uint8_t opacity = 255, int transparent = -1);
int glyphs(const DaedricFont &f, const char *text, int x, int y, uint16_t fg, int spacing = 2);
void glyphsCentre(const DaedricFont &f, const char *text, int cx, int y, uint16_t fg);

// Reveal animation: the same word drawn as Daedric runes dissolving into latin
// letters as `reveal` runs 0..1 (0 = pure runes, 1 = pure english). Both are
// centred on cx; alpha blends toward `bg`. Use for selected menu/list items.
void wordDissolve(const DaedricFont &f, const char *s, int cx, int yRune, int yLatin,
                  float reveal, uint16_t runeCol, uint16_t latCol, uint16_t bg, int latSize = 1);
float smoothstep(float e0, float e1, float x);

void diamond(int cx, int cy, int r, uint16_t c, bool filled = true);
void frame(int x, int y, int w, int h, uint16_t line, uint16_t stud);

// Small built-in 6x8 font helpers (transparent background).
void text(const char *s, int x, int y, uint16_t c, int size = 1);
void textCentre(const char *s, int cx, int y, uint16_t c, int size = 1);
void textRight(const char *s, int rx, int y, uint16_t c, int size = 1);

// Labelled slider: dim label at x, bar of width w starting at bx, filled to v (0..1).
void meter(const char *label, int x, int y, int bx, int w, float v);
// "C", "C#", ... for a MIDI note (any octave, negative-safe).
const char *noteName(int midi);

// Top bar: Daedric name + latin name, battery; bottom hint line.
void header(const char *daedric, const char *latin, int transparent = -1);
void footer(const char *hint, const char *touchHint = nullptr);
// Whole-screen notice when a rite cannot allocate its state.
void starved(const char *daedric, const char *latin);

} // namespace theme

// Semantic RGB565 roles shared by the existing rites and device chrome.
#define C_VOID (theme::palette().background)
#define C_ASH (theme::palette().ash)
#define C_DUSK (theme::palette().dusk)
#define C_CLOT (theme::palette().clot)
#define C_BLOOD (theme::palette().blood)
#define C_RUST (theme::palette().rust)
#define C_EMBER (theme::palette().ember)
#define C_GLOW (theme::palette().glow)
#define C_BONE (theme::palette().bone)
#define C_DIM (theme::palette().dim)
#define C_UMBER (theme::palette().umber)
#define C_STONE (theme::palette().stone)
