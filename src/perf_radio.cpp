// Isolated controller-initialization cost only. No discovery, association,
// credentials, peer traffic, or inference of wireless throughput.
#ifdef GRIM_PERF
#include "perf_radio.h"
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_err.h>
#include <Arduino.h>
#include <cstring>

namespace perf_radio {
namespace {
const char *current = "off";
bool nowReady = false;
bool bleReady = false;
int lastError = ESP_OK;
const char *lastOperation = "none";
bool checked(esp_err_t code, const char *operation) {
    if (code == ESP_OK) return true;
    lastError = code; lastOperation = operation; return false;
}
bool stop() {
    bool okay = true;
    if (nowReady) { okay = checked(esp_now_deinit(), "esp_now_deinit") && okay; nowReady = false; }
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
        okay = checked(esp_bt_controller_disable(), "bt_controller_disable") && okay;
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED)
        okay = checked(esp_bt_controller_deinit(), "bt_controller_deinit") && okay;
    bleReady = false;
    WiFi.persistent(false);
    WiFi.disconnect(false, false);
    if (!WiFi.mode(WIFI_OFF)) { lastError = ESP_FAIL; lastOperation = "wifi_mode_off"; okay = false; }
    current = "off";
    return okay;
}
}
bool set(const char *requested) {
    if (strcmp(requested, "off") && strcmp(requested, "wifi") &&
        strcmp(requested, "ble") && strcmp(requested, "espnow")) return false;
    lastError = ESP_OK; lastOperation = "none";
    // An off request checks the actual stacks even when the cached label is off.
    if (strcmp(requested, "off") == 0) return stop();
    if (!strcmp(current, requested)) return true;
    if (!stop()) return false;
    if (!strcmp(requested, "off")) return true;
    if (!strcmp(requested, "ble")) {
        esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
#if CONFIG_IDF_TARGET_ESP32
        // Classic SDK defaults to BTDM; init and enable must select the same mode.
        config.mode = ESP_BT_MODE_BLE;
#endif
        if (!checked(esp_bt_controller_init(&config), "bt_controller_init_ble")) return false;
        if (!checked(esp_bt_controller_enable(ESP_BT_MODE_BLE), "bt_controller_enable_ble")) {
            esp_bt_controller_deinit();
            return false;
        }
        bleReady = true;
        current = "ble";
        return true;
    }
    if (!WiFi.mode(WIFI_STA)) { lastError = ESP_FAIL; lastOperation = "wifi_mode_sta"; return false; }
    if (!checked(esp_wifi_set_storage(WIFI_STORAGE_RAM), "wifi_storage_ram")) return false;
    // Do not call WiFi.begin(): stored SSIDs are never used.
    WiFi.disconnect(false, false);
    if (!strcmp(requested, "espnow")) {
        if (!checked(esp_now_init(), "esp_now_init")) { stop(); return false; }
        nowReady = true;
        current = "espnow";
    } else current = "wifi";
    return true;
}
const char *mode() { return current; }
void report() {
    wifi_mode_t actual = WIFI_MODE_NULL;
    const esp_err_t query = esp_wifi_get_mode(&actual);
    Serial.printf("PERF {\"schema\":1,\"type\":\"radio_state\",\"requested_state\":\"%s\",\"last_error\":%d,\"last_error_name\":\"%s\",\"last_operation\":\"%s\",\"wifi_mode\":%u,\"wifi_mode_query_status\":%d,\"bt_controller_status\":%u,\"espnow_initialized_by_profiler\":%s}\n",
        current, lastError, esp_err_to_name(lastError), lastOperation, unsigned(actual), query,
        unsigned(esp_bt_controller_get_status()), nowReady ? "true" : "false");
}
}
#endif
