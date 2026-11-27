#pragma once

#include "hal.h"

struct Rite;
namespace theme { struct Colourway; struct Palette; }

namespace cyd_controls {

enum class TextMode { None, Password, Url, Prompt, Bookmarks, Waiting };
enum class Orientation : uint8_t { Landscape = 0, Portrait = 1, Landscape180 = 2, Portrait180 = 3 };
enum class MenuBackdrop : uint8_t { Theme, Rain, Ashfire, Plasma };

void loadSettings();
Orientation orientation();
bool isPortrait();
MenuBackdrop menuBackdrop();
void openCarouselSettings();
void backCarouselSettings();
bool carouselSettingsVisible();
void drawCarouselSettings(int transparent);
void drawPortraitSettings(Canvas &band, int top);
bool saveColourway(const theme::Colourway &colourway);
void setRite(const Rite *rite);
void setTextMode(TextMode mode);
inline constexpr int VisionsArtTop = 19;
bool visionsImmersive();

// Coordinates are LCD pixels, after touch calibration and rotation.
bool touch(bool down, int x, int y, uint32_t now, hal::Key &out);
bool anyHeld();
void draw(lgfx::LGFXBase &display);
int displayWidth();
int displayHeight();
// Scene dimensions and origin, plus the bottom edge of its display clip.
int viewportWidth();
int viewportHeight();
int viewportLeft();
int viewportTop();
int viewportBottom();

// Sequence steps and Gemini links use the original app canvas coordinates.
bool takeViewportTap(int &x, int &y);
// The palette gets the initial contact, then the most recent motion sample.
bool takePalettePointer(int &x, int &y, bool &start);

} // namespace cyd_controls
