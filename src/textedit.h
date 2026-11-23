// A one-line text field shared by the networked rites (SSID, password, URL,
// Gemini input). Feed it keys; it handles printables, backspace and the
// cursor. Draw it with draw().
#pragma once
#include "hal.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>

struct TextEdit {
    char buf[128] = {};
    int len = 0, cur = 0;
    bool mask = false; // show dots (passwords)

    void clear() { len = cur = 0; buf[0] = 0; }
    void set(const char *s) {
        strncpy(buf, s, sizeof buf - 1);
        buf[sizeof buf - 1] = 0;
        len = cur = strlen(buf);
    }
    // returns true if the key was consumed
    bool key(const hal::Key &k) {
        if (k.nav == hal::N_LEFT && k.fn) { if (cur > 0) --cur; return true; }
        if (k.nav == hal::N_RIGHT && k.fn) { if (cur < len) ++cur; return true; }
        if (k.nav == hal::N_BACK) {
            if (cur > 0) {
                memmove(buf + cur - 1, buf + cur, len - cur + 1);
                --cur;
                --len;
            }
            return true;
        }
        if (k.ch >= 32 && k.ch < 127 && len < (int)sizeof buf - 1) {
            memmove(buf + cur + 1, buf + cur, len - cur + 1);
            buf[cur++] = k.ch;
            ++len;
            return true;
        }
        return false;
    }
    void draw(int x, int y, int w, uint32_t now, uint16_t fg = C_GLOW) {
        Canvas &cv = hal::canvas();
        cv.fillRect(x, y, w, 12, C_ASH);
        theme::frame(x, y, w, 12, C_RUST, C_UMBER);
        const int cells = (w - 6) / 6;
        if (cells < 1) return;
        const int off = cur >= cells ? cur - cells + 1 : 0;
        const int n = len - off < cells ? len - off : cells;
        char shown[sizeof buf];
        if (mask) memset(shown, '*', n);
        else memcpy(shown, buf + off, n);
        shown[n] = 0;
        theme::text(shown, x + 3, y + 3, fg);
        if ((now / 350) & 1) {
            cv.fillRect(x + 3 + (cur - off) * 6, y + 3, 5, 7, C_EMBER);
        }
    }
};
