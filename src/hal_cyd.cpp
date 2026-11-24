// ESP32-2432S028R backend: ILI9341 LCD, XPT2046 touch and GPIO26 speaker output.
#include "hal.h"
#include "cyd_controls.h"
#include "theme.h"
#ifdef GRIM_CYD_PCM
#include "cyd_pcm_protocol.h"
#include "cyd_pcm_serial.h"
#include <driver/uart.h>
#endif

#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <driver/i2s.h>
#include <lgfx/v1/panel/Panel_ILI9341.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace hal {
namespace {
constexpr int ScreenW = 320, ScreenH = 240;
constexpr int BacklightPin = 21, BootPin = 0;
constexpr int BlueLedPin = 17;
constexpr int TouchCs = 33, TouchMosi = 32, TouchMiso = 39, TouchClk = 25;

class Display : public lgfx::LGFX_Device {
    lgfx::Bus_SPI bus;
    lgfx::Panel_ILI9341_2 panel;

public:
    Display() {
        auto spi = bus.config();
        spi.spi_host = SPI2_HOST;
        spi.freq_write = 40000000;
        spi.freq_read = 8000000;
        spi.spi_3wire = false; // LCD readback uses the separate GPIO12 MISO line.
        spi.pin_sclk = 14;
        spi.pin_mosi = 13;
        spi.pin_miso = 12;
        spi.pin_dc = 2;
        bus.config(spi);
        panel.setBus(&bus);

        auto lcd = panel.config();
        lcd.pin_cs = 15;
        lcd.pin_rst = -1;
        lcd.invert = true;
        lcd.bus_shared = false;
        panel.config(lcd);
        setPanel(&panel);
    }
};

Display lcd;
Canvas cv(&lcd);
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(TouchCs);
bool displayReady = false, canvasReady = false;

struct BackdropMote {
    int16_t x, y;
    uint16_t colour;
    uint8_t alpha;
};

struct Rectangle {
    int left = 0, top = 0, right = 0, bottom = 0;

    bool empty() const { return left >= right || top >= bottom; }
    void include(const Rectangle &other) {
        if (empty()) {
            *this = other;
            return;
        }
        left = std::min(left, other.left);
        top = std::min(top, other.top);
        right = std::max(right, other.right);
        bottom = std::max(bottom, other.bottom);
    }
};

std::array<BackdropMote, 18> backdropMotes{};
size_t backdropMoteCount = 0;
std::array<Rectangle, 2> backdropDamage{};

// Sprite zoom uses a half-pixel pivot. This is the first output pixel that
// samples a given logical coordinate at 4/3 scale, including negative rows.
int rasterEdge(int coordinate) {
    const int scaled = 4 * coordinate - 2;
    return scaled >= 0 ? (scaled + 2) / 3 : scaled / 3;
}

void drawBackdropMotes(int origin, int sceneBottom) {
    for (size_t i = 0; i < backdropMoteCount; ++i) {
        const auto &mote = backdropMotes[i];
        const bool above = mote.y < 0;
        const Rectangle pixels{
            rasterEdge(mote.x),
            std::max(above ? 0 : sceneBottom, origin + rasterEdge(mote.y)),
            std::min(ScreenW, rasterEdge(mote.x + 1)),
            std::min(above ? origin : ScreenH, origin + rasterEdge(mote.y + 1)),
        };
        if (pixels.empty()) continue;
        const uint16_t behind = lcd.readPixel(pixels.left, pixels.top);
        lcd.fillRect(pixels.left, pixels.top, pixels.right - pixels.left, pixels.bottom - pixels.top,
                     theme::blend(mote.colour, behind, mote.alpha));
        backdropDamage[above ? 0 : 1].include(pixels);
    }
}

constexpr int AudioBlock = 128;
constexpr i2s_port_t AudioPort = I2S_NUM_0;
SemaphoreHandle_t audioLock = nullptr;
AudioFn audioFn = nullptr, overlayFn = nullptr;
std::atomic<uint8_t> level{140};
#ifdef GRIM_CYD_DIAGNOSTICS
std::atomic<uint32_t> audioBlocks{0}, signalBlocks{0};
uint32_t presentUs = 0;
#endif

void audioTask(void *) {
    int16_t main[AudioBlock], overlay[AudioBlock];
    uint16_t pcm[AudioBlock * 2];
#ifdef GRIM_CYD_PCM
    static_assert(AudioBlock == CYD_PCM_SAMPLES && RATE == CYD_PCM_RATE, "PCM protocol must match the I2S mixer");
#endif
    for (;;) {
        // Complete any in-flight callback before setAudio(nullptr) returns
        // and the main thread tears down the active rite.
        xSemaphoreTake(audioLock, portMAX_DELAY);
        if (audioFn) audioFn(main, AudioBlock);
        else memset(main, 0, sizeof(main));
        if (overlayFn) overlayFn(overlay, AudioBlock);
        else memset(overlay, 0, sizeof(overlay));
        xSemaphoreGive(audioLock);

        const int volume = level.load(std::memory_order_relaxed);
#ifdef GRIM_CYD_PCM
        const uint32_t pcmSession = cyd_pcm_serial::session();
#endif
#ifdef GRIM_CYD_DIAGNOSTICS
        bool signal = false;
#endif
        for (int i = 0; i < AudioBlock; ++i) {
            const int sample = std::clamp<int>(main[i] + overlay[i], -32768, 32767);
            const int scaled = sample * volume / 255;
            const uint16_t value = scaled + 32768;
#ifdef GRIM_CYD_PCM
            if (pcmSession) main[i] = static_cast<int16_t>(scaled);
#endif
            // The built-in DAC reads the top 8 bits. Send the same sample in
            // both I2S slots while enabling only GPIO26, leaving touch CLK alone.
            pcm[i * 2] = pcm[i * 2 + 1] = value;
#ifdef GRIM_CYD_DIAGNOSTICS
            signal |= (value >> 8) != 128;
#endif
        }
        size_t written;
        if (i2s_write(AudioPort, pcm, sizeof(pcm), &written, portMAX_DELAY) != ESP_OK) {
            vTaskDelay(1);
        }
#if defined(GRIM_CYD_PCM) || defined(GRIM_CYD_DIAGNOSTICS)
        else if (written == sizeof(pcm)) {
#ifdef GRIM_CYD_PCM
            if (pcmSession) cyd_pcm_serial::submit(pcmSession, main);
#endif
#ifdef GRIM_CYD_DIAGNOSTICS
            audioBlocks.fetch_add(1, std::memory_order_relaxed);
            if (signal) signalBlocks.fetch_add(1, std::memory_order_relaxed);
#endif
        }
#endif
    }
}

bool beginAudio() {
    audioLock = xSemaphoreCreateMutex();
    if (!audioLock) return false;

    i2s_config_t cfg{};
    cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
    cfg.sample_rate = RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = AudioBlock;
    cfg.tx_desc_auto_clear = true;
    const bool installed = i2s_driver_install(AudioPort, &cfg, 0, nullptr) == ESP_OK;
    if (installed && i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN) == ESP_OK &&
        xTaskCreatePinnedToCore(audioTask, "grim-audio", 4096, nullptr, 5, nullptr, 0) == pdPASS) {
        return true;
    }
    if (installed) {
        i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);
        i2s_driver_uninstall(AudioPort);
    }
    vSemaphoreDelete(audioLock);
    audioLock = nullptr;
    return false;
}

constexpr uint8_t QueueSize = 16;
Key input[QueueSize];
uint8_t head = 0, tail = 0;

void push(const Key &key) {
    const uint8_t next = (tail + 1) % QueueSize;
    if (next == head) return;
    input[tail] = key;
    tail = next;
}

void sendTouch(bool down, int x, int y, uint32_t now) {
    Key key;
    if (cyd_controls::touch(down, x, y, now, key)) push(key);
}

#ifdef GRIM_CYD_DIAGNOSTICS
bool syntheticDown = false;
int syntheticX = 0, syntheticY = 0;

bool pcmStreaming() {
#ifdef GRIM_CYD_PCM
    return cyd_pcm_serial::session() != 0;
#else
    return false;
#endif
}

void shot() {
    // Read the physical controller after both the app and controls are drawn.
    // Payload has one little-endian RGB565 word per pixel, left-to-right.
    Serial.printf("shot %d %d rgb565le\n", ScreenW, ScreenH);
    lgfx::rgb565_t row[ScreenW];
    for (int y = 0; y < ScreenH; ++y) {
        lcd.readRect(0, y, ScreenW, 1, row);
        Serial.write(reinterpret_cast<const uint8_t *>(row), sizeof(row));
        delay(1);
    }
}

void diagnostic(const char *cmd, uint32_t now) {
    int x, y;
    bool tap = sscanf(cmd, "tap %d %d", &x, &y) == 2;
    if (tap || sscanf(cmd, "touch %d %d", &x, &y) == 2) {
        if (x < 0 || x >= ScreenW || y < 0 || y >= ScreenH) return;
        if (tap && syntheticDown) sendTouch(false, syntheticX, syntheticY, now);
        sendTouch(true, x, y, now);
        if (tap) sendTouch(false, x, y, now);
        syntheticDown = !tap;
        syntheticX = x;
        syntheticY = y;
        if (!pcmStreaming()) Serial.println("touch accepted");
    } else if (strcmp(cmd, "release") == 0) {
        if (syntheticDown) sendTouch(false, syntheticX, syntheticY, now);
        syntheticDown = false;
    } else if (pcmStreaming()) {
        return;
    } else if (strcmp(cmd, "status") == 0) {
        Serial.printf("heap=%u largest=%u display=%d audio=%d volume=%u present_us=%u audio_blocks=%u signal_blocks=%u theme_id=%u view_h=%d clip_y=%d view_top=%d\n",
                      ESP.getFreeHeap(), ESP.getMaxAllocHeap(), displayReady, audioLock != nullptr, volume(),
                      presentUs, audioBlocks.load(std::memory_order_relaxed), signalBlocks.load(std::memory_order_relaxed),
                      static_cast<unsigned>(theme::colourScheme()), cyd_controls::viewportHeight(),
                      cyd_controls::viewportBottom(), cyd_controls::viewportTop());
    } else if (strcmp(cmd, "shot") == 0 && displayReady) {
        shot();
    }
}
#endif

#if defined(GRIM_CYD_PCM) || defined(GRIM_CYD_DIAGNOSTICS)
void dispatchSerial(char *line, unsigned length, uint32_t now) {
#ifdef GRIM_CYD_PCM
    line[length] = '\n';
    line[length + 1] = '\0';
    if (cyd_pcm_serial::handleCommand(line, now)) return;
#endif
#ifdef GRIM_CYD_DIAGNOSTICS
    line[length] = '\0';
    diagnostic(line, now);
#endif
}

void serialInput(uint32_t now) {
    static char line[40];
    static unsigned length = 0;
    static bool overflow = false;
    uint8_t bytes[64];
#ifdef GRIM_CYD_PCM
    // Read through the UART driver's independent RX path so the touch loop
    // never waits for HardwareSerial's lock while the PCM task is writing.
    const int received = uart_read_bytes(UART_NUM_0, bytes, sizeof(bytes), 0);
#else
    const int received = Serial.read(bytes, sizeof(bytes));
#endif
    for (int i = 0; i < received; ++i) {
        const int c = bytes[i];
        if (c == '\n') {
            if (!overflow) dispatchSerial(line, length, now);
            length = 0;
            overflow = false;
        } else if (c != '\r') {
            if (length < sizeof(line) - 2) line[length++] = c;
            else overflow = true;
        }
    }
}
#endif

void scanInput() {
    const uint32_t now = millis();
#ifdef GRIM_CYD_PCM
    cyd_pcm_serial::expire(now);
#endif
#if defined(GRIM_CYD_PCM) || defined(GRIM_CYD_DIAGNOSTICS)
    serialInput(now);
#endif
    static uint32_t lastScan = 0;
    if (now - lastScan < 12) return;
    lastScan = now;

#ifdef GRIM_CYD_DIAGNOSTICS
    if (syntheticDown) {
        sendTouch(true, syntheticX, syntheticY, now);
    } else
#endif
    {
        const auto p = touch.getPoint();
        const bool down = p.z >= 400;
        // CISmon calibrated XPT2046 rotation 0 in portrait. Convert that
        // coordinate pair to the panel's TFT_eSPI-compatible landscape view.
        const int x = constrain(map(p.y, 200, 3900, 0, ScreenW - 1), 0, ScreenW - 1);
        const int y = constrain(map(p.x, 3900, 200, 0, ScreenH - 1), 0, ScreenH - 1);
        sendTouch(down, x, y, now);
#ifdef GRIM_CYD_DIAGNOSTICS
        static bool wasDown = false;
        if (down && !wasDown && !pcmStreaming() &&
            cyd_controls::viewportBottom() >= cyd_controls::viewportHeight()) {
            Serial.printf("screen touch %d %d pressure %d\n", x, y, p.z);
        }
        wasDown = down;
#endif
    }

    static uint8_t bootSamples = 0;
    static bool bootDown = false;
    // Require a stable release before a held or bouncing BOOT button can
    // emit another Back.
    bootSamples = ((bootSamples << 1) | (digitalRead(BootPin) == LOW)) & 7;
    if (bootSamples == 7 && !bootDown) {
        bootDown = true;
        push(Key{'`', N_ESC});
    } else if (!bootSamples) {
        bootDown = false;
    }
}
} // namespace

void begin() {
#ifdef GRIM_CYD_PCM
    Serial.begin(CYD_PCM_BAUD);
#else
    Serial.begin(115200);
#endif
    cyd_controls::loadSettings();
    pinMode(BootPin, INPUT_PULLUP);
    for (int pin : {4, 16, BlueLedPin}) {
        digitalWrite(pin, HIGH); // The CYD RGB LED is active low.
        pinMode(pin, OUTPUT);
    }
    pinMode(BacklightPin, OUTPUT);
    digitalWrite(BacklightPin, LOW);
    displayReady = lcd.init();
    if (displayReady) {
        lcd.setRotation(5); // Same memory access directions as TFT_eSPI rotation 1.
        lcd.fillScreen(static_cast<uint16_t>(0));
        digitalWrite(BacklightPin, HIGH);
    }
    cv.setColorDepth(16);
    canvasReady = cv.createSprite(W, H) != nullptr;
    if (!canvasReady) {
        cv.setColorDepth(8);
        canvasReady = cv.createSprite(W, H) != nullptr;
    }
    cv.setPivot(0, 0);
    if (!canvasReady && displayReady) lcd.drawString("Display buffer unavailable", 8, 8);

    touchSpi.begin(TouchClk, TouchMiso, TouchMosi, TouchCs);
    touch.begin(touchSpi);
    touch.setRotation(0);
    const bool audioReady = beginAudio();
#ifdef GRIM_CYD_PCM
    if (audioReady && !cyd_pcm_serial::begin(BlueLedPin)) Serial.println("Diviner CYD: USB PCM unavailable");
#endif
    Serial.printf("Diviner CYD: LCD %s %dx%d, canvas %s %dx%d, audio %s, heap %u bytes\n",
                  displayReady ? "ready" : "failed", lcd.width(), lcd.height(),
                  canvasReady ? "ready" : "failed", W, H, audioReady ? "ready" : "failed", ESP.getFreeHeap());
}

Canvas &canvas() { return cv; }

void queueBackdropMote(int x, int y, uint16_t colour, uint8_t alpha) {
    if (!alpha || backdropMoteCount == backdropMotes.size() || x < 0 || x >= W ||
        y < -theme::FullBackdropMargin || y >= H + theme::FullBackdropMargin ||
        (y >= 0 && y < H)) return;
    backdropMotes[backdropMoteCount++] = {static_cast<int16_t>(x), static_cast<int16_t>(y), colour, alpha};
}

void present() {
    if (!displayReady || !canvasReady) {
        backdropMoteCount = 0;
        return;
    }
#ifdef GRIM_CYD_DIAGNOSTICS
    const uint32_t start = micros();
#endif
    const int top = cyd_controls::viewportTop();
    const int viewHeight = cyd_controls::viewportHeight();
    const int clipBottom = cyd_controls::viewportBottom();
    const bool immersive = cyd_controls::visionsImmersive();
    static int marginOrigin = 0;
    static std::array<uint16_t, 8> previousBackdrop{};
    lcd.startWrite();
    if (top > 0) {
        const auto &colours = theme::palette();
        const std::array<uint16_t, 8> backdrop{
            colours.background, colours.ember, colours.dusk, colours.bone, colours.gradientTop,
            colours.gradientBottom, static_cast<uint16_t>(colours.backgroundStyle),
            static_cast<uint16_t>(colours.gradientMode),
        };
        if (marginOrigin != top || backdrop != previousBackdrop) {
            theme::drawBackdropMargins(lcd, top, colours);
            previousBackdrop = backdrop;
        } else {
            for (const auto &rect : backdropDamage) {
                if (!rect.empty()) {
                    theme::drawBackdropPatch(lcd, top, rect.left, rect.top, rect.right - rect.left,
                                             rect.bottom - rect.top, colours);
                }
            }
        }
    }
    marginOrigin = top > 0 ? top : 0;
    backdropDamage = {};
    lcd.setClipRect(0, 0, ScreenW, clipBottom);
    if (immersive) {
        constexpr float zoom = static_cast<float>(ScreenH) / (H - cyd_controls::VisionsArtTop);
        // Align scaled edges using LovyanGFX's half-pixel pivot convention.
        constexpr float pixelOffset = (zoom - 1.f) / 2.f;
        constexpr float left = (ScreenW - W * zoom) / 2.f + pixelOffset;
        constexpr float artTop = -cyd_controls::VisionsArtTop * zoom + pixelOffset;
        cv.pushRotateZoom(left, artTop, 0, zoom, zoom);
    } else {
        constexpr float scale = static_cast<float>(ScreenW) / W;
        cv.pushRotateZoom(0, top, 0, scale, scale);
    }
    lcd.clearClipRect();
    if (top > 0) drawBackdropMotes(top, top + viewHeight);
    backdropMoteCount = 0;
    if (!immersive) cyd_controls::draw(lcd);
    lcd.endWrite();
#ifdef GRIM_CYD_DIAGNOSTICS
    presentUs = micros() - start;
#endif
}

uint32_t ms() { return millis(); }

void idle(uint32_t msec) {
    scanInput();
    if (msec) delay(msec);
}

bool pollKey(Key &key) {
    scanInput();
    if (head == tail) return false;
    key = input[head];
    head = (head + 1) % QueueSize;
    return true;
}

bool anyHeld() { return cyd_controls::anyHeld(); }

void setAudio(AudioFn fn) {
    if (!audioLock) return;
    xSemaphoreTake(audioLock, portMAX_DELAY);
    audioFn = fn;
    xSemaphoreGive(audioLock);
}

void setOverlayAudio(AudioFn fn) {
    if (!audioLock) return;
    xSemaphoreTake(audioLock, portMAX_DELAY);
    overlayFn = fn;
    xSemaphoreGive(audioLock);
}

void setVolume(uint8_t v) { level.store(v, std::memory_order_relaxed); }
uint8_t volume() { return level.load(std::memory_order_relaxed); }

int batteryPercent() { return -1; }
void backToLauncher() {} // The CYD boots directly into Diviner.
} // namespace hal

void grimSetup();
void grimLoop();
void setup() { grimSetup(); }
void loop() { grimLoop(); }
