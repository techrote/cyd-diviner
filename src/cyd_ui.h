#pragma once

#include "theme.h"
#include <algorithm>
#include <cstring>

namespace cyd_ui {

// Draw in absolute portrait-page coordinates while touching only this band.
class Surface {
public:
    Canvas &canvas;
    const int top;

    Surface(Canvas &target, int firstRow) : canvas(target), top(firstRow) {}

    int width() const { return hal::W; }
    int height() const { return 320; }

    void fillRect(int x, int y, int w, int h, uint16_t c) { canvas.fillRect(x, y - top, w, h, c); }
    void drawRect(int x, int y, int w, int h, uint16_t c) { canvas.drawRect(x, y - top, w, h, c); }
    void drawFastHLine(int x, int y, int w, uint16_t c) { canvas.drawFastHLine(x, y - top, w, c); }
    void drawFastVLine(int x, int y, int h, uint16_t c) { canvas.drawFastVLine(x, y - top, h, c); }
    void drawLine(int x0, int y0, int x1, int y1, uint16_t c) { canvas.drawLine(x0, y0 - top, x1, y1 - top, c); }
    void fillCircle(int x, int y, int radius, uint16_t c) { canvas.fillCircle(x, y - top, radius, c); }
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
        canvas.fillTriangle(x0, y0 - top, x1, y1 - top, x2, y2 - top, c);
    }

    void diamond(int x, int y, int radius, uint16_t c, bool filled = true) {
        if (filled) {
            fillTriangle(x - radius, y, x, y - radius, x + radius, y, c);
            fillTriangle(x - radius, y, x, y + radius, x + radius, y, c);
        } else {
            drawLine(x - radius, y, x, y - radius, c);
            drawLine(x, y - radius, x + radius, y, c);
            drawLine(x + radius, y, x, y + radius, c);
            drawLine(x, y + radius, x - radius, y, c);
        }
    }

    void frame(int x, int y, int w, int h, uint16_t line, uint16_t stud) {
        if (!intersects(y - 2, h + 4)) return;
        drawRect(x, y, w, h, line);
        diamond(x, y, 2, stud);
        diamond(x + w - 1, y, 2, stud);
        diamond(x, y + h - 1, 2, stud);
        diamond(x + w - 1, y + h - 1, 2, stud);
    }

    void text(const char *s, int x, int y, uint16_t c, int size = 1) {
        if (!intersects(y, 8 * size)) return;
        canvas.setFont(&fonts::Font0);
        canvas.setTextSize(size);
        canvas.setTextColor(c);
        canvas.setTextDatum(lgfx::textdatum_t::top_left);
        canvas.drawString(s, x, y - top);
    }
    void textCentre(const char *s, int x, int y, uint16_t c, int size = 1) {
        text(s, x - static_cast<int>(std::strlen(s)) * 6 * size / 2, y, c, size);
    }
    void textRight(const char *s, int x, int y, uint16_t c, int size = 1) {
        text(s, x - static_cast<int>(std::strlen(s)) * 6 * size, y, c, size);
    }

    int glyphs(const DaedricFont &font, const char *s, int x, int y, uint16_t c,
              int spacing = 2, uint8_t opacity = 255, int transparent = -1) {
        if (!intersects(y, font.h)) return theme::glyphWidth(font, s, spacing);
        return theme::glyphs(canvas, font, s, x, y - top, c, spacing, opacity, transparent);
    }
    void glyphsCentre(const DaedricFont &font, const char *s, int x, int y, uint16_t c,
                      int spacing = 2, uint8_t opacity = 255, int transparent = -1) {
        if (!intersects(y, font.h)) return;
        glyphs(font, s, x - theme::glyphWidth(font, s, spacing) / 2, y, c, spacing, opacity, transparent);
    }

    bool intersects(int y, int h) const { return y < top + canvas.height() && y + h > top; }
};

} // namespace cyd_ui
