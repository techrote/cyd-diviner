#ifdef GRIM_CYD

#include "../cyd_controls.h"
#include "../rite.h"
#include "../theme.h"

#include <algorithm>
#include <cstdlib>
#include <stdio.h>

namespace {

using theme::BackgroundStyle;
using theme::ColourRole;
using theme::ColourScheme;
using theme::Colourway;
using theme::GradientMode;

constexpr uint16_t pixelRgb(uint32_t rgb) {
    return ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f);
}

constexpr uint16_t Background = pixelRgb(0x101819);
constexpr uint16_t Panel = pixelRgb(0x1b292a);
constexpr uint16_t Border = pixelRgb(0x49605f);
constexpr uint16_t Text = pixelRgb(0xe6eeea);
constexpr uint16_t Subtle = pixelRgb(0xb3c3be);
constexpr uint16_t Focus = pixelRgb(0x80d6ad);
constexpr uint16_t Error = pixelRgb(0xffb477);

struct Tab {
    const char *name;
    int width;
};

constexpr Tab Tabs[] = {{"Background", 68}, {"Panel", 43}, {"Text", 36}, {"Accent", 50}, {"Style", 35}};
constexpr int TabCount = sizeof Tabs / sizeof Tabs[0];
constexpr int StyleTab = TabCount - 1;
constexpr int TabsX = 4, TabsY = 21, TabsH = 22;

struct Track {
    const char *label;
    int left;
    int minimum, maximum;
};

constexpr Track Tracks[] = {{"R", 17, 0, 255}, {"G", 17, 0, 255}, {"B", 17, 0, 255},
                            {"Saturation", 66, 0, 200}, {"Brightness", 66, -100, 100}};
constexpr int TrackCount = sizeof Tracks / sizeof Tracks[0];
constexpr int TracksY = 46, RowHeight = 16, TrackRight = 113, ValueRight = 146;
constexpr int Saturation = 3, Brightness = 4;

constexpr const char *Styles[] = {"Flat", "Gradient", "Grid", "Scanlines", "Dither",
                                  "Diagonal", "Checker", "Diamonds", "Waves"};
constexpr int StyleCount = sizeof Styles / sizeof Styles[0];
static_assert(StyleCount == static_cast<int>(BackgroundStyle::Waves) + 1);
constexpr int OptionsX = 3, OptionsW = 144;
constexpr int StyleColumnW = OptionsW / 2;
constexpr int StyleRows = (StyleCount + 1) / 2;

constexpr const char *StopLabels[] = {"BG", "Panel", "Text", "Acc"};
constexpr int StopW = OptionsW / StyleTab, StopH = 21, StartY = 74, EndY = 105;
static_assert(sizeof StopLabels / sizeof StopLabels[0] == StyleTab);

constexpr const char *GradientModes[] = {"Blend >", "Multitone >", "Bands >"};
constexpr int GradientModeCount = sizeof GradientModes / sizeof GradientModes[0];
static_assert(GradientModeCount == static_cast<int>(GradientMode::Bands) + 1);

Colourway draft, saved;
theme::Palette preview;
int tab = 0, focusedTrack = 0, draggedTrack = -1;
unsigned hue = 0;
bool saveFailed = false, gradientDetails = false;

bool sameColourway(const Colourway &a, const Colourway &b) {
    return a.rgb == b.rgb && a.saturation == b.saturation &&
           a.brightness == b.brightness && a.contrast == b.contrast &&
           a.backgroundStyle == b.backgroundStyle && a.gradientStart == b.gradientStart &&
           a.gradientEnd == b.gradientEnd && a.gradientMode == b.gradientMode;
}

bool changed() { return !sameColourway(draft, saved); }

void refresh() {
    theme::buildPalette(preview, draft);
    saveFailed = false;
    cyd_controls::setPaletteDirty(changed());
}

void enter() {
    draft = saved = theme::presetColourway(theme::colourScheme());
    tab = static_cast<int>(ColourRole::Background);
    focusedTrack = 0;
    draggedTrack = -1;
    gradientDetails = false;
    hue = hal::ms() % 360;
    refresh();
}

void leave() {} // The draft never replaces the active palette until Save succeeds.

int componentShift(int track) { return (2 - track) * 8; }

int value(int track) {
    if (track < Saturation) return (draft.rgb[tab] >> componentShift(track)) & 0xff;
    return track == Saturation ? draft.saturation : draft.brightness;
}

uint32_t replaceComponent(uint32_t colour, int track, int component) {
    const int shift = componentShift(track);
    return (colour & ~(UINT32_C(0xff) << shift)) | (static_cast<uint32_t>(component) << shift);
}

bool setValue(int track, int next) {
    next = std::clamp(next, Tracks[track].minimum, Tracks[track].maximum);
    if (value(track) == next) return false;
    if (track < Saturation) draft.rgb[tab] = replaceComponent(draft.rgb[tab], track, next);
    else if (track == Saturation) draft.saturation = static_cast<uint8_t>(next);
    else draft.brightness = static_cast<int8_t>(next);
    return true;
}

bool setStyle(int next) {
    const auto style = static_cast<BackgroundStyle>(next);
    gradientDetails = style == BackgroundStyle::Gradient;
    if (style == draft.backgroundStyle) return false;
    draft.backgroundStyle = style;
    return true;
}

void replaceColours(Colourway colours) {
    colours.backgroundStyle = draft.backgroundStyle;
    colours.gradientStart = draft.gradientStart;
    colours.gradientEnd = draft.gradientEnd;
    colours.gradientMode = draft.gradientMode;
    draft = colours;
    refresh();
}

uint16_t nextHue() {
    hue = (hue + 67 + std::rand() % 226) % 360;
    return static_cast<uint16_t>(hue);
}

bool setStop(ColourRole &stop, int offset) {
    const auto role = static_cast<ColourRole>(offset / StopW);
    if (stop == role) return false;
    stop = role;
    return true;
}

bool stylePointer(int x, int y) {
    const int offset = x - OptionsX;
    if (offset < 0 || offset >= OptionsW) return false;
    if (gradientDetails) {
        if (y >= TracksY && y < TracksY + RowHeight) {
            if (offset < StyleColumnW) {
                gradientDetails = false;
                return false;
            }
            const int next = (static_cast<int>(draft.gradientMode) + 1) % GradientModeCount;
            draft.gradientMode = static_cast<GradientMode>(next);
            return true;
        }
        if (y >= StartY && y < StartY + StopH) return setStop(draft.gradientStart, offset);
        if (y >= EndY && y < EndY + StopH) return setStop(draft.gradientEnd, offset);
        return false;
    }
    if (y < TracksY || y >= TracksY + StyleRows * RowHeight) return false;
    const int style = ((y - TracksY) / RowHeight) * 2 + offset / StyleColumnW;
    return style < StyleCount && setStyle(style);
}

bool pointer(int x, int y, bool start) {
    if (start) {
        draggedTrack = -1;
        if (y >= TabsY && y < TabsY + TabsH) {
            int left = TabsX;
            for (int i = 0; i < TabCount; ++i) {
                if (x >= left && x < left + Tabs[i].width) {
                    tab = i;
                    gradientDetails = false;
                    break;
                }
                left += Tabs[i].width;
            }
            return false;
        }
        if (tab == StyleTab) return stylePointer(x, y);
        if (y >= TracksY && y < TracksY + TrackCount * RowHeight) {
            const int hit = (y - TracksY) / RowHeight;
            if (x >= Tracks[hit].left - 5 && x <= TrackRight + 5) focusedTrack = draggedTrack = hit;
        }
    }
    if (tab == StyleTab || draggedTrack < 0) return false;
    const Track &track = Tracks[draggedTrack];
    const int width = TrackRight - track.left;
    const int offset = std::clamp(x, track.left, TrackRight) - track.left;
    const int next = track.minimum + (offset * (track.maximum - track.minimum) + width / 2) / width;
    return setValue(draggedTrack, next);
}

bool key(const hal::Key &k) {
    switch (k.nav) {
    case hal::N_LEFT:
    case hal::N_RIGHT:
    case hal::N_UP:
    case hal::N_DOWN: {
        const int step = k.nav == hal::N_LEFT || k.nav == hal::N_UP ? -1 : 1;
        if (tab == StyleTab) {
            const int current = static_cast<int>(draft.backgroundStyle);
            if (setStyle((current + StyleCount + step) % StyleCount)) refresh();
        } else if (k.nav == hal::N_UP || k.nav == hal::N_DOWN) {
            focusedTrack = (focusedTrack + TrackCount + step) % TrackCount;
        } else {
            if (setValue(focusedTrack, value(focusedTrack) + step)) refresh();
        }
        return true;
    }
    case hal::N_TAB:
        tab = (tab + 1) % TabCount;
        draggedTrack = -1;
        gradientDetails = false;
        return true;
    default:
        break;
    }

    switch (k.ch) {
    case 'e':
    case 'n':
        replaceColours(theme::presetColourway(k.ch == 'e' ? ColourScheme::Ember : ColourScheme::Neon));
        break;
    case 'g':
        replaceColours(theme::generateColourway(nextHue()));
        break;
    case 'm':
    case 'h':
        draft = theme::generateColourway(nextHue(), k.ch == 'm' ? GradientMode::Multitone : GradientMode::Bands);
        tab = StyleTab;
        gradientDetails = true;
        draggedTrack = -1;
        refresh();
        break;
    case 's':
        if (theme::colourScheme() != ColourScheme::Custom || changed()) {
            saveFailed = !cyd_controls::saveColourway(draft);
            if (!saveFailed) {
                saved = draft;
                cyd_controls::setPaletteDirty(false);
            }
        }
        break;
    default:
        break;
    }
    return true;
}

void drawTabs(Canvas &cv) {
    int x = TabsX;
    for (int i = 0; i < TabCount; ++i) {
        const Tab &item = Tabs[i];
        const bool selected = i == tab;
        const uint16_t edge = selected ? Focus : Border;
        const int width = item.width - 2;
        cv.fillRect(x, TabsY, width, TabsH, Panel);
        cv.drawRect(x, TabsY, width, TabsH, edge);
        theme::textCentre(item.name, x + width / 2, TabsY + (i == StyleTab ? 7 : 4), selected ? Text : Subtle);
        if (i != StyleTab) {
            cv.fillRect(x + 4, TabsY + 14, width - 8, 4, pixelRgb(draft.rgb[i]));
            cv.drawRect(x + 3, TabsY + 13, width - 6, 6, Border);
        }
        x += item.width;
    }
}

void drawStyles(Canvas &cv) {
    for (int i = 0; i < StyleCount; ++i) {
        const int x = OptionsX + (i % 2) * StyleColumnW;
        const int y = TracksY + (i / 2) * RowHeight;
        const bool selected = static_cast<BackgroundStyle>(i) == draft.backgroundStyle;
        cv.fillRect(x, y, StyleColumnW - 2, RowHeight - 1, Panel);
        cv.drawRect(x, y, StyleColumnW - 2, RowHeight - 1, selected ? Focus : Border);
        cv.drawCircle(x + 6, y + 7, 3, selected ? Focus : Subtle);
        if (selected) cv.fillCircle(x + 6, y + 7, 1, Focus);
        theme::text(Styles[i], x + 12, y + 4, selected ? Text : Subtle);
    }
}

void drawStops(Canvas &cv, const char *title, ColourRole selected, int y, const uint16_t *colours) {
    const int role = static_cast<int>(selected);
    theme::text(title, OptionsX, y - 10, Text);
    theme::textRight(Tabs[role].name, OptionsX + OptionsW - 2, y - 10, Subtle);
    for (int i = 0; i < StyleTab; ++i) {
        const int x = OptionsX + i * StopW;
        const int width = StopW - 2;
        cv.fillRect(x, y, width, StopH, Panel);
        cv.drawRect(x, y, width, StopH, i == role ? Focus : Border);
        theme::textCentre(StopLabels[i], x + width / 2, y + 3, i == role ? Text : Subtle);
        cv.fillRect(x + 4, y + 13, width - 8, 4, colours[i]);
        cv.drawRect(x + 3, y + 12, width - 6, 6, Border);
    }
}

void drawGradientDetails(Canvas &cv) {
    const int modeX = OptionsX + StyleColumnW;
    const int width = StyleColumnW - 2;
    const int height = RowHeight - 1;
    cv.fillRect(OptionsX, TracksY, width, height, Panel);
    cv.drawRect(OptionsX, TracksY, width, height, Border);
    theme::textCentre("< Styles", OptionsX + width / 2, TracksY + 4, Text);
    cv.fillRect(modeX, TracksY, width, height, Panel);
    cv.drawRect(modeX, TracksY, width, height, Border);
    theme::textCentre(GradientModes[static_cast<int>(draft.gradientMode)], modeX + width / 2, TracksY + 4, Text);
    const uint16_t colours[] = {preview.background, preview.dusk, preview.bone, preview.ember};
    drawStops(cv, "Start colour", draft.gradientStart, StartY, colours);
    drawStops(cv, "End colour", draft.gradientEnd, EndY, colours);
}

void drawTrack(Canvas &cv, int i) {
    const Track &track = Tracks[i];
    const int y = TracksY + i * RowHeight;
    const int span = TrackRight - track.left;
    const int v = value(i);
    const bool selected = i == focusedTrack;
    const uint16_t ink = selected ? Text : Subtle;
    if (selected) cv.fillRect(3, y, 144, RowHeight - 1, Panel);
    theme::text(track.label, 4, y + 4, ink);

    if (i < Saturation) {
        const uint32_t colour = draft.rgb[tab];
        for (int x = track.left; x <= TrackRight; x += 2) {
            const int channel = (x - track.left) * 255 / span;
            cv.fillRect(x, y + 5, x == TrackRight ? 1 : 2, 6, pixelRgb(replaceComponent(colour, i, channel)));
        }
    } else {
        cv.fillRect(track.left, y + 5, span + 1, 6, Border);
        cv.drawFastVLine(track.left + span / 2, y + 4, 8, Subtle);
    }
    cv.drawRect(track.left - 1, y + 4, span + 3, 8, Border);
    const int knob = track.left + ((v - track.minimum) * span + (track.maximum - track.minimum) / 2) /
                                 (track.maximum - track.minimum);
    cv.fillRect(knob - 2, y + 2, 5, 12, Background);
    cv.drawRect(knob - 2, y + 2, 5, 12, selected ? Focus : Text);

    char label[8];
    const char *format = i == Saturation ? "%d%%" : i == Brightness ? "%+d" : "%d";
    snprintf(label, sizeof label, format, v);
    theme::textRight(label, ValueRight, y + 4, ink);
}

void drawPreview(Canvas &cv) {
    constexpr int x = 152, y = 57, w = 84;
    const int h = tab == StyleTab ? 72 : 56;
    const bool showStops = tab == StyleTab && gradientDetails;
    theme::text(showStops ? "Stop colours" : "UI preview", x, 47, Text);
    if (showStops) theme::drawBackground(cv, x, y, w, h, preview);
    else theme::drawSceneBackground(cv, x, y, w, h, preview);
    cv.drawRect(x, y, w, h, Border);
    if (showStops) return;
    theme::glyphs(DAEDRIC_S, "D", x + 5, y + 3, preview.ember);
    theme::text("Diviner", x + 23, y + 8, preview.bone);
    cv.drawFastHLine(x + 4, y + 19, w - 8, preview.clot);
    cv.fillRect(x + 7, y + 24, w - 14, 16, preview.dusk);
    cv.drawRect(x + 7, y + 24, w - 14, 16, preview.rust);
    theme::textCentre("Open", x + w / 2, y + 28, preview.glow);
    for (int i = 0; i < w - 10; ++i) {
        const auto index = i * 255 / (w - 11);
        cv.drawFastVLine(x + 5 + i, y + h - 11, 6, preview.intensity[index]);
    }

    if (tab == StyleTab) return;
    const uint32_t rgb = draft.rgb[tab];
    cv.fillRect(x + 3, 119, 21, 10, pixelRgb(rgb));
    cv.drawRect(x + 3, 119, 21, 10, Border);
    char hex[8];
    snprintf(hex, sizeof hex, "#%06lX", static_cast<unsigned long>(rgb & 0xffffffu));
    theme::textRight(hex, x + w - 2, 120, Text);
}

void draw(uint32_t) {
    int x, y;
    bool start, update = false;
    while (cyd_controls::takePalettePointer(x, y, start)) update = pointer(x, y, start) || update;
    if (update) refresh();

    Canvas &cv = hal::canvas();
    cv.fillScreen(Background);
    theme::text("Palette", 4, 2, Text, 2);
    if (tab == StyleTab && gradientDetails && draft.gradientMode != GradientMode::Blend) {
        theme::text("Via Panel", 92, 6, Subtle);
    }
    const char *status = saveFailed ? "Save failed" : changed() ? "Unsaved" :
                         theme::colourScheme() == ColourScheme::Custom ? "Saved" : "Preview";
    theme::textRight(status, 236, 6, saveFailed ? Error : Text);
    cv.drawFastHLine(0, 18, hal::W, Border);

    drawTabs(cv);
    if (tab == StyleTab) {
        if (gradientDetails) drawGradientDetails(cv);
        else drawStyles(cv);
    } else {
        for (int i = 0; i < TrackCount; ++i) drawTrack(cv, i);
    }
    drawPreview(cv);
}

} // namespace

const Rite RITE_PALETTE = {"PALETTE", "palette", "design a colourway", enter, leave, key, draw};

#endif
