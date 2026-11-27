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

constexpr uint16_t Error = pixelRgb(0xffb477);

struct Tab {
    const char *name;
    int width;
};

constexpr Tab Tabs[] = {{"Background", 68}, {"Panel", 43}, {"Text", 36}, {"Accent", 50}, {"Style", 35}};
constexpr int TabCount = sizeof Tabs / sizeof Tabs[0];
constexpr int StyleTab = TabCount - 1;
constexpr int TabsX = 4, TabsY = 21, TabsH = 21;

struct Track {
    const char *label;
    int left;
    int minimum, maximum;
};

constexpr Track Tracks[] = {{"R", 17, 0, 255}, {"G", 17, 0, 255}, {"B", 17, 0, 255},
                            {"Saturation", 66, 0, 200}, {"Brightness", 66, -100, 100}};
constexpr int TrackCount = sizeof Tracks / sizeof Tracks[0];
constexpr int TracksY = 43, RowHeight = 14, TrackRight = 113, ValueRight = 146;
constexpr int Saturation = 3, Brightness = 4;

constexpr const char *Styles[] = {"Flat", "Gradient", "Grid", "Scanlines", "Dither",
                                  "Diagonal", "Checker", "Diamonds", "Waves"};
constexpr int StyleCount = sizeof Styles / sizeof Styles[0];
static_assert(StyleCount == static_cast<int>(BackgroundStyle::Waves) + 1);
constexpr int OptionsX = 3, OptionsW = 144;
constexpr int StyleColumnW = OptionsW / 2;
constexpr int StyleRows = (StyleCount + 1) / 2;

constexpr const char *StopLabels[] = {"BG", "Panel", "Text", "Acc"};
constexpr int StopW = OptionsW / StyleTab, StopH = 17, StartY = 69, EndY = 96;
static_assert(sizeof StopLabels / sizeof StopLabels[0] == StyleTab);

constexpr const char *GradientModes[] = {"Blend >", "Multitone >", "Bands >"};
constexpr int GradientModeCount = sizeof GradientModes / sizeof GradientModes[0];
static_assert(GradientModeCount == static_cast<int>(GradientMode::Bands) + 1);

struct Action {
    const char *label;
    char key;
};
constexpr Action Actions[] = {{"Ember", 'e'}, {"Neon", 'n'}, {"Hue", 'g'},
                              {"Multi", 'm'}, {"Bands", 'h'}, {"Save", 's'}};
constexpr int ActionCount = sizeof Actions / sizeof Actions[0];
constexpr int ActionWidth = hal::W / ActionCount;
constexpr int ActionsY = 116;
static_assert(ActionWidth * ActionCount == hal::W);

constexpr int PreviewX = 152, PreviewY = 53, PreviewW = 84;
constexpr int StepsY = 99, StepWidth = 17;

Colourway draft, saved;
theme::Palette preview;
struct Surface {
    uint16_t fill, text, edge, focus;
};
Surface background, panel;

Surface editorSurface(uint16_t fill) {
    const uint16_t ink = theme::readableInk(preview.bone, fill);
    return {fill, ink, theme::blend(ink, fill, 128), theme::readableInk(preview.ember, fill)};
}

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
    background = editorSurface(preview.background);
    panel = editorSurface(preview.dusk);
    saveFailed = false;
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
            if (!saveFailed) saved = draft;
        }
        break;
    default:
        break;
    }
    return true;
}

bool pointer(int x, int y, bool start) {
    if (start) {
        draggedTrack = -1;
        if (y >= ActionsY) {
            key(hal::Key{Actions[x / ActionWidth].key});
            return false;
        }
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
        if (y >= StepsY && x >= PreviewX && x < PreviewX + PreviewW) {
            if (x < PreviewX + StepWidth) return setValue(focusedTrack, value(focusedTrack) - 1);
            if (x >= PreviewX + PreviewW - StepWidth) return setValue(focusedTrack, value(focusedTrack) + 1);
            return false;
        }
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

void drawTabs(Canvas &cv) {
    int x = TabsX;
    for (int i = 0; i < TabCount; ++i) {
        const Tab &item = Tabs[i];
        const bool selected = i == tab;
        const Surface &surface = selected ? panel : background;
        const int width = item.width - 2;
        if (selected) cv.fillRect(x, TabsY, width, TabsH - 1, panel.fill);
        const int bottom = TabsY + TabsH - 1;
        cv.drawFastHLine(x, bottom, width, selected ? panel.focus : surface.edge);
        if (selected) theme::diamond(x + width / 2, bottom, 1, panel.focus);
        theme::textCentre(item.name, x + width / 2, TabsY + (i == StyleTab ? 6 : 3), surface.text);
        if (i != StyleTab) {
            cv.fillRect(x + 5, TabsY + 13, width - 10, 4, pixelRgb(draft.rgb[i]));
            cv.drawRect(x + 4, TabsY + 12, width - 8, 6, surface.edge);
        }
        x += item.width;
    }
}

void drawStyles(Canvas &cv) {
    for (int i = 0; i < StyleCount; ++i) {
        const int x = OptionsX + (i % 2) * StyleColumnW;
        const int y = TracksY + (i / 2) * RowHeight;
        const bool selected = static_cast<BackgroundStyle>(i) == draft.backgroundStyle;
        const Surface &surface = selected ? panel : background;
        if (selected) cv.fillRect(x, y, StyleColumnW - 2, RowHeight - 1, panel.fill);
        theme::diamond(x + 5, y + 6, 2, selected ? surface.focus : surface.edge, selected);
        theme::text(Styles[i], x + 11, y + 3, surface.text);
    }
}

void drawStops(Canvas &cv, const char *title, ColourRole selected, int y, const uint16_t *colours) {
    const int role = static_cast<int>(selected);
    theme::text(title, OptionsX, y - 9, background.text);
    theme::textRight(Tabs[role].name, OptionsX + OptionsW - 2, y - 9, background.text);
    for (int i = 0; i < StyleTab; ++i) {
        const int x = OptionsX + i * StopW;
        const int width = StopW - 2;
        const bool chosen = i == role;
        const Surface &surface = chosen ? panel : background;
        if (chosen) cv.fillRect(x, y, width, StopH, surface.fill);
        cv.drawFastHLine(x, y + StopH - 1, width, chosen ? surface.focus : surface.edge);
        theme::textCentre(StopLabels[i], x + width / 2, y + 1, surface.text);
        cv.fillRect(x + 4, y + 10, width - 8, 4, colours[i]);
        cv.drawRect(x + 3, y + 9, width - 6, 6, surface.edge);
    }
}

void drawGradientDetails(Canvas &cv) {
    const int modeX = OptionsX + StyleColumnW;
    const int width = StyleColumnW - 2;
    const int height = RowHeight - 1;
    cv.fillRect(OptionsX, TracksY, width, height, panel.fill);
    cv.drawFastHLine(OptionsX, TracksY + height - 1, width, panel.edge);
    theme::textCentre("< Styles", OptionsX + width / 2, TracksY + 3, panel.text);
    cv.fillRect(modeX, TracksY, width, height, panel.fill);
    cv.drawFastHLine(modeX, TracksY + height - 1, width, panel.edge);
    theme::textCentre(GradientModes[static_cast<int>(draft.gradientMode)], modeX + width / 2, TracksY + 3, panel.text);
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
    const Surface &surface = selected ? panel : background;
    if (selected) cv.fillRect(3, y, 144, RowHeight - 1, panel.fill);
    theme::text(track.label, 4, y + 3, surface.text);

    if (i < Saturation) {
        for (int x = track.left; x <= TrackRight; x += 2) {
            const uint32_t channel = (x - track.left) * 255 / span;
            cv.fillRect(x, y + 5, x == TrackRight ? 1 : 2, 4, pixelRgb(channel << componentShift(i)));
        }
    } else {
        cv.drawFastHLine(track.left, y + 6, span + 1, surface.edge);
        cv.drawFastVLine(track.left + span / 2, y + 4, 6, surface.text);
    }
    if (i < Saturation) cv.drawRect(track.left - 1, y + 4, span + 3, 6, surface.edge);
    const int knob = track.left + ((v - track.minimum) * span + (track.maximum - track.minimum) / 2) /
                                 (track.maximum - track.minimum);
    theme::diamond(knob, y + 7, 3, surface.fill);
    theme::diamond(knob, y + 7, 2, selected ? surface.focus : surface.text);

    char label[8];
    const char *format = i == Saturation ? "%d%%" : i == Brightness ? "%+d" : "%d";
    snprintf(label, sizeof label, format, v);
    theme::textRight(label, ValueRight, y + 3, surface.text);
}

void drawPreview(Canvas &cv) {
    constexpr int x = PreviewX, y = PreviewY, w = PreviewW;
    const bool showStops = tab == StyleTab && gradientDetails;
    const bool middleStop = showStops && draft.gradientMode != GradientMode::Blend;
    const int h = tab != StyleTab ? 43 : middleStop ? 49 : 59;
    const char *label = saveFailed ? "Save failed" : showStops ? "Stop colours" : "Preview";
    theme::text(label, x, TracksY, saveFailed ? theme::readableInk(Error, background.fill) : background.text);
    if (showStops) theme::drawBackground(cv, x, y, w, h, preview);
    else theme::drawSceneBackground(cv, x, y, w, h, preview);
    theme::frame(x, y, w, h, background.edge, background.focus);
    if (showStops) {
        if (middleStop) theme::textCentre("Via Panel", x + w / 2, y + h + 3, background.text);
        return;
    }
    theme::glyphs(DAEDRIC_S, "D", x + 5, y + 3, preview.ember);
    theme::text("diviner", x + 23, y + 8, preview.bone);
    cv.drawFastHLine(x + 4, y + 19, w - 8, preview.clot);
    theme::diamond(x + w / 2, y + 19, 1, preview.rust);
    cv.fillRect(x + 8, y + 23, w - 16, 12, preview.dusk);
    theme::frame(x + 8, y + 23, w - 16, 12, preview.rust, preview.ember);
    theme::textCentre("rite", x + w / 2, y + 25, preview.glow);
    for (int i = 0; i < w - 10; ++i) {
        const auto index = i * 255 / (w - 11);
        cv.drawFastVLine(x + 5 + i, y + h - 5, 3, preview.intensity[index]);
    }

    if (tab == StyleTab) return;
    const int plusX = x + w - StepWidth;
    cv.fillRect(x, StepsY + 2, StepWidth, 12, panel.fill);
    cv.fillRect(plusX, StepsY + 2, StepWidth, 12, panel.fill);
    cv.drawFastHLine(x, StepsY + 13, StepWidth, panel.edge);
    cv.drawFastHLine(plusX, StepsY + 13, StepWidth, panel.edge);
    theme::textCentre("-", x + StepWidth / 2, StepsY + 4, panel.text);
    theme::textCentre("+", plusX + StepWidth / 2, StepsY + 4, panel.text);
    char hex[8];
    snprintf(hex, sizeof hex, "#%06lX", static_cast<unsigned long>(draft.rgb[tab] & 0xffffffu));
    theme::textCentre(hex, x + w / 2, StepsY + 4, background.text);
}

void drawHeader(Canvas &cv, bool pending) {
    const int runeWidth = theme::glyphs(DAEDRIC_S, "PALETTE", 4, 1, background.focus);
    theme::text("palette", 4 + runeWidth + 6, 5, background.text);
    if (pending) theme::textRight("unsaved", 167, 5, background.text);
    else if (theme::colourScheme() == ColourScheme::Custom) theme::textRight("saved", 167, 5, background.text);
    theme::textRight(pending ? "< Discard" : "< Settings", 236, 5, background.focus);
    cv.drawFastHLine(0, 18, hal::W, background.edge);
    theme::diamond(hal::W / 2, 18, 2, background.focus);
}

void drawActions(Canvas &cv) {
    cv.drawFastHLine(0, ActionsY - 1, hal::W, background.edge);
    for (int i = 0; i < ActionCount; ++i) {
        const int x = i * ActionWidth + 2, y = ActionsY + 2, w = ActionWidth - 4;
        constexpr int h = 14;
        const bool save = Actions[i].key == 's';
        cv.fillRect(x, y, w, h, panel.fill);
        if (save) theme::frame(x, y, w, h, panel.edge, panel.focus);
        else cv.drawFastHLine(x, y + h - 1, w, panel.edge);
        theme::textCentre(Actions[i].label, x + w / 2, y + 3, save ? panel.focus : panel.text);
    }
}

void draw(uint32_t) {
    int x, y;
    bool start, update = false;
    while (cyd_controls::takePalettePointer(x, y, start)) update = pointer(x, y, start) || update;
    if (update) refresh();

    Canvas &cv = hal::canvas();
    cv.fillScreen(background.fill);
    drawHeader(cv, changed());

    drawTabs(cv);
    if (tab == StyleTab) {
        if (gradientDetails) drawGradientDetails(cv);
        else drawStyles(cv);
    } else {
        for (int i = 0; i < TrackCount; ++i) drawTrack(cv, i);
    }
    drawPreview(cv);
    drawActions(cv);
}

} // namespace

const Rite RITE_PALETTE = {"PALETTE", "palette", "design a colourway", enter, leave, key, draw};

#endif
