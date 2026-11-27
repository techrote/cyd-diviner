// Standalone comparative build: deliberately excludes Diviner, LCD and audio.
#ifdef GRIM_PERF_KERNEL_STANDALONE
#include "perf_kernels.h"
#include "perf_output.h"
#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <cmath>
#include <cstring>
#ifdef GRIM_PERF_LINK
#include "perf_link.h"
#endif
#ifndef GRIM_PERF_SOURCE
#define GRIM_PERF_SOURCE "unknown"
#endif
#ifndef GRIM_PERF_PROFILE
#define GRIM_PERF_PROFILE "unknown"
#endif
#ifndef GRIM_PERF_SOURCE_DIGEST
#define GRIM_PERF_SOURCE_DIGEST "unknown"
#endif
#ifndef GRIM_PERF_DIRTY
#define GRIM_PERF_DIRTY 0
#endif
#if CONFIG_IDF_TARGET_ESP32
extern "C" uint8_t temprature_sens_read();
#endif

namespace {
char command_line[320];
unsigned command_length;
bool overflow;
struct SensorReading { uint8_t raw = 0; float celsius = NAN; };
SensorReading readSensor() {
    SensorReading reading;
#if CONFIG_IDF_TARGET_ESP32
    reading.raw = temprature_sens_read();
#else
    reading.celsius = temperatureRead();
#endif
    return reading;
}
void sensorFields(perf_output::Line &line, const SensorReading &reading) {
#if CONFIG_IDF_TARGET_ESP32
    line.printf("\"temperature_raw\":%u,\"temperature_c_indication\":null,\"temperature_method\":\"RTC_library_raw_undocumented_uncalibrated\"", unsigned(reading.raw));
#else
    line.print("\"temperature_raw\":null,\"temperature_c_indication\":");
    if (std::isfinite(reading.celsius)) line.printf("%.3f", double(reading.celsius));
    else line.print("null");
    line.print(",\"temperature_method\":\"Arduino_IDF_driver_Celsius_indication_external_accuracy_not_verified\"");
#endif
}
void information(const char *type) {
    esp_chip_info_t chip; esp_chip_info(&chip);
    // Driver diagnostics, if any, must precede the machine-readable JSON line.
    const SensorReading sensor = readSensor();
    perf_output::Line line(2048);
    line.printf("PERF {\"schema\":1,\"type\":\"%s\",\"source_sha\":\"%s\",\"source\":\"%s\",\"profile\":\"%s\",\"source_digest\":\"%s\",\"source_dirty\":%s,\"chip\":\"%s\",\"revision\":%u,\"cores\":%u,\"cpu_mhz\":%u,\"flash_bytes\":%u,\"flash_hz\":%u,\"psram_bytes\":%u,\"workspace_bytes\":%u,\"framework\":\"%s\",\"serial_baud\":115200,\"standalone\":true,\"ordinary_audio_worker\":false,\"ordinary_display_worker\":false,\"kernel_default_memory\":\"internal\",\"heap_internal_free_bytes\":%u,\"heap_internal_largest_bytes\":%u,\"heap_psram_free_bytes\":%u,\"heap_psram_largest_bytes\":%u,\"uptime_us\":%llu,",
        type, GRIM_PERF_SOURCE, GRIM_PERF_SOURCE, GRIM_PERF_PROFILE, GRIM_PERF_SOURCE_DIGEST, GRIM_PERF_DIRTY ? "true" : "false",
        ESP.getChipModel(), unsigned(chip.revision), unsigned(chip.cores), unsigned(ESP.getCpuFreqMHz()), unsigned(ESP.getFlashChipSize()), unsigned(ESP.getFlashChipSpeed()), unsigned(ESP.getPsramSize()), unsigned(sizeof(perf_kernels::Workspace)), ESP.getSdkVersion(),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)), static_cast<unsigned long long>(esp_timer_get_time()));
    sensorFields(line, sensor);
    line.printf(",\"native_usb\":%s,\"usb_tx_buffer_bytes\":%u,\"usb_tx_buffer_configured\":%s,\"telemetry_max_record_bytes\":%u,\"telemetry_write_failures\":%u,\"telemetry_short_writes\":%u",
        perf_output::nativeUsb() ? "true" : "false", perf_output::nativeUsb() ? unsigned(perf_output::UsbTxBytes) : 0u,
        perf_output::usbBufferConfigured ? "true" : "false", unsigned(perf_output::MaxRecordBytes), unsigned(perf_output::writeFailures), unsigned(perf_output::shortWrites));
#ifdef GRIM_PERF_LINK_PEER
    line.println(",\"link_peer_compiled\":true}");
#else
    line.println(",\"link_peer_compiled\":false}");
#endif
    line.send();
}
bool standaloneCommand(const char *line) {
    if (!std::strcmp(line, "perf info")) { information("info"); return true; }
    if (!std::strcmp(line, "perf sample")) { information("state"); return true; }
    return false;
}
}
void setup() {
    perf_output::configureStandalone();
    delay(500);
#ifdef GRIM_PERF_LINK
    perf_link::begin();
#endif
    information("kernel_identity");
}
void loop() {
#ifdef GRIM_PERF_LINK
    perf_link::poll();
#endif
    while (Serial.available()) {
        const int byte = Serial.read();
        if (byte == '\n') {
            command_line[command_length] = 0;
            bool handled = !overflow && (standaloneCommand(command_line) || perf_kernels::command(command_line));
#ifdef GRIM_PERF_LINK
            if (!overflow && !handled) handled = perf_link::command(command_line);
#endif
            if (overflow) Serial.println("PERF {\"schema\":1,\"type\":\"kernel_error\",\"error\":\"command line exceeded 319 bytes\"}");
            else if (!handled) Serial.println("PERF {\"schema\":1,\"type\":\"kernel_error\",\"error\":\"unknown command\"}");
            command_length = 0; overflow = false;
        } else if (byte != '\r') {
            if (command_length + 1 < sizeof(command_line)) command_line[command_length++] = static_cast<char>(byte);
            else overflow = true;
        }
    }
    delay(1);
}
#endif
