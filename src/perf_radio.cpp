// Isolated controller-initialization cost only. No discovery, association,
// credentials, peer traffic, or inference of wireless throughput.
#ifdef GRIM_PERF
#include "perf_radio.h"
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_now.h>
#include <cstring>

namespace perf_radio {
namespace {
const char *current = "off";
bool nowReady = false;
bool bleReady = false;
void stop() {
    if (nowReady) { esp_now_deinit(); nowReady = false; }
    if (bleReady) {
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        bleReady = false;
    }
    WiFi.persistent(false);
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    current = "off";
}
}
bool set(const char *requested) {
    if (strcmp(requested, "off") && strcmp(requested, "wifi") &&
        strcmp(requested, "ble") && strcmp(requested, "espnow")) return false;
    if (!strcmp(current, requested)) return true;
    stop();
    if (!strcmp(requested, "off")) return true;
    if (!strcmp(requested, "ble")) {
        esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if (esp_bt_controller_init(&config) != ESP_OK) return false;
        if (esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK) {
            esp_bt_controller_deinit();
            return false;
        }
        bleReady = true;
        current = "ble";
        return true;
    }
    if (!WiFi.mode(WIFI_STA)) return false;
    // Do not call WiFi.begin(): stored SSIDs are never used.
    WiFi.disconnect(false, false);
    if (!strcmp(requested, "espnow")) {
        if (esp_now_init() != ESP_OK) { stop(); return false; }
        nowReady = true;
        current = "espnow";
    } else current = "wifi";
    return true;
}
const char *mode() { return current; }
}
#endif
