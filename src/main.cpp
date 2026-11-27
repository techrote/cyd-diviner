// Daedric Grimoire shell: splash, rite carousel, volume, and the frame loop
// that drives whichever rite is open.
#include "hal.h"
#include "rite.h"
#include "uisfx.h"
#include "theme.h"
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef GRIM_CYD
#include "cyd_controls.h"
#endif
#ifdef GRIM_CYD_DIAGNOSTICS
#include <Arduino.h>
#endif

using namespace theme;

namespace {
#ifdef GRIM_CYD
const Rite Settings{"SETTINGS", "settings", "display, sound, and palettes", nullptr, nullptr, nullptr, nullptr};
#endif
const Rite *const RITES[] = {&RITE_DRONE,  &RITE_SEQ,    &RITE_BEAT,
                             &RITE_WIRED,  &RITE_VISIONS, &RITE_ORACLE,
                             &RITE_LINK,   &RITE_SCRY,   &RITE_LISTEN,
                             &RITE_KIN,    &RITE_NET,    &RITE_GEMINI,
#ifndef GRIM_CYD
                             &RITE_IR,
#endif
                             &RITE_SENTINEL, &RITE_PULSE,
#ifdef GRIM_CYD
                             &Settings,
#else
                             &RITE_PROBE,
#endif
};
constexpr int N_RITES = sizeof(RITES) / sizeof(RITES[0]);
constexpr uint32_t FRAME_MS = 33;

enum Mode { SPLASH, MENU, RITE };
Mode mode = SPLASH;
int sel = 0;
float slide = 0; // animated carousel position, eases toward sel
const Rite *active = nullptr;
uint32_t t0 = 0, selAt = 0;
#ifndef GRIM_CYD
uint32_t volShownAt = 0, escArmedAt = 0;
#endif

struct Mote {
    float x, y, vy;
    uint8_t life;
} motes[18];

struct MoteBounds { int top, bottom; };

MoteBounds moteBounds() {
#ifdef GRIM_CYD
    if (cyd_controls::isPortrait()) {
        const int top = cyd_controls::viewportTop();
        return {-top, cyd_controls::viewportBottom() - top};
    }
    constexpr int margin = 22; // 30 physical pixels at landscape scale.
    return {-margin, hal::H + margin};
#else
    return {0, hal::H};
#endif
}

void motesInit() {
    const auto [top, bottom] = moteBounds();
    for (auto &m : motes) {
        m.x = rand() % hal::W;
        m.y = top + rand() % (bottom - top);
        m.vy = 0.15f + (rand() % 100) / 220.f;
        m.life = rand() % 255;
    }
}
void motesDraw() {
    Canvas &cv = hal::canvas();
    const auto [top, bottom] = moteBounds();
    bool styled = palette().backgroundStyle != BackgroundStyle::Flat;
#ifdef GRIM_CYD
    const bool nativeMenu = mode == MENU && cyd_controls::menuBackdrop() != cyd_controls::MenuBackdrop::Theme;
    const bool hasMargins = cyd_controls::viewportTop() > 0;
#endif
    for (auto &m : motes) {
        m.y -= m.vy;
        m.life += 3;
        if (m.y < top || m.y > bottom) {
            m.y = bottom - 1;
            m.x = rand() % hal::W;
        }
        uint8_t a = (uint8_t)(128 + 127 * sinf(m.life * 0.0245f));
        const int x = static_cast<int>(m.x), y = static_cast<int>(floorf(m.y));
#ifdef GRIM_CYD
        if (nativeMenu) {
            queueVisionsMenuMote(x, y, a);
            continue;
        }
#endif
        if (y < 0 || y >= hal::H) {
#ifdef GRIM_CYD
            if (hasMargins && a) hal::queueBackdropMote(x, y, C_EMBER, a);
#endif
            continue;
        }
        if (!styled) cv.drawPixel(x, y, blend(C_EMBER, C_VOID, a));
        else if (a) cv.drawPixel(x, y, blend(C_EMBER, cv.readPixel(x, y), a));
    }
}

void heptagram(int cx, int cy, int r, float a, uint16_t c) {
    Canvas &cv = hal::canvas();
    int px[7], py[7];
    for (int i = 0; i < 7; ++i) {
        float t = a + i * 6.2831853f / 7;
        px[i] = cx + (int)(cosf(t) * r);
        py[i] = cy + (int)(sinf(t) * r);
    }
    for (int i = 0; i < 7; ++i) cv.drawLine(px[i], py[i], px[(i + 3) % 7], py[(i + 3) % 7], c);
    for (int i = 0; i < 7; ++i) diamond(px[i], py[i], 1, C_GLOW);
}

/* ------------------------------------------------------------ splash */
const char *const LINES[] = {"present day.", "present time.", "the wired remembers.", "let us begin."};

void drawSplash(uint32_t now) {
    Canvas &cv = hal::canvas();
    uint32_t t = now - t0;
    theme::drawBackground(cv);
    motesDraw();
    int cx = 62, cy = 67;
    cv.drawCircle(cx, cy, 50, C_CLOT);
    cv.drawCircle(cx, cy, 31, C_CLOT);
    const char *ring = "THEGRIMOIREOFTHEWIRED";
    int n = strlen(ring);
    for (int i = 0; i < n; ++i) {
        float a = -1.5708f + i * 6.2831853f / n + t * 0.00025f;
        char g[2] = {ring[i], 0};
        bool lit = ((t / 140) % n) == (uint32_t)i;
        int gx = cx + (int)(cosf(a) * 40.5f) - 4, gy = cy + (int)(sinf(a) * 40.5f) - 8;
        glyphs(DAEDRIC_S, g, gx, gy, lit ? C_GLOW : (i & 1 ? C_BLOOD : C_RUST));
    }
    heptagram(cx, cy, 28, t * 0.0012f, C_BLOOD);
    cv.fillCircle(cx, cy, 13, C_DUSK);
    glyphsCentre(DAEDRIC_M, "G", cx + 1, cy - 10, C_CLOT);
    glyphsCentre(DAEDRIC_M, "G", cx, cy - 11, C_EMBER);

    // title decodes letter by letter
    const char *title = "GRIMOIRE";
    int shown = t / 120;
    char buf[16] = {0};
    for (int i = 0; i < 8; ++i) buf[i] = i < shown ? title[i] : 'A' + (rand() % 26);
    glyphsCentre(DAEDRIC_M, buf, 178, 26, shown >= 8 ? C_EMBER : C_RUST);
    textCentre("G R I M O I R E", 178, 54, C_DIM);
    cv.drawFastHLine(122, 66, 112, C_CLOT);
    diamond(178, 66, 2, C_RUST);

    int li = (t / 900) % 4;
    int chars = (t % 900) / 40;
    char line[32];
    snprintf(line, sizeof line, "%.*s%s", chars, LINES[li], (t / 300) & 1 ? "_" : " ");
    text(line, 124, 76, C_BONE);
    textCentre("sound & sight", 178, 98, C_DIM);
    int bar = t * 104 / 2600;
    cv.drawRect(126, 112, 106, 5, C_CLOT);
    cv.fillRect(127, 113, bar > 104 ? 104 : bar, 3, C_BLOOD);
}

/* ------------------------------------------------------------ menu */
constexpr int CardY = 66, CardSpacing = 86;

void card(const Rite &r, int cx, bool centre, float reveal = 0.f) {
    Canvas &cv = hal::canvas();
    const DaedricFont &font = !centre ? DAEDRIC_S :
                              glyphWidth(DAEDRIC_L, r.glyph) <= 92 ? DAEDRIC_L : DAEDRIC_M;
    const int latinWidth = centre ? static_cast<int>(strlen(r.glyph)) * 6 : 0;
    const int latinSize = latinWidth * 2 <= 96 ? 2 : 1;
    // Keep the backing steady through the reveal by allowing for either word.
    const int w = std::max(glyphWidth(font, r.glyph), latinWidth * latinSize) + (centre ? 12 : 8);
    const int h = font.h + 8;
    const int x = cx - w / 2, y = CardY - h / 2;
    if (x >= hal::W || x + w <= 0) return;

    cv.fillRect(x, y, w, h, centre ? C_DUSK : C_ASH);
    frame(x, y, w, h, centre ? C_RUST : C_CLOT, centre ? C_EMBER : C_UMBER);
    if (centre) {
        const int runeY = y + 4;
        const int latinY = runeY + (font.h - 8 * latinSize) / 2;
        wordDissolve(font, r.glyph, cx, runeY, latinY, reveal, C_EMBER, C_GLOW, C_DUSK, latinSize);

        const int tagY = y - 13, subtitleY = y + h + 5;
        const auto tagAlpha = static_cast<uint8_t>(255 * smoothstep(0.4f, 1.f, reveal));
        const auto subtitleAlpha = static_cast<uint8_t>(255 * smoothstep(0.55f, 1.f, reveal));
        const auto backdropAt = [&](int y) {
            const uint16_t colour = cv.readPixel(cx, y);
#ifdef GRIM_CYD
            if (colour == menuBackdropKey()) return C_VOID;
#endif
            return colour;
        };
        if (tagAlpha) textCentre("- rite -", cx, tagY, blend(C_DIM, backdropAt(tagY + 4), tagAlpha));
        if (subtitleAlpha) textCentre(r.name, cx, subtitleY, blend(C_BONE, backdropAt(subtitleY + 4), subtitleAlpha));
    } else {
        glyphsCentre(font, r.glyph, cx, y + 4, C_BLOOD);
    }
}

void drawMenu(uint32_t now) {
    Canvas &cv = hal::canvas();
#ifdef GRIM_CYD
    drawVisionsMenuBackdrop(now);
#else
    theme::drawBackground(cv);
#endif
#ifdef GRIM_CYD
    if (cyd_controls::menuBackdrop() != cyd_controls::MenuBackdrop::Rain) motesDraw();
    if (cyd_controls::carouselSettingsVisible()) {
        cyd_controls::drawCarouselSettings(menuBackdropKey());
        return;
    }
    header("GRIMOIRE", "grimoire", menuBackdropKey());
#else
    motesDraw();
    header("GRIMOIRE", "grimoire");
#endif

    slide += (sel - slide) * 0.35f;
    if (fabsf(sel - slide) < 0.01f) slide = sel;
    const int anchor = static_cast<int>(roundf(slide));
    const float offset = anchor - slide;
    for (int d = -2; d <= 2; ++d) {
        if (d == 0) continue;
        const int i = (anchor + d + N_RITES * 4) % N_RITES;
        const int cx = static_cast<int>(hal::W / 2 + (offset + d) * CardSpacing);
        card(*RITES[i], cx, false);
    }
    const int i = (anchor % N_RITES + N_RITES) % N_RITES;
    const float reveal = smoothstep(130.f, 520.f, static_cast<float>(now - selAt));
    card(*RITES[i], static_cast<int>(hal::W / 2 + offset * CardSpacing), true, reveal);
    // arrows
    uint16_t ac = ((now / 400) & 1) ? C_RUST : C_BLOOD;
    cv.fillTriangle(4, 66, 10, 60, 10, 72, ac);
    cv.fillTriangle(235, 66, 229, 60, 229, 72, ac);

    textCentre(RITES[sel]->desc, hal::W / 2, 112, C_BONE);
    for (int i = 0; i < N_RITES; ++i) diamond(hal::W / 2 - (N_RITES - 1) * 5 + i * 10, 127, 2, i == sel ? C_EMBER : C_CLOT, i == sel);

#ifndef GRIM_CYD
    if (now - volShownAt < 1200) {
        char b[24];
        snprintf(b, sizeof b, "vol %d", hal::volume() * 100 / 255);
        cv.fillRect(84, 52, 72, 26, C_DUSK);
        frame(84, 52, 72, 26, C_RUST, C_EMBER);
        textCentre(b, 120, 56, C_GLOW);
        cv.drawRect(92, 67, 56, 5, C_CLOT);
        cv.fillRect(93, 68, hal::volume() * 54 / 255, 3, C_EMBER);
    }
    if (escArmedAt && now - escArmedAt < 1500) {
        cv.fillRect(40, 50, 160, 30, C_DUSK);
        frame(40, 50, 160, 30, C_RUST, C_EMBER);
        textCentre("again: to the shrine", 120, 56, C_GLOW);
        textCentre("(back to the launcher)", 120, 67, C_DIM);
    } else escArmedAt = 0;
#endif
}

void open(const Rite *rite) {
    uisfx::trigger(uisfx::OPEN);
#ifdef GRIM_CYD
    if (rite == &Settings) {
        cyd_controls::openCarouselSettings();
        return;
    }
#endif
    active = rite;
    mode = RITE;
#ifdef GRIM_CYD
    releaseVisionsMenuBackdrop();
    cyd_controls::setRite(active);
#endif
    if (active->enter) active->enter();
#ifdef GRIM_CYD_DIAGNOSTICS
    Serial.printf("Diviner rite %s heap=%u\n", active->name, ESP.getFreeHeap());
#endif
}
void close() {
    // Silence the audio worker before a rite tears down its state/radio. This
    // makes cleanup ordering explicit and prevents one final callback from
    // touching resources while a leave hook is releasing them.
    hal::setAudio(nullptr);
    if (active && active->leave) active->leave();
    uisfx::trigger(uisfx::BACK);
#ifdef GRIM_CYD
    const bool returnToSettings = active == &RITE_PALETTE;
#endif
    active = nullptr;
    mode = MENU;
    selAt = hal::ms();
#ifdef GRIM_CYD
    cyd_controls::setRite(nullptr);
    if (returnToSettings) cyd_controls::openCarouselSettings();
#endif
#ifdef GRIM_CYD_DIAGNOSTICS
    Serial.printf("Diviner menu heap=%u\n", ESP.getFreeHeap());
#endif
}

void finishSplash(uint32_t now) {
    mode = MENU;
    selAt = now;
#ifdef GRIM_CYD
    cyd_controls::setRite(nullptr);
#endif
#ifdef GRIM_CYD_DIAGNOSTICS
    Serial.printf("Diviner menu heap=%u\n", ESP.getFreeHeap());
#endif
}

bool volumeKey(const hal::Key &k) {
    if (k.ch == '-' || k.ch == '_' || k.ch == '=' || k.ch == '+') {
        int v = hal::volume() + ((k.ch == '-' || k.ch == '_') ? -16 : 16);
        hal::setVolume(v < 0 ? 0 : v > 255 ? 255 : v);
#ifndef GRIM_CYD
        volShownAt = hal::ms();
#endif
        return true;
    }
    return false;
}
} // namespace

void grimSetup() {
    hal::begin();
    hal::setOverlayAudio(uisfx::render); // UI blips layer over everything
    srand(hal::ms() ^ 0x5A17);
    motesInit();
    t0 = hal::ms();
}

void grimLoop() {
    uint32_t start = hal::ms();
    hal::Key k;
    while (hal::pollKey(k)) {
        if (mode == SPLASH) {
            finishSplash(hal::ms());
            uisfx::trigger(uisfx::SELECT);
            continue;
        }
        if (mode == MENU) {
            if (volumeKey(k)) continue;
#ifdef GRIM_CYD
            if (cyd_controls::carouselSettingsVisible()) {
                if (k.nav == hal::N_ESC) {
                    cyd_controls::backCarouselSettings();
                    uisfx::trigger(uisfx::BACK);
                } else if (k.ch == 'p') {
                    open(&RITE_PALETTE);
                }
                continue;
            }
#endif
#ifndef GRIM_CYD
            if (k.nav == hal::N_ESC) {
                if (escArmedAt) hal::backToLauncher();
                escArmedAt = hal::ms();
                uisfx::trigger(uisfx::BACK);
                continue;
            }
            escArmedAt = 0;
#endif
            if (k.nav == hal::N_LEFT || k.nav == hal::N_UP) { sel = (sel + N_RITES - 1) % N_RITES; selAt = hal::ms(); uisfx::trigger(uisfx::MOVE); }
            else if (k.nav == hal::N_RIGHT || k.nav == hal::N_DOWN) { sel = (sel + 1) % N_RITES; selAt = hal::ms(); uisfx::trigger(uisfx::MOVE); }
            else if (k.nav == hal::N_ENTER) open(RITES[sel]);
            else if (k.ch >= '1' && k.ch < '1' + N_RITES) open(RITES[sel = k.ch - '1']);
            // keep the carousel from spinning the long way round on wrap
            if (sel - slide > N_RITES / 2.f) slide += N_RITES;
            if (slide - sel > N_RITES / 2.f) slide -= N_RITES;
            continue;
        }
        // in a rite: Esc always leaves; Fn+-/= is volume everywhere
        if (k.nav == hal::N_ESC && !k.fn) {
            close();
            continue;
        }
        if (k.fn && volumeKey(k)) continue;
        bool wasNav = k.nav == hal::N_UP || k.nav == hal::N_DOWN || k.nav == hal::N_LEFT ||
                      k.nav == hal::N_RIGHT;
        if (!active->key(k)) close();
        // click on list/grid moves in any rite, except over the drone's held sound
        else if (wasNav && active != &RITE_DRONE) uisfx::trigger(uisfx::MOVE);
    }

    uint32_t now = hal::ms();
#ifdef GRIM_CYD
    if (active == &RITE_VISIONS && cyd_controls::visionsImmersive()) {
        hal::drawVisionsFullscreen(drawVisionsNative, now);
        const uint32_t spent = hal::ms() - start;
        hal::idle(spent < FRAME_MS ? FRAME_MS - spent : 1);
        return;
    }
#endif
#ifdef GRIM_CYD_DIAGNOSTICS
    const uint32_t drawStart = micros();
#endif
    if (mode == SPLASH) {
        drawSplash(now);
        if (now - t0 > 2600) finishSplash(now);
    } else if (mode == MENU) {
        drawMenu(now);
    } else {
        active->frame(now);
#ifndef GRIM_CYD
        if (now - volShownAt < 900) {
            Canvas &cv = hal::canvas();
            cv.fillRect(180, 2, 58, 13, C_DUSK);
            cv.drawRect(180, 2, 58, 13, C_RUST);
            cv.fillRect(182, 7, hal::volume() * 54 / 255, 3, C_EMBER);
        }
#endif
    }
#ifdef GRIM_CYD_DIAGNOSTICS
    hal::setFrameDrawUs(micros() - drawStart);
#endif
#ifdef GRIM_CYD
    if (mode == MENU && cyd_controls::menuBackdrop() != cyd_controls::MenuBackdrop::Theme) {
        hal::presentNativeMenu(renderNativeMenuBackdrop, now);
    } else
#endif
    {
        hal::present();
    }
    uint32_t spent = hal::ms() - start;
    hal::idle(spent < FRAME_MS ? FRAME_MS - spent : 1);
}
