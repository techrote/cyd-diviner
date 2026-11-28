// Bounded, platform-neutral direct manipulation events. MIT; Diviner contributors.
#pragma once
#include <stdint.h>

// Coordinates are in Diviner's 240 x 135 canvas, after display rotation and
// viewport scaling. Delivered only on the UI thread; never from an ISR/audio
// callback. Return true from a rite's initial pointer event to capture motion.
struct InstrumentPointer {
    int x = 0, y = 0;
    bool start = false;
    bool down = false;
    uint32_t ms = 0;
};

namespace instrument_touch {
struct Viewport {
    int left, top, width, height, bottom;
};

inline bool contains(const Viewport &v, int x, int y) {
    return v.width > 0 && v.height > 0 && x >= v.left && x < v.left + v.width &&
           y >= v.top && y < v.top + v.height && y < v.bottom;
}

// Clamp captured drags to the instrument edge, but reject initial touches in
// letterboxing/overlays with contains(). Positive display dimensions required.
inline InstrumentPointer map(const Viewport &v, int x, int y, bool start,
                             bool down, uint32_t now) {
    auto clamp = [](int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; };
    const int px = clamp(x - v.left, 0, v.width > 0 ? v.width - 1 : 0);
    const int py = clamp(y - v.top, 0, v.height > 0 ? v.height - 1 : 0);
    const int lx = v.width > 0 ? (px * 240 + v.width / 2) / v.width : 0;
    const int ly = v.height > 0 ? (py * 135 + v.height / 2) / v.height : 0;
    return {clamp(lx, 0, 239), clamp(ly, 0, 134), start, down, now};
}
} // namespace instrument_touch
