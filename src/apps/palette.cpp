#ifdef GRIM_CYD

#include "../cyd_controls.h"
#include "../cyd_ui.h"
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
constexpr int StopH = 17, StartY = 69, EndY = 96;
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

constexpr int PortraitTabsY = 24, PortraitTabsH = 30;
constexpr int PortraitBodyY = 132, PortraitRowH = 24, PortraitTrackRight = 168;
constexpr int PortraitValueX = 206, PortraitMinusX = 175, PortraitPlusX = 218, PortraitStepW = 18;
constexpr int PortraitOptionsX = 12, PortraitOptionsW = 216;
constexpr int PortraitStartY = 176, PortraitEndY = 220, PortraitStopH = 26;
constexpr int PortraitPresetY = 260, PortraitGenerateY = 291, PortraitActionH = 28;
constexpr int PortraitActionsPerRow = ActionCount / 2, PortraitActionW = hal::W / PortraitActionsPerRow;
static_assert(PortraitActionsPerRow * 2 == ActionCount && PortraitActionsPerRow * PortraitActionW == hal::W);
constexpr int PortraitPreviewX = 12, PortraitPreviewY = 74, PortraitPreviewW = 216, PortraitPreviewH = 49;

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

bool setStop(ColourRole &stop, int index) {
    const auto role = static_cast<ColourRole>(index);
    if (stop == role) return false;
    stop = role;
    return true;
}

bool stylePointer(int x, int y, bool portrait) {
    const int optionsX = portrait ? PortraitOptionsX : OptionsX;
    const int width = portrait ? PortraitOptionsW : OptionsW;
    const int top = portrait ? PortraitBodyY : TracksY;
    const int rowHeight = portrait ? PortraitRowH : RowHeight;
    const int start = portrait ? PortraitStartY : StartY, end = portrait ? PortraitEndY : EndY;
    const int stopHeight = portrait ? PortraitStopH : StopH;
    const int offset = x - optionsX;
    if (offset < 0 || offset >= width) return false;
    if (gradientDetails) {
        if (y >= top && y < top + rowHeight) {
            if (offset < width / 2) {
                gradientDetails = false;
                return false;
            }
            const int next = (static_cast<int>(draft.gradientMode) + 1) % GradientModeCount;
            draft.gradientMode = static_cast<GradientMode>(next);
            return true;
        }
        if (y >= start && y < start + stopHeight) return setStop(draft.gradientStart, offset / (width / StyleTab));
        if (y >= end && y < end + stopHeight) return setStop(draft.gradientEnd, offset / (width / StyleTab));
        return false;
    }
    if (y < top || y >= top + StyleRows * rowHeight) return false;
    const int style = ((y - top) / rowHeight) * 2 + offset / (width / 2);
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
    const bool portrait = cyd_controls::isPortrait();
    const int tabsY = portrait ? PortraitTabsY : TabsY;
    const int tabsH = portrait ? PortraitTabsH : TabsH;
    const int trackY = portrait ? PortraitBodyY : TracksY;
    const int trackH = portrait ? PortraitRowH : RowHeight;
    const int trackRight = portrait ? PortraitTrackRight : TrackRight;
    if (start) {
        draggedTrack = -1;
        if (portrait) {
            if (y >= PortraitPresetY && y < PortraitPresetY + PortraitActionH) {
                key(hal::Key{Actions[x / PortraitActionW].key});
                return false;
            }
            if (y >= PortraitGenerateY && y < PortraitGenerateY + PortraitActionH) {
                key(hal::Key{Actions[PortraitActionsPerRow + x / PortraitActionW].key});
                return false;
            }
        } else if (y >= ActionsY) {
            key(hal::Key{Actions[x / ActionWidth].key});
            return false;
        }
        if (y >= tabsY && y < tabsY + tabsH) {
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
        if (tab == StyleTab) return stylePointer(x, y, portrait);
        if (!portrait && y >= StepsY && x >= PreviewX && x < PreviewX + PreviewW) {
            if (x < PreviewX + StepWidth) return setValue(focusedTrack, value(focusedTrack) - 1);
            if (x >= PreviewX + PreviewW - StepWidth) return setValue(focusedTrack, value(focusedTrack) + 1);
            return false;
        }
        if (y >= trackY && y < trackY + TrackCount * trackH) {
            const int hit = (y - trackY) / trackH;
            if (x >= Tracks[hit].left - 5 && x <= trackRight + 5) focusedTrack = draggedTrack = hit;
            else if (portrait && x >= PortraitMinusX && x < PortraitPlusX + PortraitStepW) {
                const bool stepping = focusedTrack == hit;
                focusedTrack = hit;
                if (stepping && x < PortraitMinusX + PortraitStepW) return setValue(hit, value(hit) - 1);
                if (stepping && x >= PortraitPlusX) return setValue(hit, value(hit) + 1);
            }
        }
    }
    if (tab == StyleTab || draggedTrack < 0) return false;
    const Track &track = Tracks[draggedTrack];
    const int width = trackRight - track.left;
    const int offset = std::clamp(x, track.left, trackRight) - track.left;
    const int next = track.minimum + (offset * (track.maximum - track.minimum) + width / 2) / width;
    return setValue(draggedTrack, next);
}

void drawTabs(cyd_ui::Surface &cv, int y, int height) {
    int x = TabsX;
    const int contentY = y + (height - TabsH) / 2;
    for (int i = 0; i < TabCount; ++i) {
        const Tab &item = Tabs[i];
        const bool selected = i == tab;
        const Surface &surface = selected ? panel : background;
        const int width = item.width - 2;
        if (selected) cv.fillRect(x, y, width, height - 1, panel.fill);
        const int bottom = y + height - 1;
        cv.drawFastHLine(x, bottom, width, selected ? panel.focus : surface.edge);
        if (selected) cv.diamond(x + width / 2, bottom, 1, panel.focus);
        cv.textCentre(item.name, x + width / 2, contentY + (i == StyleTab ? 6 : 3), surface.text);
        if (i != StyleTab) {
            cv.fillRect(x + 5, contentY + 13, width - 10, 4, pixelRgb(draft.rgb[i]));
            cv.drawRect(x + 4, contentY + 12, width - 8, 6, surface.edge);
        }
        x += item.width;
    }
}

void drawStyles(cyd_ui::Surface &cv, bool portrait) {
    const int optionsX = portrait ? PortraitOptionsX : OptionsX;
    const int top = portrait ? PortraitBodyY : TracksY;
    const int rowHeight = portrait ? PortraitRowH : RowHeight;
    const int columnWidth = portrait ? PortraitOptionsW / 2 : StyleColumnW;
    for (int i = 0; i < StyleCount; ++i) {
        const int x = optionsX + (i % 2) * columnWidth;
        const int y = top + (i / 2) * rowHeight;
        const int offset = (rowHeight - RowHeight) / 2;
        const bool selected = static_cast<BackgroundStyle>(i) == draft.backgroundStyle;
        const Surface &surface = selected ? panel : background;
        if (selected) cv.fillRect(x, y, columnWidth - 2, rowHeight - 1, panel.fill);
        cv.diamond(x + 5, y + 6 + offset, 2, selected ? surface.focus : surface.edge, selected);
        cv.text(Styles[i], x + 11, y + 3 + offset, surface.text);
    }
}

void drawStops(cyd_ui::Surface &cv, const char *title, ColourRole selected, int y,
               const uint16_t *colours, bool portrait) {
    const int optionsX = portrait ? PortraitOptionsX : OptionsX;
    const int optionsW = portrait ? PortraitOptionsW : OptionsW;
    const int height = portrait ? PortraitStopH : StopH;
    const int contentY = y + (height - StopH) / 2;
    const int stopW = optionsW / StyleTab;
    const int role = static_cast<int>(selected);
    cv.text(title, optionsX, y - 9, background.text);
    cv.textRight(Tabs[role].name, optionsX + optionsW - 2, y - 9, background.text);
    for (int i = 0; i < StyleTab; ++i) {
        const int x = optionsX + i * stopW;
        const int width = stopW - 2;
        const bool chosen = i == role;
        const Surface &surface = chosen ? panel : background;
        if (chosen) cv.fillRect(x, y, width, height, surface.fill);
        cv.drawFastHLine(x, y + height - 1, width, chosen ? surface.focus : surface.edge);
        cv.textCentre(StopLabels[i], x + width / 2, contentY + 1, surface.text);
        cv.fillRect(x + 4, contentY + 10, width - 8, 4, colours[i]);
        cv.drawRect(x + 3, contentY + 9, width - 6, 6, surface.edge);
    }
}

void drawGradientDetails(cyd_ui::Surface &cv, bool portrait) {
    const int x = portrait ? PortraitOptionsX : OptionsX;
    const int y = portrait ? PortraitBodyY : TracksY;
    const int column = portrait ? PortraitOptionsW / 2 : StyleColumnW;
    const int height = (portrait ? PortraitRowH : RowHeight) - 1;
    const int labelY = y + (height - 7) / 2;
    cv.fillRect(x, y, column - 2, height, panel.fill);
    cv.drawFastHLine(x, y + height - 1, column - 2, panel.edge);
    cv.textCentre("< Styles", x + (column - 2) / 2, labelY, panel.text);
    const int modeX = x + column;
    cv.fillRect(modeX, y, column - 2, height, panel.fill);
    cv.drawFastHLine(modeX, y + height - 1, column - 2, panel.edge);
    cv.textCentre(GradientModes[static_cast<int>(draft.gradientMode)], modeX + (column - 2) / 2, labelY, panel.text);
    const uint16_t colours[] = {preview.background, preview.dusk, preview.bone, preview.ember};
    drawStops(cv, "Start colour", draft.gradientStart, portrait ? PortraitStartY : StartY, colours, portrait);
    drawStops(cv, "End colour", draft.gradientEnd, portrait ? PortraitEndY : EndY, colours, portrait);
}

void drawTrack(cyd_ui::Surface &cv, int i, bool portrait) {
    const Track &track = Tracks[i];
    const int rowHeight = portrait ? PortraitRowH : RowHeight;
    const int y = (portrait ? PortraitBodyY : TracksY) + i * rowHeight;
    if (!cv.intersects(y, rowHeight)) return;
    const int railY = y + (rowHeight - RowHeight) / 2;
    const int right = portrait ? PortraitTrackRight : TrackRight;
    const int span = right - track.left;
    const int v = value(i);
    const bool selected = i == focusedTrack;
    const Surface &surface = selected ? panel : background;
    if (selected) cv.fillRect(3, y, portrait ? hal::W - 6 : 144, rowHeight - 1, panel.fill);
    cv.text(track.label, 4, railY + 3, surface.text);

    if (i < Saturation) {
        for (int x = track.left; x <= right; x += 2) {
            const uint32_t channel = (x - track.left) * 255 / span;
            cv.fillRect(x, railY + 5, x == right ? 1 : 2, 4, pixelRgb(channel << componentShift(i)));
        }
    } else {
        cv.drawFastHLine(track.left, railY + 6, span + 1, surface.edge);
        cv.drawFastVLine(track.left + span / 2, railY + 4, 6, surface.text);
    }
    if (i < Saturation) cv.drawRect(track.left - 1, railY + 4, span + 3, 6, surface.edge);
    const int knob = track.left + ((v - track.minimum) * span + (track.maximum - track.minimum) / 2) /
                                 (track.maximum - track.minimum);
    cv.diamond(knob, railY + 7, 3, surface.fill);
    cv.diamond(knob, railY + 7, 2, selected ? surface.focus : surface.text);

    char label[8];
    const char *format = i == Saturation ? "%d%%" : i == Brightness ? "%+d" : "%d";
    snprintf(label, sizeof label, format, v);
    if (portrait) {
        cv.textCentre(label, PortraitValueX, railY + 3, surface.text);
        if (selected) {
            cv.drawFastHLine(PortraitMinusX, railY + 13, PortraitStepW, surface.edge);
            cv.drawFastHLine(PortraitPlusX, railY + 13, PortraitStepW, surface.edge);
            cv.textCentre("-", PortraitMinusX + PortraitStepW / 2, railY + 3, surface.text);
            cv.textCentre("+", PortraitPlusX + PortraitStepW / 2, railY + 3, surface.text);
        }
    } else cv.textRight(label, ValueRight, railY + 3, surface.text);
}

void drawPreview(cyd_ui::Surface &cv, bool portrait) {
    const int x = portrait ? PortraitPreviewX : PreviewX;
    const int y = portrait ? PortraitPreviewY : PreviewY;
    const int w = portrait ? PortraitPreviewW : PreviewW;
    const bool showStops = tab == StyleTab && gradientDetails;
    const bool middleStop = showStops && draft.gradientMode != GradientMode::Blend;
    const int h = portrait ? PortraitPreviewH : tab != StyleTab ? 43 : middleStop ? 49 : 59;
    const int labelY = portrait ? 61 : TracksY;
    const char *label = saveFailed ? "Save failed" : showStops ? "Stop colours" : "Preview";
    cv.text(label, x, labelY, saveFailed ? theme::readableInk(Error, background.fill) : background.text);
    if (portrait && tab != StyleTab) {
        char hex[8];
        snprintf(hex, sizeof hex, "#%06lX", static_cast<unsigned long>(draft.rgb[tab] & 0xffffffu));
        cv.textRight(hex, x + w, labelY, background.text);
    }
    if (cv.intersects(y, h)) {
        if (showStops) theme::drawBackground(cv.canvas, x, y - cv.top, w, h, preview);
        else theme::drawSceneBackground(cv.canvas, x, y - cv.top, w, h, preview);
    }
    cv.frame(x, y, w, h, background.edge, background.focus);
    if (showStops) {
        if (middleStop) {
            if (portrait) cv.textRight("Via Panel", x + w, labelY, background.text);
            else cv.textCentre("Via Panel", x + w / 2, y + h + 3, background.text);
        }
        return;
    }
    cv.glyphs(DAEDRIC_S, "D", x + 5, y + 3, preview.ember);
    cv.text("diviner", x + 23, y + 8, preview.bone);
    cv.drawFastHLine(x + 4, y + 19, w - 8, preview.clot);
    cv.diamond(x + w / 2, y + 19, 1, preview.rust);
    cv.fillRect(x + 8, y + 23, w - 16, 12, preview.dusk);
    cv.frame(x + 8, y + 23, w - 16, 12, preview.rust, preview.ember);
    cv.textCentre("rite", x + w / 2, y + 25, preview.glow);
    if (cv.intersects(y + h - 5, 3)) {
        for (int i = 0; i < w - 10; ++i) {
            const auto index = i * 255 / (w - 11);
            cv.drawFastVLine(x + 5 + i, y + h - 5, 3, preview.intensity[index]);
        }
    }

    if (tab == StyleTab || portrait) return;
    const int plusX = x + w - StepWidth;
    cv.fillRect(x, StepsY + 2, StepWidth, 12, panel.fill);
    cv.fillRect(plusX, StepsY + 2, StepWidth, 12, panel.fill);
    cv.drawFastHLine(x, StepsY + 13, StepWidth, panel.edge);
    cv.drawFastHLine(plusX, StepsY + 13, StepWidth, panel.edge);
    cv.textCentre("-", x + StepWidth / 2, StepsY + 4, panel.text);
    cv.textCentre("+", plusX + StepWidth / 2, StepsY + 4, panel.text);
    char hex[8];
    snprintf(hex, sizeof hex, "#%06lX", static_cast<unsigned long>(draft.rgb[tab] & 0xffffffu));
    cv.textCentre(hex, x + w / 2, StepsY + 4, background.text);
}

void drawHeader(cyd_ui::Surface &cv, bool pending) {
    if (!cv.intersects(0, 21)) return;
    const int runeWidth = cv.glyphs(DAEDRIC_S, "PALETTE", 4, 1, background.focus);
    cv.text("palette", 4 + runeWidth + 6, 5, background.text);
    if (pending) cv.textRight("unsaved", 167, 5, background.text);
    else if (theme::colourScheme() == ColourScheme::Custom) cv.textRight("saved", 167, 5, background.text);
    cv.textRight(pending ? "< Discard" : "< Settings", 236, 5, background.focus);
    cv.drawFastHLine(0, 18, hal::W, background.edge);
    cv.diamond(hal::W / 2, 18, 2, background.focus);
}

void drawActions(cyd_ui::Surface &cv, bool portrait) {
    const int top = portrait ? PortraitPresetY : ActionsY;
    const int width = portrait ? PortraitActionW : ActionWidth;
    const int height = portrait ? PortraitActionH - 5 : 14;
    cv.drawFastHLine(0, top - 1, hal::W, background.edge);
    for (int i = 0; i < ActionCount; ++i) {
        const int column = portrait ? i % PortraitActionsPerRow : i;
        const int row = portrait && i >= PortraitActionsPerRow ? PortraitGenerateY : top;
        const int x = column * width + 2, y = row + 2, w = width - 4;
        const bool save = Actions[i].key == 's';
        cv.fillRect(x, y, w, height, panel.fill);
        if (save) cv.frame(x, y, w, height, panel.edge, panel.focus);
        else cv.drawFastHLine(x, y + height - 1, w, panel.edge);
        cv.textCentre(Actions[i].label, x + w / 2, y + (height - 8) / 2, save ? panel.focus : panel.text);
    }
}

void updatePointer() {
    int x, y;
    bool start, update = false;
    while (cyd_controls::takePalettePointer(x, y, start)) update = pointer(x, y, start) || update;
    if (update) refresh();
}

void drawEditor(cyd_ui::Surface &cv, bool portrait) {
    cv.fillRect(0, 0, hal::W, portrait ? cv.height() : hal::H, background.fill);
    drawHeader(cv, changed());
    drawTabs(cv, portrait ? PortraitTabsY : TabsY, portrait ? PortraitTabsH : TabsH);
    if (tab == StyleTab) {
        if (gradientDetails) drawGradientDetails(cv, portrait);
        else drawStyles(cv, portrait);
    } else {
        for (int i = 0; i < TrackCount; ++i) drawTrack(cv, i, portrait);
    }
    drawPreview(cv, portrait);
    drawActions(cv, portrait);
}

void draw(uint32_t) {
    updatePointer();
    cyd_ui::Surface surface(hal::canvas(), 0);
    drawEditor(surface, false);
}

} // namespace

void preparePalettePortrait() { updatePointer(); }

void drawPalettePortrait(Canvas &canvas, int top) {
    cyd_ui::Surface surface(canvas, top);
    drawEditor(surface, true);
}

const Rite RITE_PALETTE = {"PALETTE", "palette", "design a colourway", enter, leave, key, draw};

#endif
