// Test the actual opt-in pipeline, with an endian-compatible fake RGB565 seam.
// No ESP hardware, graphics library implementation or physical timings simulated.
#include "../../src/perf_color.h"
#include "Arduino.h"
#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
TestSerial Serial;
static int64_t clockUs = 0;
int64_t esp_timer_get_time() { return clockUs; }
int TestSerial::printf(const char *format, ...) {
    char bytes[8192]; va_list args; va_start(args, format);
    const int n = vsnprintf(bytes, sizeof(bytes), format, args); va_end(args);
    assert(n >= 0 && size_t(n) < sizeof(bytes)); output.append(bytes, size_t(n)); return n;
}
namespace perf_profile {
bool running() { return false; }
void sceneSettings(const char *, const char *fields) { Serial.output = fields; }
}
static uint16_t oracle(const char *format, uint16_t rgb) {
    unsigned r = rgb >> 11, g = rgb >> 5 & 63, b = rgb & 31;
    if (std::string(format) == "rgb111") return (r >= 16 ? 0xf800 : 0) | (g >= 32 ? 0x7e0 : 0) | (b >= 16 ? 31 : 0);
    if (std::string(format) == "rgb444") {
        r >>= 1; g >>= 2; b >>= 1;
        return uint16_t(((r * 2 + r / 8) << 11) | ((g * 4 + g / 4) << 5) | (b * 2 + b / 8));
    }
    g >>= 1; return uint16_t((r << 11) | ((g * 2 + g / 16) << 5) | b);
}
int main() {
    for (const char *format : {"rgb111", "rgb444", "rgba5551"}) {
        // Odd width exercises row rounding. Guards prove the borrowed tail is
        // respected; fake swap565 uses the real library's swapped storage order.
        constexpr unsigned w = 3, h = 8;
        std::array<uint8_t, 1024 + 16> canvas{}; canvas.fill(0xa7);
        auto *pixels = reinterpret_cast<lgfx::swap565_t *>(canvas.data());
        assert(perf_color::configure(format, w, h, 1024, true, "gate", "trails"));
        perf_color::onRun("colour_test", 100, 200);
        assert(perf_color::beginFrame(canvas.data(), 1024, w, h, true, "gate", "trails"));
        std::array<uint16_t, w * h> expected{};
        for (unsigned i = 0; i < expected.size(); ++i) { uint16_t rgb = uint16_t(i * 2387 + 0x8410); pixels[i] = lgfx::rgb565_t{rgb}; expected[i] = oracle(format, rgb); }
        perf_color::baseRoundtrip(pixels, h);
        for (unsigned i = 0; i < expected.size(); ++i) assert(lgfx::getSwap16(pixels[i].get()) == expected[i]);
        // Black must be an opaque changed pixel, rather than confused with an
        // absent overlay. Unchanged pixels keep the quantized base exactly.
        pixels[0] = lgfx::rgb565_t{0}; expected[0] = 0;
        pixels[2] = lgfx::rgb565_t{0x7bef}; expected[2] = oracle(format, 0x7bef);
        perf_color::compositeRoundtrip(pixels, h);
        for (unsigned i = 0; i < expected.size(); ++i) assert(lgfx::getSwap16(pixels[i].get()) == expected[i]);
        for (unsigned i = 1024; i < canvas.size(); ++i) assert(canvas[i] == 0xa7);
        clockUs = 150; perf_color::endFrame(); Serial.output.clear(); perf_color::exportRun();
        assert(Serial.output.find("\"successful_applied_frames\":1") != std::string::npos);
        assert(Serial.output.find("\"bands_applied\":1") != std::string::npos);
        assert(Serial.output.find("\"first_applied_offset_us\":50") != std::string::npos);
        assert(Serial.output.find("\"rejected_frames\":0") != std::string::npos);
        assert(Serial.output.find("\"receipt_scope\":\"postwindow_actual_renderer_layout\"") != std::string::npos);
    }
    std::array<uint8_t, 1024> canvas{};
    assert(perf_color::configure("off", 3, 8, canvas.size(), false, "gate", "none"));
    perf_color::onRun("native_test", 100, 200);
    assert(!perf_color::beginFrame(canvas.data(), canvas.size(), 3, 8, false, "gate", "none"));
    clockUs = 199; perf_color::endFrame(); clockUs = 200; perf_color::endFrame();
    Serial.output.clear(); perf_color::exportRun();
    assert(Serial.output.find("\"native_frames\":1") != std::string::npos);
    assert(Serial.output.find("\"successful_applied_frames\":0") != std::string::npos);
    assert(Serial.output.find("\"packed_storage_bits_per_pixel\":0") != std::string::npos);
    assert(Serial.output.find("\"bands_applied\":0") != std::string::npos);
    assert(perf_color::configure("rgba5551", 3, 8, canvas.size(), true, "gate", "trails"));
    perf_color::onRun("rejected_test", 100, 200);
    assert(!perf_color::beginFrame(canvas.data(), 1, 3, 8, true, "gate", "trails"));
    clockUs = 150; perf_color::endFrame(); Serial.output.clear(); perf_color::exportRun();
    assert(Serial.output.find("\"rejected_frames\":1") != std::string::npos);
    assert(Serial.output.find("\"layout_fits\":false") != std::string::npos);
    puts("PASS actual color pipeline: endian seam, three formats, binary black/absent coverage, odd rows, guarded storage, window boundaries and rejection counts");
}
