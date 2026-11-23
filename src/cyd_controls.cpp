#include "cyd_controls.h"

#include "rite.h"
#include "theme.h"
#include <initializer_list>
#include <stdio.h>
#include <string.h>

namespace cyd_controls {
namespace {

constexpr int LCD_W = 320, LCD_H = 240, DOCK_Y = 180, KEYBOARD_Y = 110;
constexpr uint32_t RELEASE_MS = 30, FIRST_REPEAT_MS = 340, REPEAT_MS = 95;

enum class Panel { Main, Volume, Notes, Scales, More, Keyboard };
enum class Action { Send, Main, Volume, Notes, Scales, More, Keyboard, Symbols, Shift, Level };

struct Button {
    const char *label = "";
    hal::Key key{};
    Action action = Action::Send;
};

constexpr Button nav(const char *label, hal::Nav n, bool fn = false) {
    return {label, {0, n, fn}};
}

constexpr Button character(const char *label, char c, bool fn = false) {
    return {label, {c, hal::N_NONE, fn}};
}

constexpr Button local(const char *label, Action action) {
    return {label, {}, action};
}

constexpr Button menu = nav("Menu", hal::N_ESC);
constexpr Button up = nav("Up", hal::N_UP);
constexpr Button down = nav("Down", hal::N_DOWN);
constexpr Button left = nav("<", hal::N_LEFT);
constexpr Button right = nav(">", hal::N_RIGHT);
constexpr Button done = local("Done", Action::Main);
constexpr Button volume = local("Volume", Action::Volume);
constexpr Button notes = local("Notes", Action::Notes);
constexpr Button more = local("More", Action::More);

struct Row {
    Button buttons[10];
    int n = 0;

    void add(std::initializer_list<Button> values) {
        for (Button value : values) buttons[n++] = value;
    }

    void chars(const char *keys) {
        for (; *keys; ++keys) buttons[n++] = character(nullptr, *keys);
    }
};

struct Layout {
    Row rows[5];
    int top = DOCK_Y;
    int n = 2;

    void set(std::initializer_list<Button> first, std::initializer_list<Button> second) {
        rows[0].add(first);
        rows[1].add(second);
    }
};

const Rite *active = nullptr;
TextMode textMode = TextMode::None;
Panel panel = Panel::Main;
bool splash = true, dirty = true, symbols = false, shift = false;
bool contact = false, releasing = false, canRepeat = false;
uint32_t releaseAt = 0, repeatAt = 0;
int heldRow = -1, heldCol = -1;
hal::Key heldKey;
int tapX = -1, tapY = -1;
int displayedVolume = -1;

void show(Panel next) {
    panel = next;
    heldRow = heldCol = -1;
    canRepeat = false;
    dirty = true;
}

void keyboard(Layout &layout) {
    layout.top = KEYBOARD_Y;
    layout.n = 5;
    if (symbols) {
        layout.rows[0].chars("!@#$%^&*()");
        layout.rows[1].chars("-_=+[]{}\\|");
        layout.rows[2].chars(";:'\",./?<>");
        layout.rows[3].chars("~`");
        layout.rows[3].add({nav("Del", hal::N_BACK)});
    } else {
        layout.rows[0].chars("1234567890");
        layout.rows[1].chars(shift ? "QWERTYUIOP" : "qwertyuiop");
        layout.rows[2].chars(shift ? "ASDFGHJKL" : "asdfghjkl");
        layout.rows[3].add({local(shift ? "ABC" : "Shift", Action::Shift)});
        layout.rows[3].chars(shift ? "ZXCVBNM" : "zxcvbnm");
        layout.rows[3].add({nav("Del", hal::N_BACK)});
    }
    layout.rows[4].add({local(symbols ? "ABC" : "Symbols", Action::Symbols),
                        nav("<", hal::N_LEFT, true), nav(">", hal::N_RIGHT, true),
                        character("Space", ' '),
                        active == &RITE_BEAT ? local("Hide", Action::Main) : nav("Cancel", hal::N_ESC, true),
                        nav(active == &RITE_BEAT ? "Cast" : textMode == TextMode::Password ? "Join" :
                            textMode == TextMode::Url ? "Open" : "Send", hal::N_ENTER)});
}

void makeLayout(Layout &layout) {
    switch (panel) {
    case Panel::Keyboard:
        keyboard(layout);
        return;
    case Panel::Volume:
        layout.set({character("Vol -", '-', true), local("", Action::Level), character("Vol +", '=', true)}, {done});
        return;
    case Panel::Notes:
        layout.set({character("C", 'a'), character("C#", 'w'), character("D", 's'), character("D#", 'e'),
                    character("E", 'd'), character("F", 'f'), character("F#", 't'), character("G", 'g')},
                   {character("G#", 'y'), character("A", 'h'), character("A#", 'u'), character("B", 'j'),
                    character("Oct -", 'z'), character("Oct +", 'x'), done});
        return;
    case Panel::Scales:
        layout.set({character("Phrygian", '1'), character("Aeolian", '2'), character("Dorian", '3')},
                   {character("Locrian", '4'), character("Hijaz", '5'), character("Fifths", '6'), done});
        return;
    case Panel::More:
        layout.set({character("BPM -", '['), character("BPM +", ']'), character("Swing", '\''),
                    character("Accent", 'v'), character("Slide", 'l')},
                   {character("Mute", 'm'), character("Row clr", 'c'), nav("Step clr", hal::N_BACK),
                    character("Copy", 'y', true), volume, done});
        return;
    case Panel::Main:
        break;
    }

    if (splash) {
        layout.set({nav("Begin", hal::N_ENTER)}, {});
    } else if (!active) {
        layout.set({nav("Previous", hal::N_LEFT), nav("Open", hal::N_ENTER), nav("Next", hal::N_RIGHT)}, {volume});
    } else if (active == &RITE_DRONE) {
        layout.set({menu, nav("Bright -", hal::N_DOWN), nav("Bright +", hal::N_UP),
                    nav("Drift -", hal::N_LEFT), nav("Drift +", hal::N_RIGHT), character("Hold", ' ')},
                   {notes, local("Scales", Action::Scales), character("Wind", 'n'), character("Bells", 'b'),
                    character("Safe", 'q'), character("Reseed", 'r'), volume});
    } else if (active == &RITE_SEQ) {
        layout.set({menu, left, right, up, down, nav("Step", hal::N_ENTER), character("Play", ' ')},
                   {notes, nav("Bank", hal::N_TAB), character("Fill", 'q'), character("Roll", 'r'),
                    character("Song", 'g', true), more});
    } else if (active == &RITE_BEAT) {
        layout.set({menu, local("Edit", Action::Keyboard), nav("Cast", hal::N_ENTER), nav("Preset", hal::N_TAB)},
                   {nav("Rate -", hal::N_DOWN, true), nav("Rate +", hal::N_UP, true),
                    character("Reset t", 'r', true), nav("Clear", hal::N_BACK, true), volume});
    } else if (active == &RITE_WIRED) {
        layout.set({menu, nav("Tilt -", hal::N_DOWN), nav("Tilt +", hal::N_UP), nav("Spin -", hal::N_LEFT),
                    nav("Spin +", hal::N_RIGHT), character("Pause", ' ')},
                   {character("Torus", '1'), character("Navi", '2'), character("Solids", '3'),
                    character("Tunnel", '4'), character("Sound", 's'), character("Mono", 'm'), volume});
    } else if (active == &RITE_VISIONS) {
        layout.set({menu, nav("Speed -", hal::N_LEFT), nav("Speed +", hal::N_RIGHT), character("Pause", ' '),
                    character("Sound", 's'), volume},
                   {character("Plasma", '1'), character("Gate", '2'), character("Ashfire", '3'),
                    character("Rain", '4'), character("Kaleido", '5')});
    } else if (active == &RITE_ORACLE) {
        layout.set({menu, nav("Cast", hal::N_ENTER), character("Utterance", ' ')}, {volume});
    } else if (active == &RITE_LINK) {
        layout.set({menu, up, down, nav("Join", hal::N_ENTER)},
                   {character("Scan", 'r'), character("Forget", 'f'), character("Portal", 'p'), volume});
    } else if (active == &RITE_SCRY) {
        layout.set({menu, up, down, nav("Detail", hal::N_ENTER)}, {character("Scan", 'r'), volume});
    } else if (active == &RITE_LISTEN) {
        layout.set({menu, nav("Channel -", hal::N_LEFT), nav("Channel +", hal::N_RIGHT), nav("Hop/Hold", hal::N_ENTER)},
                   {character("Mute", ' '), volume});
    } else if (active == &RITE_KIN) {
        layout.set({menu, up, down}, {volume});
    } else if (active == &RITE_NET) {
        layout.set({menu, up, down, nav("Scan", hal::N_ENTER)}, {volume});
    } else if (active == &RITE_GEMINI) {
        if (textMode == TextMode::Waiting) {
            layout.set({menu}, {volume});
        } else if (textMode == TextMode::Bookmarks) {
            layout.set({nav("Back", hal::N_ESC, true), up, down, nav("Open", hal::N_ENTER)}, {volume});
        } else {
            layout.set({menu, up, down, character("Page back", 'u')},
                       {character("Open URL", 'g'), character("Bookmarks", 'b'), volume});
        }
    } else if (active == &RITE_SENTINEL) {
        layout.set({menu, up, down, nav("Trust", hal::N_ENTER), character("Arm/Disarm", ' ')},
                   {character("Wi-Fi", 'w'), character("BLE", 'b'), character("Relearn", 'l'), character("Mute", 'm'), volume});
    } else if (active == &RITE_PULSE) {
        layout.set({menu, nav("Sens -", hal::N_DOWN), nav("Sens +", hal::N_UP), character("Baseline", 'l')},
                   {character("Mute", ' '), character("Mono", 'm'), volume});
    } else {
        layout.set({menu}, {volume});
    }
}

bool repeatable(hal::Key key) {
    return key.nav == hal::N_LEFT || key.nav == hal::N_RIGHT || key.nav == hal::N_UP ||
           key.nav == hal::N_DOWN || (key.nav == hal::N_BACK && !key.fn);
}

bool dispatch(Button button, uint32_t now, hal::Key &out) {
    switch (button.action) {
    case Action::Main: show(Panel::Main); return false;
    case Action::Volume: show(Panel::Volume); return false;
    case Action::Notes: show(Panel::Notes); return false;
    case Action::Scales: show(Panel::Scales); return false;
    case Action::More: show(Panel::More); return false;
    case Action::Keyboard: show(Panel::Keyboard); return false;
    case Action::Symbols: symbols = !symbols; dirty = true; return false;
    case Action::Shift: shift = !shift; dirty = true; return false;
    case Action::Level: return false;
    case Action::Send: break;
    }
    if (!button.key.ch && !button.key.nav) return false;
    out = heldKey = button.key;
    canRepeat = repeatable(out);
    repeatAt = now + FIRST_REPEAT_MS;
    if (panel == Panel::Keyboard && shift && out.ch >= 'A' && out.ch <= 'Z') {
        shift = false;
        dirty = true;
    }
    return true;
}

bool tapViewport(int x, int y, uint32_t now, hal::Key &out) {
    if (splash) return dispatch(nav("", hal::N_ENTER), now, out);
    if (panel == Panel::Keyboard) return false;
    if (!active && y >= 30 && y < 147) {
        hal::Nav key = x < 90 ? hal::N_LEFT : x >= 230 ? hal::N_RIGHT : hal::N_ENTER;
        // Carousel cards are discrete taps, unlike the dock arrows.
        out = nav("", key).key;
        return true;
    }
    if (active == &RITE_BEAT && y >= 122 && y < 164) {
        show(Panel::Keyboard);
    } else if (active == &RITE_SEQ || active == &RITE_GEMINI) {
        tapX = x * 3 / 4;
        tapY = y * 3 / 4;
    }
    return false;
}

int rowEdge(const Layout &layout, int row) {
    return layout.top + (LCD_H - layout.top) * row / layout.n;
}

} // namespace

void setRite(const Rite *rite) {
    splash = false;
    active = rite;
    textMode = TextMode::None;
    symbols = shift = false;
    tapX = tapY = -1;
    show(Panel::Main);
}

void setTextMode(TextMode mode) {
    if (textMode == mode) return;
    textMode = mode;
    symbols = shift = false;
    tapX = tapY = -1;
    const bool edit = mode == TextMode::Password || mode == TextMode::Url || mode == TextMode::Prompt;
    show(edit ? Panel::Keyboard : Panel::Main);
}

int viewportTop() { return panel == Panel::Keyboard && active == &RITE_BEAT ? KEYBOARD_Y - DOCK_Y : 0; }
int viewportBottom() { return panel == Panel::Keyboard ? KEYBOARD_Y : DOCK_Y; }

bool takeViewportTap(int &x, int &y) {
    if (tapX < 0) return false;
    x = tapX;
    y = tapY;
    tapX = tapY = -1;
    return true;
}

bool anyHeld() { return contact && !releasing; }

bool touch(bool down, int x, int y, uint32_t now, hal::Key &out) {
    if (!down) {
        if (contact && !releasing) {
            releasing = true;
            releaseAt = now;
        } else if (contact && now - releaseAt >= RELEASE_MS) {
            contact = releasing = canRepeat = false;
            heldRow = heldCol = -1;
            dirty = true;
        }
        return false;
    }
    releasing = false;
    if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return false;

    if (!contact && y < viewportBottom()) {
        contact = true;
        return tapViewport(x, y, now, out);
    }

    if (contact && !canRepeat) return false;
    Layout layout;
    makeLayout(layout);
    int row = 0;
    while (row + 1 < layout.n && y >= rowEdge(layout, row + 1)) ++row;
    const Row &line = layout.rows[row];
    int col = x * line.n / LCD_W;

    if (contact) {
        if (y < layout.top || row != heldRow || col != heldCol) {
            canRepeat = false;
            heldRow = heldCol = -1;
            dirty = true;
        } else if (int32_t(now - repeatAt) >= 0) {
            repeatAt = now + REPEAT_MS;
            out = heldKey;
            return true;
        }
        return false;
    }
    contact = true;
    if (!line.n) return false;
    heldRow = row;
    heldCol = col;
    dirty = true;
    return dispatch(line.buttons[col], now, out);
}

void draw(lgfx::LGFXBase &display) {
    const int percent = (hal::volume() * 100 + 127) / 255;
    if (!dirty && (panel != Panel::Volume || percent == displayedVolume)) return;
    dirty = false;
    displayedVolume = percent;

    Layout layout;
    makeLayout(layout);
    display.fillRect(0, layout.top, LCD_W, LCD_H - layout.top, C_VOID);
    display.drawFastHLine(0, layout.top, LCD_W, C_RUST);
    display.setFont(&fonts::Font0);
    display.setTextDatum(textdatum_t::middle_center);

    char level[8];
    snprintf(level, sizeof level, "%d%%", percent);
    for (int row = 0; row < layout.n; ++row) {
        const Row &line = layout.rows[row];
        const int y = rowEdge(layout, row) + 1;
        const int h = rowEdge(layout, row + 1) - y - 1;
        for (int col = 0; col < line.n; ++col) {
            const Button &button = line.buttons[col];
            const int x = LCD_W * col / line.n + 2;
            const int w = LCD_W * (col + 1) / line.n - x - 2;
            const bool pressed = heldRow == row && heldCol == col;
            const bool primary = button.key.nav == hal::N_ENTER;
            const uint16_t bg = pressed ? C_BLOOD : primary ? C_CLOT : C_DUSK;
            display.fillRoundRect(x, y, w, h, 3, bg);
            display.drawRoundRect(x, y, w, h, 3, primary || pressed ? C_RUST : C_CLOT);
            const char ch[] = {button.key.ch, 0};
            const char *label = button.action == Action::Level ? level : (button.label ? button.label : ch);
            display.setTextColor(primary || pressed ? C_GLOW : C_BONE);
            display.setTextSize(strlen(label) * 12 + 4 <= static_cast<size_t>(w) ? 2 : 1);
            display.drawString(label, x + w / 2, y + h / 2);
        }
    }
}

} // namespace cyd_controls
