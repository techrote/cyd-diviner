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

bool visionsUsesRgb444();
bool visionsAreLayered();
bool visionsPaused();
bool visionsSoundEnabled();
int visionsChoice();

namespace cyd_controls {
namespace {

constexpr int LongEdge = 320, ShortEdge = 240;
constexpr int LandscapeDockY = 180, LandscapeKeyboardY = 110;
constexpr int PortraitDockY = 230, PortraitKeyboardY = 170;
constexpr int ToggleWidth = 30, ToggleHeight = 26, CarouselDockHeight = 30;
constexpr uint32_t RELEASE_MS = 30, FIRST_REPEAT_MS = 340, REPEAT_MS = 95;

using theme::ColourScheme;
constexpr char OrientationKey[] = "orientation";
constexpr char BackdropKey[] = "menu_backdrop";
Orientation currentOrientation = Orientation::Landscape;
MenuBackdrop currentBackdrop = MenuBackdrop::Theme;

int dockTop() { return isPortrait() ? PortraitDockY : LandscapeDockY; }
int keyboardTop() { return isPortrait() ? PortraitKeyboardY : LandscapeKeyboardY; }

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

bool saveOption(const char *key, uint8_t value) {
#ifdef GRIM_DEVICE
    nvs_handle_t settings;
    if (nvs_open(SettingsNamespace, NVS_READWRITE, &settings) != ESP_OK) return false;
    const bool saved = nvs_set_u8(settings, key, value) == ESP_OK && nvs_commit(settings) == ESP_OK;
    nvs_close(settings);
    return saved;
#else
    (void)key;
    (void)value;
    return true;
#endif
}

enum class Panel { Main, Volume, Notes, Scales, More, Keyboard, VisionsEffects, VisionsOptions };
enum class SettingsPage { Closed, Home, Theme, Display };
enum class Action : uint8_t {
    Send, Main, Volume, Notes, Scales, More, Keyboard, Symbols, Shift, Level, VisionsEffects, VisionsOptions
};

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
constexpr const char *VisionsOverlays[] = {"+ Kaleido", "+ Trails", "+ Rain"};

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
    int top = dockTop();
    int n = 2;

    void set(std::initializer_list<Button> first, std::initializer_list<Button> second) {
        rows[0].add(first);
        rows[1].add(second);
    }
};

const Rite *active = nullptr;
TextMode textMode = TextMode::None;
Panel panel = Panel::Main;
SettingsPage settingsPage = SettingsPage::Closed;
bool splash = true, dirty = true, symbols = false, shift = false;
bool dockVisible = false, dockBeforeKeyboard = false, dockBeforeSettings = false, dockBeforeFocusedRite = false;
bool immersive = false;
bool contact = false, releasing = false, canRepeat = false;
uint32_t releaseAt = 0, repeatAt = 0;
int heldRow = -1, heldCol = -1;
hal::Key heldKey;
int tapX = -1, tapY = -1;
int displayedVolume = -1;
ColourScheme displayedScheme = ColourScheme::Neon;
bool paletteContact = false;
struct Point { int x = -1, y = -1; } paletteStart, paletteMotion;

bool focusedRite(const Rite *rite) { return rite == &RITE_PALETTE || rite == &RITE_VISIONS; }

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

bool carouselMain() {
    return !splash && !active && settingsPage == SettingsPage::Closed;
}

Point viewportPoint(int x, int y) {
    const int px = x - viewportLeft(), py = y - viewportTop();
    const int logicalX = isPortrait() ? px : (px * 3 + 2) / 4;
    const int logicalY = isPortrait() ? py : (py * 3 + 2) / 4;
    return {std::clamp(logicalX, 0, hal::W - 1), std::clamp(logicalY, 0, hal::H - 1)};
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

struct Area {
    int x, y, w, h;
    bool contains(Point p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
};

constexpr Area SettingsBack{174, 0, 66, 19};
constexpr Area VolumeMinus{136, 27, 26, 22}, VolumePlus{197, 27, 29, 22};
constexpr Area SettingsTheme{14, 51, 212, 22}, SettingsDisplay{14, 75, 212, 22}, SettingsPalette{14, 99, 212, 24};
constexpr Area TurnChoice{174, 38, 52, 26};
constexpr Area CustomChoice{14, 106, 212, 18};
constexpr const char *OrientationNames[] = {"Landscape", "Portrait", "Landscape 180", "Portrait 180"};
constexpr const char *BackdropNames[] = {"Theme", "Rain", "Ashfire", "Plasma"};

constexpr Area orientationChoice(int i) { return {14 + 80 * i, 38, 76, 26}; }
constexpr Area backdropChoice(int i) { return {14 + 54 * i, 86, 50, 25}; }
constexpr Area themeChoice(size_t i) { return {14 + 54 * int(i % 4), 25 + 27 * int(i / 4), 50, 24}; }

void changeOrientation(uint8_t value) {
    if (value != static_cast<uint8_t>(currentOrientation) && saveOption(OrientationKey, value)) {
        currentOrientation = static_cast<Orientation>(value);
        dirty = true;
    }
}

bool tapSettings(Point point, hal::Key &out) {
    if (SettingsBack.contains(point)) {
        out = {0, hal::N_ESC};
        return true;
    }
    switch (settingsPage) {
    case SettingsPage::Home:
        if (VolumeMinus.contains(point) || VolumePlus.contains(point)) {
            out = {VolumeMinus.contains(point) ? '-' : '='};
            return true;
        }
        if (SettingsTheme.contains(point)) settingsPage = SettingsPage::Theme;
        else if (SettingsDisplay.contains(point)) settingsPage = SettingsPage::Display;
        else if (SettingsPalette.contains(point)) {
            out = {'p'};
            return true;
        }
        break;
    case SettingsPage::Theme:
        for (size_t i = 0; i < theme::PresetCount; ++i) {
            if (themeChoice(i).contains(point)) chooseScheme(theme::presetScheme(i));
        }
        if (CustomChoice.contains(point)) chooseScheme(ColourScheme::Custom);
        break;
    case SettingsPage::Display: {
        const auto rotation = static_cast<uint8_t>(currentOrientation);
        if (TurnChoice.contains(point)) changeOrientation(rotation ^ 2);
        else if (orientationChoice(0).contains(point)) changeOrientation(rotation & 2);
        else if (orientationChoice(1).contains(point)) changeOrientation((rotation & 2) | 1);
        for (uint8_t i = 0; i < 4; ++i) {
            if (backdropChoice(i).contains(point) && i != static_cast<uint8_t>(currentBackdrop) && saveOption(BackdropKey, i)) {
                currentBackdrop = static_cast<MenuBackdrop>(i);
            }
        }
        break;
    }
    case SettingsPage::Closed:
        break;
    }
    return false;
}

void settingRow(Area area, const char *label, const char *value = nullptr) {
    Canvas &cv = hal::canvas();
    cv.fillRect(area.x, area.y, area.w, area.h, C_DUSK);
    cv.drawFastHLine(area.x, area.y + area.h - 1, area.w, C_CLOT);
    theme::text(label, area.x + 7, area.y + (area.h - 8) / 2, theme::readableInk(C_BONE, C_DUSK));
    if (value) {
        theme::textRight(value, area.x + area.w - 20, area.y + (area.h - 8) / 2, theme::readableInk(C_DIM, C_DUSK));
        theme::text(">", area.x + area.w - 14, area.y + (area.h - 8) / 2, theme::readableInk(C_GLOW, C_DUSK));
    }
}

void settingChoice(Area area, const char *label, bool selected = false, bool swatch = false, uint16_t ink = 0) {
    Canvas &cv = hal::canvas();
    const uint16_t bg = selected ? C_CLOT : C_DUSK;
    cv.fillRect(area.x, area.y, area.w, area.h, bg);
    theme::textCentre(label, area.x + area.w / 2, area.y + (area.h - 8) / 2 - (swatch ? 3 : 0),
                      theme::readableInk(selected ? C_GLOW : C_BONE, bg));
    if (swatch) cv.drawFastHLine(area.x + area.w / 2 - 10, area.y + area.h - 6, 20, ink);
    if (selected) cv.drawFastHLine(area.x, area.y + area.h - 1, area.w, C_EMBER);
}

void keyboard(Layout &layout) {
    layout.top = keyboardTop();
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
        layout.rows[3].add({local(shift ? "ABC" : isPortrait() ? "Aa" : "Shift", Action::Shift)});
        layout.rows[3].chars(shift ? "ZXCVBNM" : "zxcvbnm");
        layout.rows[3].add({nav("Del", hal::N_BACK)});
    }
    layout.rows[4].add({local(symbols ? "ABC" : isPortrait() ? "Sym" : "Symbols", Action::Symbols),
                        nav("<", hal::N_LEFT, true), nav(">", hal::N_RIGHT, true),
                        character("Space", ' '),
                        active == &RITE_BEAT ? local("Hide", Action::Main) :
                            nav(isPortrait() ? "Back" : "Cancel", hal::N_ESC, true),
                        nav(active == &RITE_BEAT ? "Cast" : textMode == TextMode::Password ? "Join" :
                            textMode == TextMode::Url ? "Open" : "Send", hal::N_ENTER)});
}

void buildLayout(Layout &layout) {
    switch (panel) {
    case Panel::Keyboard:
        keyboard(layout);
        return;
    case Panel::Volume:
        layout.set({character("Vol -", '-', true), local("", Action::Level), character("Vol +", '=', true)},
                   {active == &RITE_VISIONS ? local("Back", Action::VisionsOptions) : done});
        return;
    case Panel::VisionsEffects:
        layout.top = displayHeight() - 58;
        if (visionsAreLayered()) {
            layout.set({character("Plasma", '1'), character("Gate", '2'), character("Ashfire", '3')}, {done});
        } else {
            layout.set({character("Plasma", '1'), character("Gate", '2'), character("Ashfire", '3')},
                       {character("Rain", '4'), character("Kaleido", '5'), character("Trails", '6'), done});
        }
        return;
    case Panel::VisionsOptions:
        layout.top = displayHeight() - 56;
        layout.rows[0].add({nav("Slower", hal::N_LEFT), nav("Faster", hal::N_RIGHT), volume});
        layout.rows[1].add({character(visionsAreLayered() ? "Layered" : "Solo", 'l')});
        if (!visionsAreLayered() && visionsChoice() == 5) {
            layout.rows[1].add({character(visionsUsesRgb444() ? "RGB444" : "RGB565", 'f')});
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
        layout.top = displayHeight() - CarouselDockHeight;
        layout.n = 1;
        layout.rows[0].add({nav("Previous", hal::N_LEFT), nav("Open", hal::N_ENTER), nav("Next", hal::N_RIGHT)});
    } else if (active == &RITE_DRONE) {
        layout.set({menu, nav(isPortrait() ? "Bright-" : "Bright -", hal::N_DOWN),
                    nav(isPortrait() ? "Bright+" : "Bright +", hal::N_UP),
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
        layout.top = displayHeight() - 30;
        layout.n = 1;
        layout.rows[0].add({local("Effects", Action::VisionsEffects), character(visionsPaused() ? "Play" : "Pause", ' '),
                           character(visionsSoundEnabled() ? "Mute" : "Unmute", 's'), local("Options", Action::VisionsOptions)});
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

void makeLayout(Layout &layout) {
    buildLayout(layout);
    if (!isPortrait() || layout.n != 2 ||
        (layout.rows[0].n <= 5 && layout.rows[1].n <= 4)) return;

    // Reflow crowded dock buttons in reading order. The last row has
    // slightly less room because it also contains the dock toggle.
    const Row first = layout.rows[0], second = layout.rows[1];
    const int count = first.n + second.n;
    layout.n = count > 9 ? 3 : 2;
    int index = 0;
    for (int r = 0; r < layout.n; ++r) {
        Row &row = layout.rows[r];
        const int remainingRows = layout.n - r;
        row.n = (count - index + remainingRows - 1) / remainingRows;
        for (int col = 0; col < row.n; ++col, ++index) {
            row.buttons[col] = index < first.n ? first.buttons[index] : second.buttons[index - first.n];
        }
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
    case Action::VisionsEffects: show(Panel::VisionsEffects); return false;
    case Action::VisionsOptions: show(Panel::VisionsOptions); return false;
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
    if (keyboardVisible()) return false;
    if (carouselSettingsVisible()) return tapSettings(p, out);
    if (!active && p.y >= 22 && p.y < 123) {
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
    } else if (active == &RITE_VISIONS) {
        if (p.y < VisionsArtTop && p.x < 39) {
            out = {0, hal::N_ESC};
            return true;
        }
        if (p.y >= VisionsArtTop || p.x >= 222) immersive = true;
    } else if (panel != Panel::Keyboard && (active == &RITE_SEQ || active == &RITE_GEMINI)) {
        tapX = p.x;
        tapY = p.y;
    }
    return false;
}

int rowEdge(const Layout &layout, int row) {
    return layout.top + (displayHeight() - layout.top) * row / layout.n;
}

int rowLeft(const Layout &layout, int row) {
    return row == layout.n - 1 ? ToggleWidth : 0;
}

void drawToggle(lgfx::LGFXBase &display) {
    constexpr int cx = ToggleWidth / 2 - 2;
    const int cy = displayHeight() - 11;
    const auto bg = theme::palette().background;
    const auto edge = theme::blend(theme::readableInk(theme::palette().dim, bg), bg, 150);
    display.fillRect(cx - 7, cy - 7, 15, 15, bg);
    display.drawFastHLine(cx - 5, cy + 5, 11, edge);
    const int point = dockVisible ? cy + 2 : cy - 3;
    const int ends = dockVisible ? cy - 2 : cy + 1;
    display.drawLine(cx - 4, ends, cx, point, edge);
    display.drawLine(cx, point, cx + 4, ends, edge);
}

} // namespace

void loadSettings() {
#ifdef GRIM_DEVICE
    nvs_handle_t settings;
    if (nvs_open(SettingsNamespace, NVS_READONLY, &settings) != ESP_OK) return;
    uint8_t value;
    if (nvs_get_u8(settings, OrientationKey, &value) == ESP_OK && value <= static_cast<uint8_t>(Orientation::Portrait180)) {
        currentOrientation = static_cast<Orientation>(value);
    }
    if (nvs_get_u8(settings, BackdropKey, &value) == ESP_OK && value <= static_cast<uint8_t>(MenuBackdrop::Plasma)) {
        currentBackdrop = static_cast<MenuBackdrop>(value);
    }
    if (!loadPalette(settings)) {
        if (nvs_get_u8(settings, LegacyColourKey, &value) == ESP_OK &&
            (value == static_cast<uint8_t>(ColourScheme::Ember) || value == static_cast<uint8_t>(ColourScheme::Neon))) {
            theme::setColourScheme(static_cast<ColourScheme>(value));
        }
    }
    nvs_close(settings);
#endif
}

Orientation orientation() { return currentOrientation; }
bool isPortrait() {
    return currentOrientation == Orientation::Portrait || currentOrientation == Orientation::Portrait180;
}
MenuBackdrop menuBackdrop() { return currentBackdrop; }
void openCarouselSettings() {
    if (!carouselMain()) return;
    dockBeforeSettings = dockVisible;
    dockVisible = false;
    settingsPage = SettingsPage::Home;
    show(Panel::Main);
}
void backCarouselSettings() {
    if (settingsPage == SettingsPage::Home) {
        dockVisible = dockBeforeSettings;
        settingsPage = SettingsPage::Closed;
        dirty = true;
    } else if (settingsPage != SettingsPage::Closed) {
        settingsPage = SettingsPage::Home;
    }
}
bool carouselSettingsVisible() { return settingsPage != SettingsPage::Closed; }

void drawCarouselSettings(int transparent) {
    using namespace theme;
    const bool home = settingsPage == SettingsPage::Home;
    const bool themes = settingsPage == SettingsPage::Theme;
    header(home ? "SETTINGS" : themes ? "THEME" : "DISPLAY", home ? "settings" : themes ? "theme" : "display", transparent);
    auto &cv = hal::canvas();
    cv.fillRect(SettingsBack.x, 0, SettingsBack.w, 17, C_DUSK);
    textRight(home ? "< Grimoire" : "< Settings", 233, 5, readableInk(C_BONE, C_DUSK));

    if (home) {
        const auto level = hal::volume();
        char percent[6];
        snprintf(percent, sizeof percent, "%d%%", (level * 100 + 127) / 255);
        settingRow({14, 27, 212, 22}, "Volume");
        cv.drawFastHLine(68, 42, 56, C_CLOT);
        cv.drawFastHLine(68, 42, level * 56 / 255, C_EMBER);
        settingChoice(VolumeMinus, "-");
        settingChoice(VolumePlus, "+");
        textCentre(percent, 180, 34, readableInk(C_GLOW, C_DUSK));
        settingRow(SettingsTheme, "Theme", colourSchemeName(colourScheme()));
        settingRow(SettingsDisplay, "Display", OrientationNames[static_cast<uint8_t>(currentOrientation)]);
        settingRow(SettingsPalette, "Palette", "Edit");
    } else if (themes) {
        for (size_t i = 0; i < PresetCount; ++i) {
            const auto scheme = presetScheme(i);
            settingChoice(themeChoice(i), colourSchemeName(scheme), scheme == colourScheme(), true, presetPalette(scheme).ember);
        }
        if (hasCustomColourway()) settingChoice(CustomChoice, "Custom", colourScheme() == ColourScheme::Custom);
    } else {
        const auto rotation = static_cast<uint8_t>(currentOrientation);
        text("Orientation", 14, 25, C_BONE);
        settingChoice(orientationChoice(0), "Landscape", !(rotation & 1));
        settingChoice(orientationChoice(1), "Portrait", rotation & 1);
        settingChoice(TurnChoice, "Flip 180", rotation & 2);
        text("Backdrop", 14, 73, C_BONE);
        for (uint8_t i = 0; i < 4; ++i) {
            settingChoice(backdropChoice(i), BackdropNames[i], i == static_cast<uint8_t>(currentBackdrop));
        }
    }
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
    show(Panel::Main);
    if (settingsPage != SettingsPage::Closed) {
        settingsPage = SettingsPage::Closed;
        dockVisible = dockBeforeSettings;
    }
    if (focusedRite(active)) dockVisible = dockBeforeFocusedRite;
    if (focusedRite(rite)) {
        dockBeforeFocusedRite = dockVisible;
        dockVisible = false;
    }
    splash = false;
    active = rite;
    immersive = false;
    textMode = TextMode::None;
    symbols = shift = false;
    tapX = tapY = -1;
    paletteContact = false;
    paletteStart = paletteMotion = {};
}

void setTextMode(TextMode mode) {
    if (textMode == mode) return;
    textMode = mode;
    symbols = shift = false;
    tapX = tapY = -1;
    const bool edit = mode == TextMode::Password || mode == TextMode::Url || mode == TextMode::Prompt;
    show(edit ? Panel::Keyboard : Panel::Main);
}

bool visionsImmersive() { return immersive; }

int displayWidth() { return isPortrait() ? ShortEdge : LongEdge; }
int displayHeight() { return isPortrait() ? LongEdge : ShortEdge; }
int viewportWidth() { return displayWidth(); }
int viewportHeight() { return immersive ? displayHeight() : viewportWidth() * hal::H / hal::W; }
int viewportLeft() { return 0; }
int viewportTop() {
    if (immersive) return 0;
    if (isPortrait() || !dockVisible || carouselMain() || active == &RITE_VISIONS) return (viewportBottom() - viewportHeight()) / 2;
    return keyboardVisible() && active == &RITE_BEAT ? keyboardTop() - viewportHeight() : 0;
}
int viewportBottom() {
    if (immersive || !dockVisible) return displayHeight();
    if (carouselMain()) return displayHeight() - CarouselDockHeight;
    if (active == &RITE_VISIONS && panel != Panel::Volume) {
        return displayHeight() - (panel == Panel::VisionsEffects ? 58 : panel == Panel::VisionsOptions ? 56 : 30);
    }
    return keyboardVisible() ? keyboardTop() : dockTop();
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
    if (x < 0 || x >= displayWidth() || y < 0 || y >= displayHeight()) return false;

    if (immersive) {
        if (!contact) {
            contact = true;
            immersive = false;
            dirty = true;
        }
        return false;
    }

    if (!carouselSettingsVisible() && active != &RITE_PALETTE && !contact &&
        x < ToggleWidth && y >= displayHeight() - ToggleHeight) {
        contact = true;
        dockVisible = !dockVisible;
        if (!dockVisible && active == &RITE_VISIONS) show(Panel::Main);
        dirty = true;
        return false;
    }

    if (!contact && y < viewportBottom()) {
        contact = true;
        const int sceneY = y - viewportTop();
        if (sceneY < 0 || sceneY >= viewportHeight()) return false;
        const Point p = viewportPoint(x, y);
        if (active == &RITE_PALETTE) {
            if (p.x >= 175 && p.y < 20) {
                out = {0, hal::N_ESC};
                return true;
            }
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
    const int col = x >= left && line.n ? ((x - left + 1) * line.n - 1) / (displayWidth() - left) : -1;

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
    if (immersive || carouselSettingsVisible() || active == &RITE_PALETTE) return;
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
    const auto &colours = theme::palette();
    display.fillRect(0, layout.top, displayWidth(), displayHeight() - layout.top, colours.background);
    display.drawFastHLine(0, layout.top, displayWidth(), colours.clot);
    display.setFont(&fonts::Font0);
    display.setTextDatum(textdatum_t::middle_center);

    char level[8];
    snprintf(level, sizeof level, "%d%%", percent);
    const uint16_t pressedBackground = theme::blend(colours.stone, colours.clot, 128);
    for (int row = 0; row < layout.n; ++row) {
        const Row &line = layout.rows[row];
        const int left = rowLeft(layout, row);
        const int width = displayWidth() - left;
        const int y = rowEdge(layout, row) + 1;
        const int h = rowEdge(layout, row + 1) - y - 1;
        for (int col = 0; col < line.n; ++col) {
            const Button &button = line.buttons[col];
            const int x = left + width * col / line.n + 2;
            const int w = left + width * (col + 1) / line.n - x - 2;
            const bool pressed = heldRow == row && heldCol == col;
            const bool selected = (panel == Panel::VisionsEffects && button.key.ch == '1' + visionsChoice()) ||
                                  (panel == Panel::VisionsOptions && button.key.ch == 'l' && visionsAreLayered());
            const bool primary = button.key.nav == hal::N_ENTER || selected;
            const uint16_t bg = pressed ? pressedBackground : primary ? colours.clot :
                                theme::blend(colours.dusk, colours.background, 190);
            display.fillRect(x, y, w, h, bg);
            display.drawFastHLine(x, y + h - 1, w, (primary || pressed) ? colours.rust : colours.clot);
            const char ch[] = {button.key.ch, 0};
            const char *label = button.action == Action::Level ? level : (button.label ? button.label : ch);
            const uint16_t ink = (primary || pressed) ? colours.glow : colours.bone;
            display.setTextColor(theme::readableInk(ink, bg));
            const bool compact = active == &RITE_VISIONS || carouselMain();
            display.setTextSize(!compact && strlen(label) * 12 + 4 <= static_cast<size_t>(w) ? 2 : 1);
            const bool pair = panel == Panel::VisionsEffects && visionsAreLayered() && row == 0;
            display.drawString(label, x + w / 2, y + h / 2 - (pair ? 5 : 0));
            if (pair) display.drawString(VisionsOverlays[col], x + w / 2, y + h / 2 + 5);
        }
    }
    drawToggle(display);
}

} // namespace cyd_controls
