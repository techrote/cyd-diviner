// Desktop backend: LovyanGFX's SDL panel at 240x135 (x3), the laptop keyboard
// standing in for the Cardputer's, SDL audio out (or a WAV file).
//
// Env vars for headless previews (SDL_VIDEODRIVER=offscreen):
//   GRIM_SCRIPT="ms:action;..."  actions: shot:<file.ppm>, key:<chars>, enter,
//                                back, esc, tab, up, down, left, right, quit
//                                (planned times; stalls preserve intervals)
//                                key: types each char; CYD also has tap:x,y
//                                and touch:x,y,down; separate taps by >30 ms
//   GRIM_WAV=<file.wav>          write the audio stream to a WAV instead of SDL
#include "hal.h"
#include "build_identity.h"
#include "audio_lifecycle.h"
#include "timing_stats.h"
#include "native_script.h"
#ifdef GRIM_CYD
#include "cyd_controls.h"
#include "theme.h"
#endif
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#include <SDL2/SDL.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

void grimSetup();
void grimLoop();
void grimShutdown();

// LovyanGFX simulator shortcuts (rotate R/L, scale 1..6) need exactly this modifier.
static constexpr SDL_Keymod SIM_SHORTCUT_MOD = KMOD_LCTRL;

namespace {
struct Board : public lgfx::LGFX_Device {
    lgfx::Panel_sdl panel;
    Board() {
        auto cfg = panel.config();
#ifdef GRIM_CYD
        cfg.memory_width = cfg.panel_width = 320;
        cfg.memory_height = cfg.panel_height = 240;
#else
        cfg.memory_width = cfg.panel_width = hal::W;
        cfg.memory_height = cfg.panel_height = hal::H;
#endif
        panel.config(cfg);
        panel.setScaling(3, 3);
        panel.setWindowTitle("daedric grimoire");
        setPanel(&panel);
    }
};
Board *lcd = nullptr;
Canvas *cv = nullptr;
audio_lifecycle::Callbacks callbacks;
std::atomic<uint8_t> vol{140};
std::atomic<bool> stopWanted{false};
std::thread wavThread;
SDL_AudioDeviceID audioDevice = 0;
diagnostics::TimingStats audioTiming, frameTiming;
std::atomic<uint32_t> audioSkipped{0};
native_script::Schedule scriptSchedule;
#ifdef GRIM_CYD
native_script::TouchEdges scriptedTouch;
int scriptedX = 0, scriptedY = 0;
bool directFrameWritten = false;
struct Mote { int x, y; uint16_t colour; uint8_t alpha; };
std::array<Mote, 18> motes{};
size_t moteCount = 0;
cyd_controls::Orientation displayOrientation = cyd_controls::Orientation::Landscape;
void rotateDisplay() {
    const auto orientation = cyd_controls::orientation();
    if (orientation == displayOrientation) return;
    displayOrientation = orientation;
    lcd->setRotation(static_cast<unsigned>(orientation));
    lcd->clearClipRect();
}
void sendTouch(bool down, int x, int y) {
    hal::Key key;
    if (cyd_controls::touch(down, x, y, hal::ms(), key)) {
        // touch already runs on the UI owner; queue mutation below is shared
        // with keyboard producers under qLock.
        extern void push(const hal::Key &);
        push(key);
    }
}
#endif

std::mutex qLock;
hal::Key queue[64];
int qHead = 0, qTail = 0;
void push(const hal::Key &k) {
    std::lock_guard<std::mutex> g(qLock);
    int n = (qTail + 1) & 63;
    if (n == qHead) return;
    queue[qTail] = k;
    qTail = n;
}

uint8_t navFor(char c) {
    switch (c) {
    case ';': return hal::N_UP;
    case '.': return hal::N_DOWN;
    case ',': return hal::N_LEFT;
    case '/': return hal::N_RIGHT;
    case '`': return hal::N_ESC;
    }
    return hal::N_NONE;
}

// --- laptop keyboard -------------------------------------------------------
// Panel_sdl pumps SDL events on the main thread; we sample the key state here
// and turn edges into Keys. Desktop arrows map to Fn+;./, so they work inside
// text editors too.
const char *SHIFTED = ")!@#$%^&*(";
char shiftChar(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= '0' && c <= '9') return SHIFTED[c - '0'];
    switch (c) {
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case '\\': return '|';
    case ';': return ':';
    case '\'': return '"';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    case '`': return '~';
    }
    return c;
}

void scanSdl() {
    static uint8_t prev[SDL_NUM_SCANCODES];
    static uint32_t heldSince[SDL_NUM_SCANCODES];
    int n = 0;
    const uint8_t *st = SDL_GetKeyboardState(&n);
    if (!st) return;
    SDL_Keymod mod = SDL_GetModState();
    bool shift = mod & KMOD_SHIFT, ctrl = mod & KMOD_CTRL, alt = mod & KMOD_ALT;
    uint32_t now = SDL_GetTicks();
    for (int sc = 0; sc < n && sc < SDL_NUM_SCANCODES; ++sc) {
        bool down = st[sc], was = prev[sc];
        prev[sc] = down;
        bool edge = down && !was;
        if (down && !edge) {
            // repeat for arrows/backspace
            if (now - heldSince[sc] > 380) {
                heldSince[sc] = now - 380 + 70;
                SDL_Keycode kc0 = SDL_GetKeyFromScancode((SDL_Scancode)sc);
                if (kc0 == SDLK_UP || kc0 == SDLK_DOWN || kc0 == SDLK_LEFT || kc0 == SDLK_RIGHT ||
                    kc0 == SDLK_BACKSPACE)
                    edge = true;
            }
            if (!edge) continue;
        }
        if (!edge) continue;
        if (!was) heldSince[sc] = now;
        SDL_Keycode kc = SDL_GetKeyFromScancode((SDL_Scancode)sc);
        hal::Key k;
        k.fn = alt; // Alt plays the Fn key
        k.ctrl = ctrl;
        switch (kc) {
        case SDLK_RETURN: k.nav = hal::N_ENTER; break;
        case SDLK_BACKSPACE: k.nav = hal::N_BACK; break;
        case SDLK_ESCAPE: k.nav = hal::N_ESC; k.ch = '`'; break;
        case SDLK_TAB: k.nav = hal::N_TAB; break;
        case SDLK_UP: k.nav = hal::N_UP; k.ch = ';'; k.fn = true; break;
        case SDLK_DOWN: k.nav = hal::N_DOWN; k.ch = '.'; k.fn = true; break;
        case SDLK_LEFT: k.nav = hal::N_LEFT; k.ch = ','; k.fn = true; break;
        case SDLK_RIGHT: k.nav = hal::N_RIGHT; k.ch = '/'; k.fn = true; break;
        case SDLK_SPACE: k.ch = ' '; break;
        default:
            if (kc >= 32 && kc < 127) {
                char c = (char)kc;
                // LovyanGFX takes R/L/1..6 only when the modifiers equal
                // SIM_SHORTCUT_MOD exactly; drop those so they don't reach the app
                if (mod == SIM_SHORTCUT_MOD && (c == 'r' || c == 'l' || (c >= '1' && c <= '6')))
                    continue;
                if (shift) c = shiftChar(c);
                k.ch = c;
                if (!shift) k.nav = navFor(c);
            } else continue;
        }
        push(k);
    }
}

// --- scripted previews ---------------------------------------------------
void saveShot(const std::string &path) {
    const int width = lcd->width(), height = lcd->height();
    std::vector<lgfx::rgb888_t> row(width);
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); return; }
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; ++y) {
        lcd->readRect(0, y, width, 1, row.data());
        for (const auto &px : row) { const uint8_t rgb[] = {px.R8(), px.G8(), px.B8()}; fwrite(rgb, 1, 3, f); }
    }
    fclose(f);
}

struct ScriptStep { uint32_t at; std::string action; };
std::vector<ScriptStep> script;
size_t nextStep = 0;
void prepareScript() {
    const char *env = getenv("GRIM_SCRIPT");
    if (!env) return;
    std::string s(env);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find(';', pos);
        std::string step = s.substr(pos, end == std::string::npos ? std::string::npos : end-pos);
        pos = end == std::string::npos ? s.size() : end+1;
        size_t colon = step.find(':');
        if (colon == std::string::npos) continue;
        uint32_t at = strtoul(step.substr(0, colon).c_str(), nullptr, 10);
        std::string act = step.substr(colon+1);
        if (act.rfind("key:", 0) == 0) {
            std::string text = act.substr(4);
            for (size_t i = 0; i < text.size(); ++i, at += 15) {
                char ch = text[i];
                if (ch == '%' && i+2 < text.size()) { ch = char(strtol(text.substr(i+1,2).c_str(), nullptr, 16)); i += 2; }
                script.push_back({at, std::string("char:")+ch});
            }
        } else script.push_back({at, act});
    }
    std::stable_sort(script.begin(), script.end(), [](const auto &a, const auto &b) { return a.at < b.at; });
    scriptSchedule.reset(hal::ms());
}

void runScript() {
    if (nextStep >= script.size()) return;
    const uint32_t now = hal::ms();
    if (!scriptSchedule.dispatch(script[nextStep].at, now)) return;
    const std::string act = script[nextStep++].action;
    if (act.rfind("shot:", 0) == 0) { saveShot(act.substr(5)); return; }
    if (act == "quit") { stopWanted.store(true, std::memory_order_release); return; }
#ifdef GRIM_CYD
    int x = 0, y = 0, down = 0;
    if (sscanf(act.c_str(), "touch:%d,%d,%d", &x, &y, &down) == 3) {
        scriptedX = x; scriptedY = y; scriptedTouch.set(down != 0, x, y, sendTouch); return;
    }
    if (sscanf(act.c_str(), "tap:%d,%d", &x, &y) == 2) {
        scriptedTouch.tap(x, y, sendTouch); return;
    }
#endif
    hal::Key k;
    if (act.rfind("char:", 0) == 0 && act.size() > 5) k.ch = act[5];
    else if (act == "enter") k.nav = hal::N_ENTER;
    else if (act == "back") k.nav = hal::N_BACK;
    else if (act == "esc") { k.nav = hal::N_ESC; k.ch = '`'; }
    else if (act == "tab") k.nav = hal::N_TAB;
    else if (act == "up") { k.nav = hal::N_UP; k.ch = ';'; k.fn = true; }
    else if (act == "down") { k.nav = hal::N_DOWN; k.ch = '.'; k.fn = true; }
    else if (act == "left") { k.nav = hal::N_LEFT; k.ch = ','; k.fn = true; }
    else if (act == "right") { k.nav = hal::N_RIGHT; k.ch = '/'; k.fn = true; }
    else return;
    push(k);
}

// --- audio -------------------------------------------------------------------
void render(int16_t *out, int n, bool scale = true) {
    const auto started = std::chrono::steady_clock::now();
    hal::AudioFn fn = nullptr, ofn = nullptr;
    if (callbacks.acquire(fn, ofn)) {
        if (fn) fn(out, n); else memset(out, 0, n * sizeof(*out));
        if (ofn && n <= 4096) {
            int16_t ob[4096];
            ofn(ob, n);
            for (int i = 0; i < n; ++i) out[i] = std::clamp<int32_t>(int32_t(out[i]) + ob[i], -32768, 32767);
        }
        callbacks.release();
    } else {
        memset(out, 0, n * sizeof(*out));
        audioSkipped.fetch_add(1, std::memory_order_relaxed);
    }
    const int level = vol.load(std::memory_order_relaxed);
    if (scale) for (int i = 0; i < n; ++i) out[i] = (int16_t)((int32_t)out[i] * level / 255);
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
    audioTiming.record(uint32_t(us), n, hal::RATE);
}

void sdlCallback(void *, uint8_t *stream, int len) { render((int16_t *)stream, len / 2); }

void wavWriter(std::string path) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(0); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(hal::RATE); u32(hal::RATE * 2); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32(0);
    uint32_t total = 0;
    int16_t blk[256];
    auto t0 = std::chrono::steady_clock::now();
    for (uint64_t frames = 0; !stopWanted.load(std::memory_order_acquire); frames += 256) {
        std::this_thread::sleep_until(t0 + std::chrono::microseconds(frames * 1000000 / hal::RATE));
        render(blk, 256, false);
        fwrite(blk, 2, 256, f);
        total += 512;
        // keep the header valid so the file is playable whenever the sim quits
        long here = ftell(f);
        fseek(f, 4, SEEK_SET); u32(36 + total);
        fseek(f, 40, SEEK_SET); u32(total);
        fseek(f, here, SEEK_SET);
        fflush(f);
    }
    fclose(f);
}

void startAudio() {
    if (const char *wav = getenv("GRIM_WAV")) {
        wavThread = std::thread(wavWriter, std::string(wav));
        return;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
    SDL_AudioSpec want{}, have{};
    want.freq = hal::RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = sdlCallback;
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audioDevice) SDL_PauseAudioDevice(audioDevice, 0);
}

void reportTiming() {
    const auto a = audioTiming.snapshot(), f = frameTiming.snapshot();
    FILE *out = stderr;
    if (const char *path = getenv("GRIM_METRICS")) { if (FILE *file = fopen(path, "w")) out = file; }
    fprintf(out, "{\"source\":\"native-desktop\",\"sample_rate\":%d,\"audio_blocks\":%u,\"audio_p50_upper_us\":%u,\"audio_p95_upper_us\":%u,\"audio_p99_upper_us\":%u,\"audio_max_us\":%u,\"audio_over_deadline\":%u,\"callback_publication_silence_blocks\":%u,\"frames\":%u,\"frame_cycle_p99_upper_us\":%u,\"frame_cycle_max_us\":%u}\n",
        hal::RATE, a.count, a.percentileUpper(50), a.percentileUpper(95), a.percentileUpper(99), a.maxUs, a.overDeadline,
        audioSkipped.load(), f.count, f.percentileUpper(99), f.maxUs);
    if (out != stderr) fclose(out);
}
void userFunc() {
    grimSetup(); prepareScript();
    while (!stopWanted.load(std::memory_order_acquire)) {
        const auto start = std::chrono::steady_clock::now();
        grimLoop();
        frameTiming.record(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());
    }
    grimShutdown();
    hal::setAudio(nullptr); hal::setOverlayAudio(nullptr);
    if (audioDevice) { SDL_PauseAudioDevice(audioDevice, 1); SDL_CloseAudioDevice(audioDevice); audioDevice = 0; }
    if (wavThread.joinable()) wavThread.join();
    reportTiming();
}
} // namespace

namespace hal {
void begin() {
    lcd->init();
    lcd->setRotation(0);
#ifdef GRIM_CYD
    cyd_controls::loadSettings(); rotateDisplay();
#endif
    cv = new Canvas(lcd);
    cv->setColorDepth(16);
    cv->createSprite(W, H);
    cv->setPivot(0, 0);
    startAudio();
    debugf("diviner_identity source=%s tree=%s profile=%s state=%s", DIVINER_SOURCE_SHA, DIVINER_SOURCE_TREE, DIVINER_BUILD_ENV, DIVINER_TREE_STATE);
}
Canvas &canvas() { return *cv; }
void present() {
#ifdef GRIM_CYD
    if (cyd_controls::visionsImmersive()) return;
    rotateDisplay();
    const int left = cyd_controls::viewportLeft(), top = cyd_controls::viewportTop();
    const int clip = cyd_controls::viewportBottom();
    lcd->startWrite();
    theme::drawBackdropMargins(*lcd, top, theme::palette());
    lcd->setClipRect(0, 0, lcd->width(), clip);
    const float scale = float(cyd_controls::viewportWidth()) / W;
    if (cyd_controls::isPortrait()) cv->pushSprite(left, top);
    else cv->pushRotateZoom(left, top, 0, scale, scale);
    lcd->clearClipRect();
    for (size_t i = 0; i < moteCount; ++i) {
        const auto &m = motes[i];
        const int x = left + int(m.x * scale), y = top + int(m.y * scale);
        if (x < 0 || x >= lcd->width() || y < 0 || y >= clip) continue;
        const auto behind = lcd->readPixel(x, y);
        lcd->fillRect(x, y, scale > 1 ? 2 : 1, scale > 1 ? 2 : 1, theme::blend(m.colour, behind, m.alpha));
    }
    moteCount = 0;
    cyd_controls::draw(*lcd);
    lcd->endWrite();
#else
    cv->pushSprite(0, 0);
#endif
}
#ifdef GRIM_CYD
void drawVisionsFullscreen(FullscreenRenderer render, uint32_t now) {
    rotateDisplay(); lcd->clearClipRect(); lcd->startWrite();
    if (render) render(*lcd, now);
    lcd->clearClipRect(); lcd->endWrite(); moteCount = 0;
}
void presentNativeMenu(FullscreenRenderer render, uint32_t now) {
    drawVisionsFullscreen(render, now); cyd_controls::draw(*lcd);
}
void queueBackdropMote(int x, int y, uint16_t colour, uint8_t alpha) {
    if (alpha && moteCount < motes.size() && (y < 0 || y >= H)) motes[moteCount++] = {x, y, colour, alpha};
}
#endif
uint32_t ms() { return SDL_GetTicks(); }
void idle(uint32_t msec) {
    runScript(); scanSdl();
    if (msec) SDL_Delay(msec);
}
bool pollKey(Key &k) {
    runScript(); scanSdl();
#ifdef GRIM_CYD
    if (scriptedTouch.held()) sendTouch(true, scriptedX, scriptedY);
    else {
        int32_t x = 0, y = 0;
        const bool down = lcd->getTouch(&x, &y);
        sendTouch(down, x, y);
    }
#endif
    std::lock_guard<std::mutex> g(qLock);
    if (qHead == qTail) return false;
    k = queue[qHead];
    qHead = (qHead + 1) & 63;
    return true;
}
bool anyHeld() {
#ifdef GRIM_CYD
    if (cyd_controls::anyHeld()) return true;
#endif
    int n = 0;
    const uint8_t *st = SDL_GetKeyboardState(&n);
    for (int i = 0; st && i < n; ++i)
        if (st[i]) return true;
    return false;
}
void setAudio(AudioFn fn) {
    callbacks.setMain(fn, [] { std::this_thread::yield(); });
}
void setOverlayAudio(AudioFn fn) { callbacks.setOverlay(fn, [] { std::this_thread::yield(); }); }
uint32_t heapFree() { return 0; }
uint32_t heapLargest() { return 0; }
void debugf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
void setVolume(uint8_t v) { vol = v; }
uint8_t volume() { return vol; }
int batteryPercent() { return 77; }
void backToLauncher() { stopWanted.store(true, std::memory_order_release); }
} // namespace hal

int main(int, char **) {
    lgfx::Panel_sdl::setShortcutKeymod(SIM_SHORTCUT_MOD);
    lcd = new Board();
    if (lgfx::Panel_sdl::setup() != 0) return 1;
    std::thread worker(userFunc);
    while (!stopWanted.load(std::memory_order_acquire) && lgfx::Panel_sdl::loop() == 0) {}
    stopWanted.store(true, std::memory_order_release);
    worker.join();
    delete cv; cv = nullptr;
    delete lcd; lcd = nullptr;
    return lgfx::Panel_sdl::close();
}
