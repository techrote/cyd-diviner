// Hardware seam for the Cardputer ADV, ESP32 CYD, and SDL simulator.
#pragma once
#include <stdint.h>

#if defined(GRIM_DEVICE) && !defined(GRIM_CYD)
#include <M5GFX.h>
using Canvas = M5Canvas;
#else
#include <LovyanGFX.hpp>
using Canvas = LGFX_Sprite;
#endif

namespace hal {

constexpr int W = 240, H = 135;
constexpr int RATE = 24000; // audio sample rate, mono int16

// Navigation codes. On the Cardputer the arrow keys are ; . , / (printed on
// the caps); a Key carries both the character and the arrow meaning, and each
// screen decides which one it wants (text editors only take arrows with Fn).
enum Nav : uint8_t { N_NONE, N_UP, N_DOWN, N_LEFT, N_RIGHT, N_ENTER, N_BACK, N_ESC, N_TAB };

struct Key {
    char ch = 0;      // printable character after shift, 0 if none
    uint8_t nav = 0;  // Nav
    bool fn = false;  // Fn held
    bool ctrl = false;
};

typedef void (*AudioFn)(int16_t *out, int n);

void begin();
Canvas &canvas(); // 240x135 back buffer; draw here, then present()
void present();
#ifdef GRIM_CYD
// The renderer writes a complete frame to the oriented LCD in one transaction.
using FullscreenRenderer = void (*)(lgfx::LGFXBase &display, uint32_t now);
void drawVisionsFullscreen(FullscreenRenderer render, uint32_t now);
void presentNativeMenu(FullscreenRenderer render, uint32_t now);
// Ambient artwork can submit points above and below the logical canvas.
void queueBackdropMote(int x, int y, uint16_t colour, uint8_t alpha);
#ifdef GRIM_CYD_DIAGNOSTICS
void setFrameDrawUs(uint32_t elapsed);
#endif
#endif
uint32_t ms();
void idle(uint32_t msec);

bool pollKey(Key &k);  // one queued key press (with auto-repeat for arrows)
bool anyHeld();

// Control thread only, outside a callback. Return only after any old in-flight
// invocation completes. The caller may then destroy the detached voice state.
// Exactly one audio consumer. Callbacks allocate nothing and must not call
// setAudio/setOverlayAudio. Overlay replacement has the same lifetime guarantee.
void setAudio(AudioFn fn);        // main voice (rite/menu); nullptr = silence
void setOverlayAudio(AudioFn fn); // mixed on top of the main voice (UI sfx)
void setVolume(uint8_t v); // 0..255
uint8_t volume();

int batteryPercent(); // -1 if unknown
uint32_t heapFree();    // bytes free / largest allocatable block (0 on the desktop)
uint32_t heapLargest();
void debugf(const char *fmt, ...); // USB serial on the device, stderr in the sim

void backToLauncher(); // restart; the Launcher bootscreen comes up first

} // namespace hal
