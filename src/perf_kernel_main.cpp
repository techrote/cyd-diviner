// Standalone comparative build: deliberately excludes Diviner, LCD and audio.
#ifdef GRIM_PERF_KERNEL_STANDALONE
#include "perf_kernels.h"
#include <Arduino.h>
#include <esp_chip_info.h>
#ifndef GRIM_PERF_SOURCE
#define GRIM_PERF_SOURCE "unknown"
#endif
#ifndef GRIM_PERF_PROFILE
#define GRIM_PERF_PROFILE "unknown"
#endif
#ifndef GRIM_PERF_SOURCE_DIGEST
#define GRIM_PERF_SOURCE_DIGEST "unknown"
#endif

namespace {
char command_line[128];
unsigned command_length;
bool overflow;
}
void setup() {
    Serial.begin(115200);
    delay(500);
    esp_chip_info_t chip; esp_chip_info(&chip);
    Serial.printf("PERF {\"schema\":1,\"type\":\"kernel_identity\",\"source\":\"%s\",\"profile\":\"%s\",\"source_digest\":\"%s\",\"chip\":\"%s\",\"revision\":%u,\"cores\":%u,\"cpu_mhz\":%u,\"flash_bytes\":%u,\"psram_bytes\":%u,\"workspace_bytes\":%u,\"framework\":\"%s\",\"temperature_c\":null,\"temperature_status\":\"NOT RUN; no calibrated sensor driver in portable kernel target\"}\n",
        GRIM_PERF_SOURCE, GRIM_PERF_PROFILE, GRIM_PERF_SOURCE_DIGEST,
        ESP.getChipModel(), chip.revision, chip.cores, ESP.getCpuFreqMHz(), ESP.getFlashChipSize(), ESP.getPsramSize(), unsigned(sizeof(perf_kernels::Workspace)), ESP.getSdkVersion());
}
void loop() {
    while (Serial.available()) {
        const int byte = Serial.read();
        if (byte == '\n') {
            command_line[command_length] = 0;
            if (!overflow && !perf_kernels::command(command_line)) Serial.println("PERF {\"schema\":1,\"type\":\"kernel_error\",\"error\":\"unknown command\"}");
            command_length = 0; overflow = false;
        } else if (byte != '\r') {
            if (command_length + 1 < sizeof(command_line)) command_line[command_length++] = static_cast<char>(byte);
            else overflow = true;
        }
    }
    delay(1);
}
#endif
