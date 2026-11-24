#include "cyd_controls.h"

#include "rite.h"
#include "theme.h"
#include <algorithm>
#include <initializer_list>
#include <stdio.h>
#include <string.h>
#ifdef GRIM_DEVICE
#include <nvs.h>
#endif

namespace cyd_controls {
namespace {

constexpr int LCD_W = 320, LCD_H = 240, DOCK_Y = 180, KEYBOARD_Y = 110;
constexpr int ToggleWidth = 30, ToggleHeight = 26, ToggleTop = LCD_H - ToggleHeight;
constexpr uint32_t RELEASE_MS = 30, FIRST_REPEAT_MS = 340, REPEAT_MS = 95;

using theme::ColourScheme;
constexpr size_t PresetsPerPage = 4;
constexpr size_t ThemePageCount = (theme::PresetCount + PresetsPerPage - 1) / PresetsPerPage;
#ifdef GRIM_DEVICE
constexpr char SettingsNamespace[] = "grimui";
constexpr char LegacyColourKey[] = "colour";
constexpr char PaletteKey[] = "palette";
constexpr uint8_t PaletteRecordVersion = 4;
// V1 stores the version, scheme, custom flag, four RGB triples and adjustments.
// V2 adds BackgroundStyle. V3 adds both gradient roles and supports more styles.
// V4 adds GradientMode and supports the expanded scheme IDs.
constexpr size_t PaletteV1Size = 18;
constexpr size_t PaletteV2Size = PaletteV1Size + 1;
constexpr size_t PaletteV3Size = PaletteV2Size + 2;
constexpr size_t PaletteV4Size = PaletteV3Size + 1;
constexpr size_t PaletteRecordSizes[] = {0, PaletteV1Size, PaletteV2Size, PaletteV3Size, PaletteV4Size};
static_assert(sizeof(PaletteRecordSizes) / sizeof(PaletteRecordSizes[0]) == PaletteRecordVersion + 1);
using PaletteRecord = std::array<uint8_t, PaletteV4Size>;
static_assert(theme::Colourway{}.rgb.size() == 4, "The palette record has four RGB colours");

bool writePalette(ColourScheme scheme, const theme::Colourway *custom) {
    PaletteRecord bytes{};
    bytes[0] = PaletteRecordVersion;
    bytes[1] = static_cast<uint8_t>(scheme);
    bytes[2] = custom ? 1 : 0;
    if (custom) {
        size_t offset = 3;
        for (uint32_t rgb : custom->rgb) {
            bytes[offset++] = rgb >> 16;
            bytes[offset++] = rgb >> 8;
            bytes[offset++] = rgb;
        }
        bytes[offset++] = custom->saturation;
        bytes[offset++] = custom->brightness + 100;
        bytes[offset++] = custom->contrast + 100;
        bytes[offset++] = static_cast<uint8_t>(custom->backgroundStyle);
        bytes[offset++] = static_cast<uint8_t>(custom->gradientStart);
        bytes[offset++] = static_cast<uint8_t>(custom->gradientEnd);
        bytes[offset] = static_cast<uint8_t>(custom->gradientMode);
    }

    nvs_handle_t settings;
    if (nvs_open(SettingsNamespace, NVS_READWRITE, &settings) != ESP_OK) return false;
    const bool saved = nvs_set_blob(settings, PaletteKey, bytes.data(), bytes.size()) == ESP_OK &&
                       nvs_commit(settings) == ESP_OK;
    nvs_close(settings);
    return saved;
}

bool loadPalette(nvs_handle_t settings) {
    PaletteRecord bytes{};
    size_t size = bytes.size();
    if (nvs_get_blob(settings, PaletteKey, bytes.data(), &size) != ESP_OK) return false;
    const uint8_t version = bytes[0];
    if (!version || version > PaletteRecordVersion || size != PaletteRecordSizes[version] || bytes[2] > 1) return false;
    const auto lastStyle = version >= 3 ? theme::BackgroundStyle::Waves : theme::BackgroundStyle::Dither;
    if (version >= 2 && bytes[PaletteV1Size] > static_cast<uint8_t>(lastStyle)) return false;
    constexpr auto LastRole = static_cast<uint8_t>(theme::ColourRole::Accent);
    if (version >= 3 && (bytes[PaletteV2Size] > LastRole || bytes[PaletteV2Size + 1] > LastRole)) return false;
    if (version >= 4 && bytes[PaletteV3Size] > static_cast<uint8_t>(theme::GradientMode::Bands)) return false;

    const auto scheme = static_cast<ColourScheme>(bytes[1]);
    const bool hasCustom = bytes[2] != 0;
    if (!theme::validColourScheme(scheme) || (scheme == ColourScheme::Custom && !hasCustom) ||
        (version < 4 && bytes[1] > static_cast<uint8_t>(ColourScheme::Custom))) return false;

    if (hasCustom) {
        theme::Colourway custom{};
        size_t offset = 3;
        for (uint32_t &rgb : custom.rgb) {
            rgb = static_cast<uint32_t>(bytes[offset]) << 16 |
                  static_cast<uint32_t>(bytes[offset + 1]) << 8 | bytes[offset + 2];
            offset += 3;
        }
        custom.saturation = bytes[offset++];
        if (bytes[offset] > 200 || bytes[offset + 1] > 200) return false;
        custom.brightness = static_cast<int>(bytes[offset++]) - 100;
        custom.contrast = static_cast<int>(bytes[offset]) - 100;
        custom.backgroundStyle = version == 1 ? theme::BackgroundStyle::Flat :
                                 static_cast<theme::BackgroundStyle>(bytes[PaletteV1Size]);
        custom.gradientStart = version >= 3 ? static_cast<theme::ColourRole>(bytes[PaletteV2Size]) :
                               theme::ColourRole::Background;
        custom.gradientEnd = version >= 3 ? static_cast<theme::ColourRole>(bytes[PaletteV2Size + 1]) :
                             theme::ColourRole::Accent;
        custom.gradientMode = version >= 4 ? static_cast<theme::GradientMode>(bytes[PaletteV3Size]) :
                              theme::GradientMode::Blend;
        if (!theme::validColourway(custom)) return false;
        theme::setCustomColourway(custom);
    }

    theme::setColourScheme(scheme);
    return true;
}
#endif

enum class Panel { Main, Volume, Theme, Notes, Scales, More, Keyboard };
enum class Action : uint8_t {
    Send, Main, Volume, Theme, Scheme, ThemePrev, ThemeNext, Notes, Scales, More, Keyboard, Symbols, Shift, Level
};

struct Button {
    const char *label = "";
    hal::Key key{};
    Action action = Action::Send;
    ColourScheme scheme = ColourScheme::Neon;
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

Button schemeButton(ColourScheme scheme) {
    return {theme::colourSchemeName(scheme), {}, Action::Scheme, scheme};
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
bool dockVisible = false, dockBeforeKeyboard = false;
bool contact = false, releasing = false, canRepeat = false;
uint32_t releaseAt = 0, repeatAt = 0;
int heldRow = -1, heldCol = -1;
hal::Key heldKey;
int tapX = -1, tapY = -1;
int displayedVolume = -1;
ColourScheme displayedScheme = ColourScheme::Neon;
size_t themePage = 0;
bool paletteDirty = false;
bool paletteContact = false;
struct Point { int x = -1, y = -1; } paletteStart, paletteMotion;

void show(Panel next) {
    if (next == Panel::Keyboard) {
        if (panel != Panel::Keyboard) dockBeforeKeyboard = dockVisible;
        dockVisible = true;
    } else if (panel == Panel::Keyboard) {
        dockVisible = dockBeforeKeyboard;
    }
    panel = next;
    heldRow = heldCol = -1;
    canRepeat = false;
    dirty = true;
}

bool keyboardVisible() {
    return dockVisible && panel == Panel::Keyboard;
}

Point viewportPoint(int x, int y) {
    const int logicalY = (y - viewportTop()) * hal::H / viewportHeight();
    return {x * hal::W / LCD_W, std::clamp(logicalY, 0, hal::H - 1)};
}

void chooseScheme(ColourScheme scheme) {
    if (theme::colourScheme() == scheme) return;
    if (scheme == ColourScheme::Custom && !theme::hasCustomColourway()) return;
#ifdef GRIM_DEVICE
    const auto *custom = theme::hasCustomColourway() ? &theme::customColourway() : nullptr;
    if (!writePalette(scheme, custom)) return;
#endif
    theme::setColourScheme(scheme);
}

void openTheme() {
    const auto current = theme::colourScheme();
    for (size_t i = 0; i < theme::PresetCount; ++i) {
        if (theme::presetScheme(i) == current) {
            themePage = i / PresetsPerPage;
            break;
        }
    }
    show(Panel::Theme);
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
    case Panel::Theme:
        for (size_t i = themePage * PresetsPerPage;
             i < theme::PresetCount && i < (themePage + 1) * PresetsPerPage; ++i) {
            layout.rows[0].add({schemeButton(theme::presetScheme(i))});
        }
        layout.rows[1].add({local("Prev", Action::ThemePrev), local("Next", Action::ThemeNext)});
        if (theme::hasCustomColourway()) {
            layout.rows[1].add({schemeButton(ColourScheme::Custom)});
        }
        layout.rows[1].add({done});
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
        layout.set({nav("Previous", hal::N_LEFT), nav("Open", hal::N_ENTER), nav("Next", hal::N_RIGHT)},
                   {volume, local("Theme", Action::Theme)});
    } else if (active == &RITE_PALETTE) {
        layout.set({nav(paletteDirty ? "Discard" : "Back", hal::N_ESC), character("Ember", 'e'),
                    character("Neon", 'n'), character("Hue", 'g'), character("Save", 's')},
                   {character("Multi", 'm'), character("Bands", 'h'), nav("-", hal::N_LEFT), nav("+", hal::N_RIGHT)});
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
    case Action::Theme: openTheme(); return false;
    case Action::Scheme: chooseScheme(button.scheme); return false;
    case Action::ThemePrev:
        themePage = (themePage + ThemePageCount - 1) % ThemePageCount;
        return false;
    case Action::ThemeNext:
        themePage = (themePage + 1) % ThemePageCount;
        return false;
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

bool tapViewport(Point p, uint32_t now, hal::Key &out) {
    if (splash) return dispatch(nav("", hal::N_ENTER), now, out);
    if (keyboardVisible() || (dockVisible && panel == Panel::Theme)) return false;
    if (!active && p.y >= 22 && p.y < 110) {
        // Cover the full width of the largest settled centre card.
        constexpr int centreHalfWidth = 54;
        hal::Nav key = p.x < hal::W / 2 - centreHalfWidth ? hal::N_LEFT :
                       p.x >= hal::W / 2 + centreHalfWidth ? hal::N_RIGHT : hal::N_ENTER;
        // Carousel cards are discrete taps, unlike the dock arrows.
        out = nav("", key).key;
        return true;
    }
    if (active == &RITE_BEAT && p.y >= 91 && p.y < 123) {
        show(Panel::Keyboard);
    } else if (panel != Panel::Keyboard && (active == &RITE_SEQ || active == &RITE_GEMINI)) {
        tapX = p.x;
        tapY = p.y;
    }
    return false;
}

int rowEdge(const Layout &layout, int row) {
    return layout.top + (LCD_H - layout.top) * row / layout.n;
}

int rowLeft(const Layout &layout, int row) {
    return row == layout.n - 1 ? ToggleWidth : 0;
}

void drawToggle(lgfx::LGFXBase &display) {
    constexpr int cx = ToggleWidth / 2, cy = ToggleTop + ToggleHeight / 2;
    display.fillRoundRect(4, ToggleTop + 3, ToggleWidth - 8, ToggleHeight - 6, 4, 0x1082);
    display.drawRoundRect(4, ToggleTop + 3, ToggleWidth - 8, ToggleHeight - 6, 4, 0xa514);
    const int point = dockVisible ? cy + 3 : cy - 3;
    const int ends = dockVisible ? cy - 2 : cy + 2;
    display.drawLine(cx - 5, ends, cx, point, 0xffff);
    display.drawLine(cx, point, cx + 5, ends, 0xffff);
}

int luma(uint16_t colour) {
    const int r = ((colour >> 11) & 31) * 255 / 31;
    const int g = ((colour >> 5) & 63) * 255 / 63;
    const int b = (colour & 31) * 255 / 31;
    return (299 * r + 587 * g + 114 * b) / 1000;
}

bool hasContrast(uint16_t text, uint16_t background) {
    const int difference = luma(text) - luma(background);
    return difference >= 48 || difference <= -48;
}

const theme::Palette &dockColours(ColourScheme scheme) {
    const auto &colours = theme::palette();
    if (scheme == ColourScheme::Custom &&
        (!hasContrast(colours.bone, colours.dusk) || !hasContrast(colours.glow, colours.clot) ||
         !hasContrast(colours.glow, colours.stone))) {
        return theme::presetPalette(ColourScheme::Neon);
    }
    return colours;
}

} // namespace

void loadSettings() {
#ifdef GRIM_DEVICE
    nvs_handle_t settings;
    if (nvs_open(SettingsNamespace, NVS_READONLY, &settings) != ESP_OK) return;
    if (!loadPalette(settings)) {
        uint8_t value;
        if (nvs_get_u8(settings, LegacyColourKey, &value) == ESP_OK &&
            (value == static_cast<uint8_t>(ColourScheme::Ember) || value == static_cast<uint8_t>(ColourScheme::Neon))) {
            theme::setColourScheme(static_cast<ColourScheme>(value));
        }
    }
    nvs_close(settings);
#endif
}

bool saveColourway(const theme::Colourway &colourway) {
    if (!theme::validColourway(colourway)) return false;
#ifdef GRIM_DEVICE
    if (!writePalette(ColourScheme::Custom, &colourway)) return false;
#endif
    theme::setCustomColourway(colourway);
    theme::setColourScheme(ColourScheme::Custom);
    dirty = true;
    return true;
}

void setRite(const Rite *rite) {
    splash = false;
    active = rite;
    textMode = TextMode::None;
    symbols = shift = false;
    tapX = tapY = -1;
    paletteDirty = paletteContact = false;
    paletteStart = paletteMotion = {};
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

void setPaletteDirty(bool changed) {
    if (paletteDirty == changed) return;
    paletteDirty = changed;
    dirty = true;
}

int viewportHeight() { return DOCK_Y; }
int viewportTop() {
    if (!dockVisible) return (LCD_H - DOCK_Y) / 2;
    return keyboardVisible() && active == &RITE_BEAT ? KEYBOARD_Y - DOCK_Y : 0;
}
int viewportBottom() {
    if (!dockVisible) return LCD_H;
    return keyboardVisible() ? KEYBOARD_Y : DOCK_Y;
}

bool takeViewportTap(int &x, int &y) {
    if (tapX < 0) return false;
    x = tapX;
    y = tapY;
    tapX = tapY = -1;
    return true;
}

bool takePalettePointer(int &x, int &y, bool &start) {
    start = paletteStart.x >= 0;
    Point &point = start ? paletteStart : paletteMotion;
    if (point.x < 0) return false;
    x = point.x;
    y = point.y;
    point = {};
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
            paletteContact = false;
            heldRow = heldCol = -1;
            dirty = true;
        }
        return false;
    }
    releasing = false;
    if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return false;

    if (!contact && x < ToggleWidth && y >= ToggleTop) {
        contact = true;
        dockVisible = !dockVisible;
        dirty = true;
        return false;
    }

    if (!contact && y < viewportBottom()) {
        contact = true;
        const int sceneY = y - viewportTop();
        if (sceneY < 0 || sceneY >= viewportHeight()) return false;
        const Point p = viewportPoint(x, y);
        if (active == &RITE_PALETTE) {
            paletteContact = true;
            paletteStart = p;
            paletteMotion = {};
            return false;
        }
        return tapViewport(p, now, out);
    }

    if (paletteContact) {
        paletteMotion = viewportPoint(x, y);
        return false;
    }

    if (contact && !canRepeat) return false;
    Layout layout;
    makeLayout(layout);
    int row = 0;
    while (row + 1 < layout.n && y >= rowEdge(layout, row + 1)) ++row;
    const Row &line = layout.rows[row];
    const int left = rowLeft(layout, row);
    const int col = x >= left && line.n ? ((x - left + 1) * line.n - 1) / (LCD_W - left) : -1;

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
    if (col < 0) return false;
    heldRow = row;
    heldCol = col;
    dirty = true;
    return dispatch(line.buttons[col], now, out);
}

void draw(lgfx::LGFXBase &display) {
    if (!dockVisible) {
        drawToggle(display);
        return;
    }
    const int percent = (hal::volume() * 100 + 127) / 255;
    const ColourScheme scheme = theme::colourScheme();
    if (!dirty && scheme == displayedScheme && (panel != Panel::Volume || percent == displayedVolume)) return;
    dirty = false;
    displayedVolume = percent;
    displayedScheme = scheme;

    Layout layout;
    makeLayout(layout);
    const auto &colours = dockColours(scheme);
    display.fillRect(0, layout.top, LCD_W, LCD_H - layout.top, colours.background);
    display.drawFastHLine(0, layout.top, LCD_W, colours.rust);
    display.setFont(&fonts::Font0);
    display.setTextDatum(textdatum_t::middle_center);

    char level[8];
    snprintf(level, sizeof level, "%d%%", percent);
    const bool extraPreset = scheme != ColourScheme::Ember && scheme != ColourScheme::Neon && scheme != ColourScheme::Custom;
    const uint16_t pressedBackground = scheme == ColourScheme::Ember ? colours.blood :
                                       extraPreset ? theme::blend(colours.stone, colours.clot, 128) : colours.stone;
    for (int row = 0; row < layout.n; ++row) {
        const Row &line = layout.rows[row];
        const int left = rowLeft(layout, row);
        const int width = LCD_W - left;
        const int y = rowEdge(layout, row) + 1;
        const int h = rowEdge(layout, row + 1) - y - 1;
        for (int col = 0; col < line.n; ++col) {
            const Button &button = line.buttons[col];
            const int x = left + width * col / line.n + 2;
            const int w = left + width * (col + 1) / line.n - x - 2;
            const bool pressed = heldRow == row && heldCol == col;
            const bool selected = button.action == Action::Scheme && button.scheme == scheme;
            const bool primary = button.key.nav == hal::N_ENTER || selected ||
                                 (active == &RITE_PALETTE && button.key.ch == 's');
            const uint16_t bg = pressed ? pressedBackground : primary ? colours.clot : colours.dusk;
            display.fillRoundRect(x, y, w, h, 3, bg);
            display.drawRoundRect(x, y, w, h, 3, primary || pressed ? colours.rust : colours.clot);
            const char ch[] = {button.key.ch, 0};
            const char *label = button.action == Action::Level ? level : (button.label ? button.label : ch);
            display.setTextColor((primary || pressed) && !extraPreset ? colours.glow : colours.bone);
            display.setTextSize(strlen(label) * 12 + 4 <= static_cast<size_t>(w) ? 2 : 1);
            display.drawString(label, x + w / 2, y + h / 2);
        }
    }
    drawToggle(display);
}

} // namespace cyd_controls
