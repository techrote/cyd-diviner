// Bounded complete-record telemetry for opt-in images. Never use during PCM.
#pragma once
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace perf_output {
constexpr size_t MaxRecordBytes = 6144; // > bounded kernel metadata + 256 uint32 timings.
constexpr size_t UsbTxBytes = 8192;
inline uint32_t writeFailures = 0, shortWrites = 0;
inline bool usbBufferConfigured = true;
inline bool nativeUsb() {
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE && (CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3)
    return true;
#else
    return false;
#endif
}
inline bool configureStandalone() {
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE && (CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3)
    // Arduino auto-starts HWCDC before setup. Stop its ISR before replacing
    // the ring, rather than deleting a live ring through setTxBufferSize().
    Serial.end();
    usbBufferConfigured = Serial.setTxBufferSize(UsbTxBytes) == UsbTxBytes;
    Serial.setTxTimeoutMs(2000);
#endif
    Serial.begin(115200);
    return usbBufferConfigured;
}
inline bool connectedWithin(uint32_t timeoutMs) {
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE && (CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3)
    const uint32_t began = millis();
    while (!Serial.isConnected() && millis() - began < timeoutMs) delay(1);
    return Serial.isConnected();
#else
    (void)timeoutMs; return true;
#endif
}
inline bool writeAll(const char *bytes, size_t length, uint32_t timeoutMs = 6000) {
    if (!connectedWithin(2000)) { ++writeFailures; return false; }
    const uint32_t began = millis();
    size_t offset = 0;
    while (offset < length && millis() - began < timeoutMs) {
        const size_t chunk = std::min<size_t>(256, length - offset);
        const size_t written = Serial.write(reinterpret_cast<const uint8_t *>(bytes + offset), chunk);
        if (written > chunk) { ++writeFailures; return false; }
        if (written != chunk) ++shortWrites;
        offset += written;
        if (!written) delay(1);
    }
    if (offset != length) { ++writeFailures; return false; }
    return true;
}
inline bool writeText(const char *text) { return writeAll(text, std::strlen(text)); }
class Line {
    char *bytes = nullptr;
    size_t used = 0, capacity = 0;
    bool valid = false;
public:
    explicit Line(size_t requested = MaxRecordBytes) : capacity(requested) {
        bytes = static_cast<char *>(heap_caps_malloc(capacity, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        valid = bytes != nullptr;
        if (valid) bytes[0] = 0;
    }
    ~Line() { heap_caps_free(bytes); }
    Line(const Line &) = delete;
    Line &operator=(const Line &) = delete;
    void printf(const char *format, ...) {
        if (!valid) return;
        va_list args; va_start(args, format);
        const int count = vsnprintf(bytes + used, capacity - used, format, args);
        va_end(args);
        if (count < 0 || static_cast<size_t>(count) >= capacity - used) { valid = false; return; }
        used += static_cast<size_t>(count);
    }
    void print(const char *text) { printf("%s", text); }
    void println(const char *text) { printf("%s\n", text); }
    bool send() {
        if (!valid) {
            ++writeFailures;
            writeText("PERF {\"schema\":1,\"type\":\"kernel_error\",\"error\":\"bounded telemetry allocation or length failure\"}\n");
            return false;
        }
        return writeAll(bytes, used);
    }
};
} // namespace perf_output
