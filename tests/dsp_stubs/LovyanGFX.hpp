// Graphics-free test seam only. Real firmware/native builds use LovyanGFX.
#pragma once
#include <cstdint>
namespace lgfx { using rgb565_t = uint16_t; }
class LGFX_Sprite {
public:
#define DSP_TEST_DRAW(name) template<class... Args> void name(Args...) {}
    DSP_TEST_DRAW(fillScreen)
    DSP_TEST_DRAW(drawCircle)
    DSP_TEST_DRAW(drawLine)
    DSP_TEST_DRAW(drawPixel)
    DSP_TEST_DRAW(drawRect)
    DSP_TEST_DRAW(fillCircle)
    DSP_TEST_DRAW(fillRect)
    DSP_TEST_DRAW(drawFastHLine)
    DSP_TEST_DRAW(drawFastVLine)
    DSP_TEST_DRAW(drawEllipse)
    DSP_TEST_DRAW(setClipRect)
    DSP_TEST_DRAW(clearClipRect)
    DSP_TEST_DRAW(pushImage)
#undef DSP_TEST_DRAW
    int width() const { return 240; }
    int getColorDepth() const { return 16; }
    uint16_t readPixel(int,int) const { return 0; }
    uint16_t color8to16(uint8_t c) const { return c; }
    uint8_t color16to8(uint16_t c) const { return uint8_t(c); }
};
