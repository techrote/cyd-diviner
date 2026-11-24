#pragma once

#include "hal.h"

struct Rite;
namespace theme { struct Colourway; }

namespace cyd_controls {

enum class TextMode { None, Password, Url, Prompt, Bookmarks, Waiting };

void loadSettings();
bool saveColourway(const theme::Colourway &colourway);
void setRite(const Rite *rite);
void setTextMode(TextMode mode);
void setPaletteDirty(bool changed);
inline constexpr int VisionsArtTop = 19;
bool visionsImmersive();

// Coordinates are LCD pixels, after touch calibration and rotation.
bool touch(bool down, int x, int y, uint32_t now, hal::Key &out);
bool anyHeld();
void draw(lgfx::LGFXBase &display);
// Scene height and origin, plus the bottom edge of its display clip.
int viewportHeight();
int viewportTop();
int viewportBottom();

// Sequence steps and Gemini links use the original app canvas coordinates.
bool takeViewportTap(int &x, int &y);
// The palette gets the initial contact, then the most recent motion sample.
bool takePalettePointer(int &x, int &y, bool &start);

} // namespace cyd_controls
