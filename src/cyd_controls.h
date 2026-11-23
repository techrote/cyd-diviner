#pragma once

#include "hal.h"

struct Rite;

namespace cyd_controls {

enum class TextMode { None, Password, Url, Prompt, Bookmarks, Waiting };

void setRite(const Rite *rite);
void setTextMode(TextMode mode);

// Coordinates are LCD pixels, after touch calibration and rotation.
bool touch(bool down, int x, int y, uint32_t now, hal::Key &out);
bool anyHeld();
void draw(lgfx::LGFXBase &display);
int viewportTop();
int viewportBottom();

// Sequence steps and Gemini links use the original app canvas coordinates.
bool takeViewportTap(int &x, int &y);

} // namespace cyd_controls
