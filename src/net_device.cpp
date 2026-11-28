// ESP32 backend for net.h. Arduino WiFi (STA + async scan), esp_wifi
// promiscuous RX counting, Bluedroid BLE scan, a ping+port LAN sweep and a
// Gemini (TLS) client, each on a worker task where it would otherwise block.
//
// Credentials live in NVS via Preferences (namespace "grimnet"), never in
// flashable source. Only one radio stack runs at a time (see net.h).
#ifdef GRIM_DEVICE
#include "net.h"
#include "hal.h"
#include "radio_queue.h"

#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_now.h>
#include <esp_idf_version.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <fcntl.h>

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#ifdef GRIM_CYD
#include <esp_heap_caps.h>
#endif

#include <algorithm>
#include <utility>
#include <vector>

namespace net {
namespace {
enum Radio { R_NONE, R_WIFI, R_SNIFF, R_BLE, R_NOW };
Radio radio = R_NONE;
Preferences prefs;
bool scanStarted = false;
#ifdef GRIM_CYD
bool joining = false;
uint32_t joinAt = 0;

void beginJoin(const char *ssid, const char *pass) {
    if (scanStarted) {
        esp_wifi_scan_stop();
        WiFi.scanDelete();
        scanStarted = false;
    }
    joining = true;
    joinAt = millis();
    WiFi.begin(ssid, pass);
}
#endif

void stopBle();
void stopNow();
void clearWifiCache();

// --- saved credentials: s0..s7 / p0..p7 + count ---
int credCount() {
    prefs.begin("grimnet", true);
    int n = prefs.getInt("n", 0);
    prefs.end();
    return n > 8 ? 8 : n;
}
String credSSID(int i) {
    prefs.begin("grimnet", true);
    String s = prefs.getString(("s" + String(i)).c_str(), "");
    prefs.end();
    return s;
}
String credPass(int i) {
    prefs.begin("grimnet", true);
    String p = prefs.getString(("p" + String(i)).c_str(), "");
    prefs.end();
    return p;
}
void credSave(const char *ssid, const char *pass) {
    prefs.begin("grimnet", false);
    int n = prefs.getInt("n", 0);
    int slot = -1;
    for (int i = 0; i < n && i < 8; ++i)
        if (prefs.getString(("s" + String(i)).c_str(), "") == ssid) slot = i;
    if (slot < 0) {
        slot = n < 8 ? n : 7; // overwrite the last if full
        if (n < 8) prefs.putInt("n", n + 1);
    }
    prefs.putString(("s" + String(slot)).c_str(), ssid);
    prefs.putString(("p" + String(slot)).c_str(), pass);
    prefs.end();
}
void credForget(const char *ssid) {
    prefs.begin("grimnet", false);
    int n = prefs.getInt("n", 0), w = 0;
    std::vector<std::pair<String, String>> keep;
    for (int i = 0; i < n && i < 8; ++i) {
        String s = prefs.getString(("s" + String(i)).c_str(), "");
        if (s != ssid && s.length()) keep.push_back({s, prefs.getString(("p" + String(i)).c_str(), "")});
    }
    for (auto &kv : keep) {
        prefs.putString(("s" + String(w)).c_str(), kv.first);
        prefs.putString(("p" + String(w)).c_str(), kv.second);
        ++w;
    }
    prefs.putInt("n", w);
    prefs.end();
}
} // namespace

void radioStop() {
    if (radio == R_SNIFF) {
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(nullptr);
    }
    if (radio == R_BLE) stopBle();
    if (radio == R_NOW) stopNow();
    if (radio == R_WIFI || radio == R_SNIFF || radio == R_NOW) {
        if (scanStarted) esp_wifi_scan_stop();
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
        WiFi.scanDelete();
        scanStarted = false;
#ifdef GRIM_CYD
        joining = false;
#endif
    }
    clearWifiCache();
    radio = R_NONE;
}

// ---- Wi-Fi ---------------------------------------------------------------
void wifiBegin(bool connect) {
    if (radio == R_WIFI && !connect) {
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
#ifdef GRIM_CYD
        joining = false;
#endif
        return;
    }
    if (radio != R_WIFI) {
        radioStop();
        WiFi.persistent(false);
        WiFi.setAutoReconnect(connect);
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        radio = R_WIFI;
        if (connect && WiFi.status() != WL_CONNECTED) {
            String last;
            // Create the namespace on first use so later read-only scans work
            // on a freshly erased board.
            if (prefs.begin("grimnet")) {
                last = prefs.getString("last", "");
                prefs.end();
            }
            if (last.length()) {
                int n = credCount();
                for (int i = 0; i < n; ++i) {
                    if (credSSID(i) != last) continue;
#ifdef GRIM_CYD
                    beginJoin(last.c_str(), credPass(i).c_str());
#else
                    WiFi.begin(last.c_str(), credPass(i).c_str());
#endif
                    break;
                }
            }
        }
    }
}
WifiState wifiState() {
    if (radio != R_WIFI && radio != R_SNIFF) return WST_OFF;
#ifdef GRIM_CYD
    if (WiFi.status() == WL_CONNECTED) {
        joining = false;
        return WST_ONLINE;
    }
    if (joining && millis() - joinAt < 12000) return WST_CONNECTING;
    joining = false;
    return scanStarted ? WST_SCANNING : WST_FAILED;
#else
    switch (WiFi.status()) {
    case WL_CONNECTED: return WST_ONLINE;
    case WL_IDLE_STATUS:
    case WL_DISCONNECTED: return scanStarted ? WST_SCANNING : WST_CONNECTING;
    case WL_CONNECT_FAILED:
    case WL_NO_SSID_AVAIL: return WST_FAILED;
    default: return WST_CONNECTING;
    }
#endif
}
// A phone hotspot often beacons slowly and only appears in some scans, so a
// single pass misses it. We keep a cache keyed by BSSID, refresh it on every
// completed scan, and age entries out after ~30s — so a network that shows up
// in any recent scan stays in the list instead of flickering in and out.
namespace {
struct Seen { AP ap; uint32_t at; };
std::vector<Seen> apCache;
void clearWifiCache() { std::vector<Seen>().swap(apCache); }
constexpr size_t AP_CACHE_LIMIT = 64;

std::vector<AP> cacheList() {
    uint32_t now = millis();
    std::vector<AP> v;
    for (auto &s : apCache)
        if (now - s.at < 30000) v.push_back(s.ap);
    std::sort(v.begin(), v.end(), [](const AP &a, const AP &b) { return a.rssi > b.rssi; });
    return v;
}
} // namespace

int lastScanStatus = -1;
uint32_t scanRetryAt = 0;
int wifiScanStatus() { return lastScanStatus; }
int wifiScan(std::vector<AP> &out) {
    if (radio != R_WIFI) wifiBegin();
#ifdef GRIM_CYD
    if (wifiState() == WST_CONNECTING) {
        out = cacheList();
        return out.empty() ? -1 : static_cast<int>(out.size());
    }
#endif
    int16_t n = WiFi.scanComplete();
    lastScanStatus = n;
    if (n == WIFI_SCAN_FAILED || !scanStarted) {
        // a refused scan used to be re-issued every frame; back off instead
        if (n == WIFI_SCAN_FAILED && scanStarted && int32_t(millis() - scanRetryAt) < 0) {
            out = cacheList();
            return out.empty() ? -1 : (int)out.size();
        }
        scanRetryAt = millis() + 700;
        // async, include hidden, ACTIVE scan, 300ms/channel dwell: a longer
        // dwell and probe requests catch phones/APs that beacon infrequently.
        WiFi.scanNetworks(true, true, false, 300);
        scanStarted = true;
        out = cacheList();
        return out.empty() ? -1 : (int)out.size();
    }
    if (n == WIFI_SCAN_RUNNING) {
        out = cacheList();
        return out.empty() ? -1 : (int)out.size();
    }
#ifdef GRIM_CYD_DIAGNOSTICS
    hal::debugf("cyd wifi scan result=%d", n);
#endif
    uint32_t now = millis();
    apCache.erase(std::remove_if(apCache.begin(), apCache.end(), [now](const Seen &s) {
        return now - s.at >= 30000;
    }), apCache.end());
    int known = credCount();
    for (int i = 0; i < n; ++i) {
        AP a;
        a.ssid = WiFi.SSID(i).c_str();
        memcpy(a.bssid, WiFi.BSSID(i), 6);
        a.rssi = WiFi.RSSI(i);
        a.channel = WiFi.channel(i);
        a.auth = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 0 : 1;
        a.known = false;
        a.lastSeen = now;
        for (int k = 0; k < known; ++k)
            if (credSSID(k) == a.ssid.c_str()) a.known = true;
        bool merged = false;
        for (auto &s : apCache)
            if (memcmp(s.ap.bssid, a.bssid, 6) == 0) { s.ap = a; s.at = now; merged = true; break; }
        if (!merged && apCache.size() < AP_CACHE_LIMIT) apCache.push_back({a, now});
        else if (!merged) {
            auto weak = std::min_element(apCache.begin(), apCache.end(), [](const Seen &a, const Seen &b) { return a.ap.rssi < b.ap.rssi; });
            if (weak != apCache.end() && a.rssi > weak->ap.rssi) *weak = {a, now};
        }
    }
    WiFi.scanDelete();
    scanStarted = false;
    out = cacheList();
    return (int)out.size();
}
void wifiConnect(const char *ssid, const char *pass) {
    wifiBegin();
    String p = pass;
    if (!p.length()) { // use a saved password if we have one
        int n = credCount();
        for (int i = 0; i < n; ++i)
            if (credSSID(i) == ssid) p = credPass(i);
    } else {
        credSave(ssid, pass);
    }
    prefs.begin("grimnet", false);
    prefs.putString("last", ssid);
    prefs.end();
#ifdef GRIM_CYD
    beginJoin(ssid, p.c_str());
#else
    WiFi.begin(ssid, p.c_str());
#endif
}
void wifiForget(const char *ssid) { credForget(ssid); }
std::string wifiIP() { return WiFi.localIP().toString().c_str(); }
std::string wifiSSID() { return WiFi.SSID().c_str(); }
int wifiRSSI() { return WiFi.RSSI(); }
std::vector<std::string> wifiSaved() {
    std::vector<std::string> v;
    int n = credCount();
    for (int i = 0; i < n; ++i) v.push_back(credSSID(i).c_str());
    return v;
}

// ---- promiscuous sniffer -------------------------------------------------
namespace {
volatile uint32_t gMgmt[14], gData[14];
volatile int gRssi = 0;
void IRAM_ATTR snifCb(void *buf, wifi_promiscuous_pkt_type_t type) {
    auto *p = (wifi_promiscuous_pkt_t *)buf;
    int ch = p->rx_ctrl.channel;
    if (ch < 1 || ch > 13) return;
    if (type == WIFI_PKT_MGMT) gMgmt[ch]++;
    else if (type == WIFI_PKT_DATA) gData[ch]++;
    gRssi = p->rx_ctrl.rssi;
}
} // namespace
void snifferBegin() {
    radioStop();
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    wifi_promiscuous_filter_t f = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA};
    esp_wifi_set_promiscuous_filter(&f);
    esp_wifi_set_promiscuous_rx_cb(&snifCb);
    radio = R_SNIFF;
}
void snifferSetChannel(int ch) {
    if (ch >= 1 && ch <= 13) esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}
void snifferPoll(uint32_t mgmt[14], uint32_t data[14], int &rssiLast) {
    for (int c = 0; c < 14; ++c) {
        mgmt[c] = gMgmt[c];
        data[c] = gData[c];
        gMgmt[c] = 0;
        gData[c] = 0;
    }
    rssiLast = gRssi;
}

// ---- ESP-NOW -------------------------------------------------------------
namespace {
const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
radio_queue::Ring<NowPkt, 15> nq; // explicit drop-new overflow
bool nqAccepting = false;
NowStatus nqStats;
portMUX_TYPE nqMux = portMUX_INITIALIZER_UNLOCKED;
void nowEnqueue(const uint8_t *mac, const uint8_t *data, int len, int8_t rssi, bool available) {
    // ESP-NOW invokes this on the Wi-Fi task, not an ISR. Keep this bounded:
    // copy at most 250 bytes, no allocation/logging or musical/DSP processing.
    portENTER_CRITICAL(&nqMux);
    if (!nqAccepting) { portEXIT_CRITICAL(&nqMux); return; }
    ++nqStats.received;
    if (!mac || !data || len <= 0 || len > 250) {
        ++nqStats.invalid;
    } else {
        NowPkt p{};
        memcpy(p.mac, mac, 6); p.rssi = rssi; p.rssiAvailable = available;
        p.len = uint8_t(len); p.receivedAt = millis(); memcpy(p.data, data, size_t(len));
        if (!nq.push(p)) ++nqStats.dropped;
        else ++nqStats.queued;
        nqStats.queueHighWater = nq.highWater;
    }
    portEXIT_CRITICAL(&nqMux);
}
#if ESP_IDF_VERSION_MAJOR >= 5
void nowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    nowEnqueue(info ? info->src_addr : nullptr, data, len,
               info && info->rx_ctrl ? info->rx_ctrl->rssi : -127, info && info->rx_ctrl);
}
#else
void nowRecv(const uint8_t *mac, const uint8_t *data, int len) {
    nowEnqueue(mac, data, len, -127, false);
}
#endif
void stopNow() {
    portENTER_CRITICAL(&nqMux); nqAccepting = false; portEXIT_CRITICAL(&nqMux);
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    portENTER_CRITICAL(&nqMux); nq.clear(); portEXIT_CRITICAL(&nqMux);
}
} // namespace
bool nowBegin() {
    if (radio == R_NOW) return true;
    radioStop();
    portENTER_CRITICAL(&nqMux);
    nq.clear(); nqAccepting = false; nqStats = NowStatus{};
    nqStats.rssiAvailable = ESP_IDF_VERSION_MAJOR >= 5;
    portEXIT_CRITICAL(&nqMux);
    WiFi.persistent(false); WiFi.setAutoReconnect(false);
    if (!WiFi.mode(WIFI_STA)) { nqStats.lastError = ESP_FAIL; return false; }
    WiFi.disconnect(false, false); WiFi.setSleep(false);
    esp_err_t err = esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    bool initialised = false;
    if (err == ESP_OK) { err = esp_now_init(); initialised = err == ESP_OK; }
    if (err == ESP_OK) err = esp_now_register_recv_cb(nowRecv);
    if (err == ESP_OK) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, BCAST, 6); peer.channel = 1; peer.ifidx = WIFI_IF_STA;
        err = esp_now_add_peer(&peer);
    }
    if (err != ESP_OK) {
        if (initialised) { esp_now_unregister_recv_cb(); esp_now_deinit(); }
        WiFi.mode(WIFI_OFF); nqStats.lastError = err; return false;
    }
    radio = R_NOW;
    portENTER_CRITICAL(&nqMux); nqAccepting = true; portEXIT_CRITICAL(&nqMux);
    return true;
}
bool nowSend(const void *data, int len) {
    esp_err_t err = (radio == R_NOW && data && len > 0 && len <= 250)
        ? esp_now_send(BCAST, static_cast<const uint8_t *>(data), len) : ESP_ERR_INVALID_ARG;
    // Main-thread API admission is not a remote delivery acknowledgement.
    portENTER_CRITICAL(&nqMux);
    if (err == ESP_OK) ++nqStats.sent; else { ++nqStats.sendErrors; nqStats.lastError = err; }
    portEXIT_CRITICAL(&nqMux);
    return err == ESP_OK;
}
void nowMac(uint8_t mac[6]) { memset(mac, 0, 6); esp_wifi_get_mac(WIFI_IF_STA, mac); }
NowStatus nowStatus() {
    portENTER_CRITICAL(&nqMux);
    NowStatus s = nqStats; s.queueDepth = uint32_t(nq.count);
    portEXIT_CRITICAL(&nqMux);
    s.ready = radio == R_NOW; return s;
}
bool nowPoll(NowPkt &out) {
    bool got = false;
    portENTER_CRITICAL(&nqMux);
    got = nq.pop(out);
    portEXIT_CRITICAL(&nqMux);
    return got;
}

// ---- BLE -----------------------------------------------------------------
namespace {
std::vector<BleDev> bleFound;
bool bleScanning = false;
bool bleScanFailed = false;
BLEScan *bleScanner = nullptr;
SemaphoreHandle_t bleLock = nullptr; // onResult runs on the Bluedroid task
uint32_t bleWindowAt = 0;

#ifdef GRIM_CYD
constexpr uint32_t BLE_HEAP_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr size_t BLE_RESULT_LIMIT = 32;
#else
constexpr size_t BLE_RESULT_LIMIT = 256;
#endif

#ifdef GRIM_CYD_DIAGNOSTICS
void bleDiagnostic(const char *phase, int result) {
    hal::debugf("cyd ble %s result=%d heap=%u largest=%u", phase, result,
                (unsigned)heap_caps_get_free_size(BLE_HEAP_CAPS),
                (unsigned)heap_caps_get_largest_free_block(BLE_HEAP_CAPS));
}
#endif

void stopBleStack() {
    BLEDevice::deinit(false); // retain controller memory so BLE can be reentered
    // Arduino 3.3.9 marks BLEDevice initialized only after the last startup
    // step, and deinit() does nothing while that flag is false. A failed host
    // allocation/registration can therefore leave a partial SDK stack alive.
    // Also complete any partial teardown on Arduino 2, checking each status
    // rather than disabling an already stopped component.
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED)
        esp_bluedroid_disable();
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED)
        esp_bluedroid_deinit();
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
        esp_bt_controller_disable();
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED)
        esp_bt_controller_deinit();
}

void stopBle() {
#ifdef GRIM_CYD
    // Release the completion semaphore before closing the BLE host. Wait
    // until the host task has stopped before clearing its retained scanner.
    if (bleScanner && bleScanning) bleScanner->stop();
    stopBleStack();
    if (bleScanner) bleScanner->clearResults();
#else
    // Arduino 3 owns and destroys the scanner during deinit.
    stopBleStack();
#endif
    bleScanner = nullptr;
    bleScanning = false;
    if (bleLock) {
        xSemaphoreTake(bleLock, portMAX_DELAY);
        std::vector<BleDev>().swap(bleFound);
        xSemaphoreGive(bleLock);
    }
#ifdef GRIM_CYD_DIAGNOSTICS
    bleDiagnostic("stopped", esp_bt_controller_get_status());
#endif
}

#ifdef GRIM_CYD
bool startBleController() {
    // Match the prebuilt Arduino SDK's controller mode. Its btStart() spins
    // after a failed allocation, so handle startup errors explicitly.
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    // Kin and Sentinel scan without opening Bluetooth connections.
    cfg.ble_max_conn = 1;
    cfg.bt_max_acl_conn = 1;
    esp_err_t err = esp_bt_controller_init(&cfg);
#ifdef GRIM_CYD_DIAGNOSTICS
    bleDiagnostic("controller-init", err);
#endif
    if (err == ESP_OK) {
        err = esp_bt_controller_enable(static_cast<esp_bt_mode_t>(cfg.mode));
#ifdef GRIM_CYD_DIAGNOSTICS
        bleDiagnostic("controller-enable", err);
#endif
        if (err == ESP_OK) return true;
        esp_bt_controller_deinit();
    }
    hal::debugf("BLE controller startup: %s", esp_err_to_name(err));
    return false;
}

bool haveBleHostMemory() {
    // The CYD's Arduino 2.0.17 Bluedroid host used about 61 KiB at startup on
    // hardware and can dereference a null L2CAP allocation when memory is low.
    // Budget extra heap for scan activity and for the host's contiguous buffers.
    return heap_caps_get_free_size(BLE_HEAP_CAPS) >= 70 * 1024 &&
           heap_caps_get_largest_free_block(BLE_HEAP_CAPS) >= 16 * 1024;
}
#endif

class BleCb : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice d) override {
#ifdef GRIM_CYD
        if (heap_caps_get_free_size(BLE_HEAP_CAPS) < 6 * 1024) return;
#endif
        BleDev dev;
        dev.name = d.haveName() ? std::string(d.getName().c_str()) : std::string();
        dev.addr = std::string(d.getAddress().toString().c_str());
        dev.rssi = d.haveRSSI() ? d.getRSSI() : 0;
        dev.company = 0xFFFF;
        if (d.haveManufacturerData()) {
            const auto m = d.getManufacturerData();
            if (m.length() >= 2) dev.company = (uint8_t)m[0] | ((uint8_t)m[1] << 8);
        }
        dev.connectable = true;
        dev.lastSeen = millis();
        // Guard bleFound: this fires on the BLE host task while the main loop
        // reads/clears the same vector. A push_back reallocation under an
        // iterating reader corrupts the heap.
        if (!bleLock) return;
        xSemaphoreTake(bleLock, portMAX_DELAY);
        bool merged = false;
        for (auto &e : bleFound)
            if (e.addr == dev.addr) { e = dev; merged = true; break; }
        if (!merged && bleFound.size() < BLE_RESULT_LIMIT) bleFound.push_back(dev);
        xSemaphoreGive(bleLock);
    }
};
BleCb bleCb;
} // namespace

void bleBegin() {
    // Already on BLE (e.g. kin -> sentinel, or re-entering the rite): keep the
    // live stack instead of tearing it down and re-initialising, which both
    // wastes the ~4s warm-up and risked the deinit/init crash.
    if (radio == R_BLE && bleScanner) return;
    radioStop();
    // Release the previous radio's heap before allocating synchronization.
    // A failed allocation must not leave that previous mode running invisibly.
    if (!bleLock) bleLock = xSemaphoreCreateMutex();
    if (!bleLock) { bleScanFailed = true; return; }
    xSemaphoreTake(bleLock, portMAX_DELAY);
    bleFound.clear();
    bleScanning = false;
    bleScanFailed = false;
    xSemaphoreGive(bleLock);
#ifdef GRIM_CYD
    if (!startBleController()) return;
    if (!haveBleHostMemory()) {
#ifdef GRIM_CYD_DIAGNOSTICS
        bleDiagnostic("host-memory", ESP_ERR_NO_MEM);
#endif
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return;
    }
#endif
    BLEDevice::init("");
#if ESP_IDF_VERSION_MAJOR >= 5
    // This also catches errors after the host was enabled, before Arduino's
    // own initialized flag is published. Do not ask getScan() to allocate.
    if (!BLEDevice::getInitialized()) { bleScanFailed = true; stopBle(); return; }
#endif
#ifdef GRIM_CYD
    auto host = esp_bluedroid_get_status();
#ifdef GRIM_CYD_DIAGNOSTICS
    bleDiagnostic("host-status", host);
#endif
    if (host != ESP_BLUEDROID_STATUS_ENABLED) {
        stopBle();
        return;
    }
#endif
    bleScanner = BLEDevice::getScan();
    if (!bleScanner) { bleScanFailed = true; stopBle(); return; }
    bleScanner->setAdvertisedDeviceCallbacks(&bleCb, true);
    bleScanner->setActiveScan(true);
    bleScanner->setInterval(100);
    bleScanner->setWindow(90);
    radio = R_BLE;
}
int bleScan(std::vector<BleDev> &out) {
    if (!bleScanner || bleScanFailed) return BLE_UNAVAILABLE;
    uint32_t now = millis();
    if (!bleScanning) {
        xSemaphoreTake(bleLock, portMAX_DELAY);
        bleFound.clear();
        xSemaphoreGive(bleLock);
        bleScanning = bleScanner->start(4, nullptr, false); // non-blocking, 4s
        bleScanFailed = !bleScanning;
        bleWindowAt = now;
#ifdef GRIM_CYD_DIAGNOSTICS
        bleDiagnostic("scan-start", bleScanning);
#endif
        return bleScanFailed ? BLE_UNAVAILABLE : -1;
    }
    // hand back the live set and drop anything not seen for 12s, under the lock
    out.clear();
    xSemaphoreTake(bleLock, portMAX_DELAY);
    for (auto it = bleFound.begin(); it != bleFound.end();) {
        if (now - it->lastSeen >= 12000) it = bleFound.erase(it);
        else { out.push_back(*it); ++it; }
    }
    xSemaphoreGive(bleLock);
    // relaunch when the window ends
    if (now - bleWindowAt > 4500) {
        bleWindowAt = now;
#ifdef GRIM_CYD_DIAGNOSTICS
        bleDiagnostic("devices", out.size());
#endif
        bleScanner->stop();
        bleScanner->clearResults();
        bleScanning = bleScanner->start(4, nullptr, false);
        bleScanFailed = !bleScanning;
        if (bleScanFailed) return BLE_UNAVAILABLE;
    }
    return out.size();
}

// ---- LAN sweep (worker task) --------------------------------------------
namespace {
TaskHandle_t lanTask = nullptr;
volatile int lanPct = 0;
std::vector<Host> lanResult;
SemaphoreHandle_t lanLock = nullptr;
const uint16_t PORTS[] = {22, 53, 80, 443, 445, 1965, 3389, 8080};

bool tcpOpen(const char *ip, uint16_t port, int toMs) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    struct timeval tv = {toMs / 1000, (toMs % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    struct sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, ip, &a.sin_addr);
    // non-blocking connect with select for a tight timeout
    fcntl(s, F_SETFL, O_NONBLOCK);
    connect(s, (struct sockaddr *)&a, sizeof a);
    fd_set wf;
    FD_ZERO(&wf);
    FD_SET(s, &wf);
    bool ok = false;
    if (select(s + 1, nullptr, &wf, nullptr, &tv) > 0) {
        int err = 0;
        socklen_t len = sizeof err;
        getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len);
        ok = err == 0;
    }
    close(s);
    return ok;
}

void lanWorker(void *) {
    IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
    uint32_t base = (uint32_t)ip & (uint32_t)mask;
    // No raw ICMP from here, so a host counts as "up" if any common port
    // answers. Probe a couple of fast ports first, then the full list only on
    // hosts that replied. (A host with every port closed stays invisible.)
    const uint16_t PROBE[] = {80, 443, 22, 445, 53};
    for (int i = 1; i <= 254; ++i) {
        uint32_t hip = base | htonl(i);
        IPAddress h(hip);
        char ips[16];
        snprintf(ips, sizeof ips, "%s", h.toString().c_str());
        bool up = false;
        for (uint16_t p : PROBE)
            if (tcpOpen(ips, p, 80)) { up = true; break; }
        if (up) {
            Host host;
            host.ip = ips;
            host.up = true;
            for (uint16_t p : PORTS)
                if (tcpOpen(ips, p, 150)) host.ports.push_back(p);
            xSemaphoreTake(lanLock, portMAX_DELAY);
            lanResult.push_back(std::move(host));
            xSemaphoreGive(lanLock);
        }
        lanPct = i * 100 / 254;
    }
    lanPct = 100;
    lanTask = nullptr;
    vTaskDelete(nullptr);
}
} // namespace

void lanBegin() {
    if (lanTask) return;
    if (!lanLock) lanLock = xSemaphoreCreateMutex();
    lanPct = 0;
    lanResult.clear();
    if (!lanLock || xTaskCreatePinnedToCore(lanWorker, "grim-lan", 6144, nullptr, 3, &lanTask, 0) != pdPASS) {
        log_e("Could not start LAN scan");
        lanPct = 100;
    }
}
int lanProgress() { return lanPct; }
std::vector<Host> lanHosts() {
    std::vector<Host> r;
    if (lanLock) {
        xSemaphoreTake(lanLock, portMAX_DELAY);
        r = lanResult;
        xSemaphoreGive(lanLock);
    }
    return r;
}

// ---- Gemini (TLS worker) -------------------------------------------------
namespace {
TaskHandle_t gemTask = nullptr;
volatile GemState gState = GEM_IDLE;
volatile int gStatus = 0, gProg = 0;
String gMeta, gBody, gURL, gReq;
SemaphoreHandle_t gemLock = nullptr;

bool parseURL(const String &url, String &host, uint16_t &port, String &full) {
    full = url;
    if (!full.startsWith("gemini://")) full = "gemini://" + full;
    String rest = full.substring(9);
    int slash = rest.indexOf('/');
    String hostport = slash < 0 ? rest : rest.substring(0, slash);
    int colon = hostport.indexOf(':');
    if (colon < 0) {
        host = hostport;
        port = 1965;
    } else {
        host = hostport.substring(0, colon);
        port = hostport.substring(colon + 1).toInt();
    }
    return host.length() > 0;
}

void gemFetch() {
    String url = gReq;
    for (int redirect = 0; redirect < 5 && gState == GEM_FETCHING; ++redirect) {
        if (WiFi.status() != WL_CONNECTED) {
            gStatus = 0;
            xSemaphoreTake(gemLock, portMAX_DELAY);
            gMeta = "Wi-Fi is not connected";
            xSemaphoreGive(gemLock);
            gState = GEM_ERROR;
            break;
        }
        String host, full;
        uint16_t port;
        if (!parseURL(url, host, port, full)) {
            gStatus = 0;
            xSemaphoreTake(gemLock, portMAX_DELAY);
            gMeta = "bad url";
            xSemaphoreGive(gemLock);
            gState = GEM_ERROR;
            break;
        }
        WiFiClientSecure client;
        client.setInsecure(); // Gemini is trust-on-first-use; we don't pin
        client.setTimeout(8);
        client.setHandshakeTimeout(8);
        gProg = 0;
        if (!client.connect(host.c_str(), port)) {
            gStatus = 0;
            xSemaphoreTake(gemLock, portMAX_DELAY);
            gMeta = "no route to " + host;
            xSemaphoreGive(gemLock);
            gState = GEM_ERROR;
            break;
        }
        client.print(full + "\r\n");
        String header = client.readStringUntil('\n');
        header.trim();
        if (header.length() == 0) {
            gStatus = 0;
            xSemaphoreTake(gemLock, portMAX_DELAY);
            gMeta = "no Gemini response (TLS/port 1965 timeout)";
            gURL = full;
            xSemaphoreGive(gemLock);
            gState = GEM_ERROR;
            client.stop();
            break;
        }
        int sp = header.indexOf(' ');
        int code = (sp > 0 ? header.substring(0, sp) : header).toInt();
        String meta = sp > 0 ? header.substring(sp + 1) : "";
        xSemaphoreTake(gemLock, portMAX_DELAY);
        gStatus = code;
        gMeta = meta;
        gURL = full;
        xSemaphoreGive(gemLock);
        int klass = code / 10;
        if (klass == 3) { // redirect
            url = meta.startsWith("gemini://") || meta.indexOf("://") > 0 ? meta : "gemini://" + host + meta;
            client.stop();
            continue;
        }
        if (klass == 1) { // input
            gState = GEM_INPUT;
            client.stop();
            break;
        }
        if (klass != 2) { // 4x/5x/6x
            gState = GEM_ERROR;
            client.stop();
            break;
        }
        String body;
        body.reserve(8192);
        uint32_t cap = 32 * 1024;
        uint32_t lastDataAt = millis();
        char chunk[512];
        // Gemini signals end-of-body by closing the connection, so keep draining
        // while bytes remain buffered even after connected() flips false —
        // otherwise the final chunk is dropped and the page renders short.
        while (gState == GEM_FETCHING && (client.connected() || client.available() > 0) && body.length() < cap) {
            if (millis() - lastDataAt >= 8000) {
                xSemaphoreTake(gemLock, portMAX_DELAY);
                gMeta = "body read timed out";
                gState = GEM_ERROR;
                xSemaphoreGive(gemLock);
                return;
            }
            int avail = client.available();
            if (avail > 0) {
                int want = avail > (int)sizeof chunk ? (int)sizeof chunk : avail;
                if (body.length() + want > cap) want = cap - body.length();
                int got = client.read((uint8_t *)chunk, want);
                if (got > 0) {
                    if (!body.concat(chunk, got)) break;
                    gProg = body.length();
                    lastDataAt = millis();
                }
            } else {
                vTaskDelay(1);
            }
        }
        client.stop();
        xSemaphoreTake(gemLock, portMAX_DELAY);
        if (gState == GEM_FETCHING) {
            gBody = std::move(body);
            gState = GEM_DONE;
        }
        xSemaphoreGive(gemLock);
        break;
    }
    if (gState == GEM_FETCHING) {
        xSemaphoreTake(gemLock, portMAX_DELAY);
        gMeta = "too many redirects";
        gState = GEM_ERROR;
        xSemaphoreGive(gemLock);
    }
}

void gemWorker(void *) {
    gemFetch(); // destroy request and TLS resources before deleting this task
    gemTask = nullptr;
    vTaskDelete(nullptr);
}
} // namespace

void gemGet(const char *url) {
    if (gemTask) return;
    if (!gemLock) gemLock = xSemaphoreCreateMutex();
    if (!gemLock) {
        gState = GEM_ERROR;
        log_e("Could not allocate Gemini request lock");
        return;
    }
    gReq = url;
    gBody = "";
    gMeta = "";
    gStatus = 0;
    gProg = 0;
    gState = GEM_FETCHING;
    if (xTaskCreatePinnedToCore(gemWorker, "grim-gem", 12288, nullptr, 4, &gemTask, 1) != pdPASS) {
        gMeta = "low memory";
        gState = GEM_ERROR;
    }
}
void gemStop() { gState = GEM_IDLE; }
GemState gemState() { return gState; }
int gemStatus() { return gStatus; }
int gemProgress() { return gProg; }
std::string gemMeta() {
    std::string s;
    if (gemLock) { xSemaphoreTake(gemLock, portMAX_DELAY); s = gMeta.c_str(); xSemaphoreGive(gemLock); }
    return s;
}
std::string gemBody() {
    std::string s;
    if (gemLock) { xSemaphoreTake(gemLock, portMAX_DELAY); s = gBody.c_str(); xSemaphoreGive(gemLock); }
    return s;
}
std::string gemURL() {
    std::string s;
    if (gemLock) { xSemaphoreTake(gemLock, portMAX_DELAY); s = gURL.c_str(); xSemaphoreGive(gemLock); }
    return s;
}

// ---- CSI motion sense ----------------------------------------------------
namespace {
portMUX_TYPE csiMux = portMUX_INITIALIZER_UNLOCKED;
float gMotion = 0;
float gBins[64];
int gBinCount = 0;
bool csiOn = false;
uint32_t csiSampleAt = 0;
float csiAmpMean = 0;
#ifdef GRIM_CYD
wifi_ap_record_t csiAP = {};
bool csiPromiscuous = false;
#endif

void csiCb(void *bssid, wifi_csi_info_t *info) {
    if (!info || !info->buf || (bssid && memcmp(bssid, info->mac, 6) != 0)) return;
    const int first = info->first_word_invalid ? 2 : 0;
    const int pairs = std::min<int>(info->len / 2, 64);
    if (pairs <= first) return;
    float amps[64];
    float total = 0;
    for (int i = first; i < pairs; ++i) {
        float im = info->buf[i * 2], re = info->buf[i * 2 + 1];
        amps[i] = sqrtf(re * re + im * im);
        total += amps[i];
    }

    portENTER_CRITICAL(&csiMux);
    if (csiOn) {
        for (int i = first; i < pairs; ++i)
            gBins[i] = gBins[i] * 0.7f + (amps[i] / 128.f) * 0.3f;
        float amp = total / (pairs - first);
        if (!gBinCount) csiAmpMean = amp;
        float dev = fabsf(amp - csiAmpMean);
        csiAmpMean = csiAmpMean * 0.98f + amp * 0.02f;
        gMotion = gMotion * 0.9f + fminf(1.f, dev / 24.f) * 0.1f;
        gBinCount = pairs;
        csiSampleAt = millis();
    }
    portEXIT_CRITICAL(&csiMux);
}
} // namespace

bool csiBegin() {
    void *bssid = nullptr;
#ifdef GRIM_CYD
    // Keep the station associated. Listen on its own channel for more CSI
    // samples when the app is idle, but measure the connected AP's signal.
    if (esp_wifi_sta_get_ap_info(&csiAP) != ESP_OK) return false;
    bssid = csiAP.bssid;
    csiPromiscuous = esp_wifi_set_promiscuous(true) == ESP_OK;
#endif
    wifi_csi_config_t cfg = {};
    cfg.lltf_en = true;
    cfg.htltf_en = true;
    cfg.stbc_htltf2_en = true;
    cfg.ltf_merge_en = true;
    cfg.channel_filter_en = true;
    cfg.manu_scale = false;
    esp_err_t err = esp_wifi_set_csi_config(&cfg);
    if (err == ESP_OK) err = esp_wifi_set_csi_rx_cb(&csiCb, bssid);
    if (err == ESP_OK) err = esp_wifi_set_csi(true);
    if (err != ESP_OK) {
        log_e("CSI startup: %s", esp_err_to_name(err));
        csiEnd();
        return false;
    }
    portENTER_CRITICAL(&csiMux);
    memset(gBins, 0, sizeof gBins);
    gBinCount = 0;
    gMotion = 0;
    csiOn = true;
    portEXIT_CRITICAL(&csiMux);
    return true;
}
void csiEnd() {
    portENTER_CRITICAL(&csiMux);
    csiOn = false;
    portEXIT_CRITICAL(&csiMux);
    esp_wifi_set_csi(false);
    esp_wifi_set_csi_rx_cb(nullptr, nullptr);
#ifdef GRIM_CYD
    if (csiPromiscuous) esp_wifi_set_promiscuous(false);
    csiPromiscuous = false;
#endif
}
int csiRead(float &motion, float *bins, int nbins) {
    portENTER_CRITICAL(&csiMux);
    bool fresh = csiOn && gBinCount && millis() - csiSampleAt < 3000;
    motion = fresh ? gMotion : 0;
    int n = fresh ? std::min(gBinCount, nbins) : -1;
    for (int i = 0; i < n; ++i) bins[i] = gBins[i];
    portEXIT_CRITICAL(&csiMux);
    return n;
}

#ifndef GRIM_CYD
// ---- host probe ----------------------------------------------------------
namespace {
TaskHandle_t probeTask = nullptr;
volatile int probePct = 0;
volatile bool probeFinished = false;
std::vector<ProbeLine> probeOut;
SemaphoreHandle_t probeLock = nullptr;
String probeIP;
uint16_t probePort;

void addLine(const String &t, uint8_t kind) {
    xSemaphoreTake(probeLock, portMAX_DELAY);
    probeOut.push_back({t.c_str(), kind});
    xSemaphoreGive(probeLock);
}

// minimal HTTP request; returns status code, fills server/title if provided
int httpReq(const String &ip, uint16_t port, const String &path, const String &authB64, String *serverHdr) {
    WiFiClient c;
    if (!c.connect(ip.c_str(), port, 4000)) return -1;
    String req = "GET " + path + " HTTP/1.1\r\nHost: " + ip + "\r\nUser-Agent: grimoire\r\n";
    if (authB64.length()) req += "Authorization: Basic " + authB64 + "\r\n";
    req += "Connection: close\r\n\r\n";
    c.print(req);
    uint32_t t0 = millis();
    String status;
    int code = 0;
    while (c.connected() && millis() - t0 < 4000) {
        if (c.available()) {
            String line = c.readStringUntil('\n');
            if (code == 0 && line.startsWith("HTTP/")) {
                int sp = line.indexOf(' ');
                code = line.substring(sp + 1, sp + 4).toInt();
            }
            if (serverHdr && line.startsWith("Server:")) *serverHdr = line.substring(7);
            if (line == "\r" || line.length() == 0) break;
        }
    }
    c.stop();
    return code;
}

// tiny base64 for the default-cred checks
String b64(const String &in) {
    static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String o;
    int i = 0;
    while (i < (int)in.length()) {
        uint32_t n = (uint8_t)in[i++] << 16;
        if (i < (int)in.length()) n |= (uint8_t)in[i++] << 8;
        if (i < (int)in.length()) n |= (uint8_t)in[i++];
        o += T[(n >> 18) & 63];
        o += T[(n >> 12) & 63];
        o += (i > (int)in.length() + 1) ? '=' : T[(n >> 6) & 63];
        o += (i > (int)in.length()) ? '=' : T[n & 63];
    }
    return o;
}

const char *PATHS[] = {"/", "/admin", "/login", "/index.html", "/config", "/setup.cgi", "/api",
                       "/.git/HEAD", "/.env", "/wp-login.php", "/phpmyadmin", "/cgi-bin/",
                       "/status", "/server-status", "/robots.txt"};
const char *CREDS[][2] = {{"admin", "admin"},   {"admin", "password"}, {"admin", ""},
                          {"root", "root"},     {"admin", "1234"},     {"user", "user"},
                          {"admin", "12345"},   {"root", "admin"},     {"admin", "admin123"},
                          {"guest", "guest"}};

void probeWorker(void *) {
    String ip = probeIP;
    uint16_t port = probePort;
    addLine("probing " + ip + ":" + String(port), 0);
    String server;
    int root = httpReq(ip, port, "/", "", &server);
    if (root < 0) {
        addLine("no HTTP response", 3);
        probePct = 100;
        probeFinished = true;
        probeTask = nullptr;
        vTaskDelete(nullptr);
    }
    if (server.length()) addLine("Server:" + server, 1);
    int np = sizeof(PATHS) / sizeof(PATHS[0]);
    bool needsAuth = false;
    for (int i = 0; i < np; ++i) {
        int code = httpReq(ip, port, PATHS[i], "", nullptr);
        if (code == 200) addLine(String(PATHS[i]) + "  200 OK", 1);
        else if (code == 401 || code == 403) { addLine(String(PATHS[i]) + "  " + code + " auth", 1); needsAuth = true; }
        else if (code == 301 || code == 302) addLine(String(PATHS[i]) + "  " + code + " ->", 0);
        probePct = (i + 1) * 70 / np;
    }
    // default-cred check against the root if it wanted auth
    if (needsAuth) {
        addLine("trying default creds...", 0);
        int nc = sizeof(CREDS) / sizeof(CREDS[0]);
        for (int i = 0; i < nc; ++i) {
            String auth = b64(String(CREDS[i][0]) + ":" + CREDS[i][1]);
            int code = httpReq(ip, port, "/", auth, nullptr);
            if (code == 200) {
                addLine(String("DEFAULT CRED: ") + CREDS[i][0] + ":" + CREDS[i][1], 2);
                break;
            }
            probePct = 70 + (i + 1) * 30 / nc;
        }
    }
    probePct = 100;
    addLine("done", 0);
    probeFinished = true;
    probeTask = nullptr;
    vTaskDelete(nullptr);
}
} // namespace

void probeBegin(const char *ip, uint16_t port) {
    if (probeTask) return;
    if (!probeLock) probeLock = xSemaphoreCreateMutex();
    xSemaphoreTake(probeLock, portMAX_DELAY);
    probeOut.clear();
    xSemaphoreGive(probeLock);
    probeIP = ip;
    probePort = port;
    probePct = 0;
    probeFinished = false;
    xTaskCreatePinnedToCore(probeWorker, "grim-probe", 8192, nullptr, 4, &probeTask, 1);
}
int probeProgress() { return probePct; }
bool probeDone() { return probeFinished; }
std::vector<ProbeLine> probeResults() {
    std::vector<ProbeLine> r;
    if (probeLock) {
        xSemaphoreTake(probeLock, portMAX_DELAY);
        r = probeOut;
        xSemaphoreGive(probeLock);
    }
    return r;
}
#endif

// ---- captive portal ------------------------------------------------------
namespace {
TaskHandle_t capTask = nullptr;
volatile PortalState capState = PORTAL_NONE;
String capURL;
SemaphoreHandle_t capLock = nullptr;
bool capDoAccept = false;

// Plain-HTTP GET; returns the status code, captures a Location header and
// whether any body followed the headers. Breaks early once it knows enough.
int httpHead(const String &host, uint16_t port, const String &path, String &loc, bool &hasBody) {
    WiFiClient c;
    loc = "";
    hasBody = false;
    if (!c.connect(host.c_str(), port, 4000)) return -1;
    c.print("GET " + path + " HTTP/1.1\r\nHost: " + host +
            "\r\nUser-Agent: grimoire\r\nConnection: close\r\n\r\n");
    uint32_t t0 = millis();
    int code = 0;
    bool inHeaders = true;
    while (c.connected() && millis() - t0 < 5000) {
        if (!c.available()) { vTaskDelay(1); continue; }
        String line = c.readStringUntil('\n');
        if (inHeaders) {
            if (code == 0 && line.startsWith("HTTP/")) {
                int sp = line.indexOf(' ');
                code = line.substring(sp + 1, sp + 4).toInt();
            }
            String lo = line;
            lo.toLowerCase();
            if (lo.startsWith("location:")) { loc = line.substring(9); loc.trim(); }
            if (line == "\r" || line.length() == 0) inHeaders = false;
        } else {
            if (line.length() && line != "\r") { hasBody = true; break; }
        }
    }
    c.stop();
    return code;
}

bool parseHttpURL(const String &url, String &host, uint16_t &port, String &path) {
    String u = url;
    port = 80;
    if (u.startsWith("http://")) u = u.substring(7);
    else if (u.startsWith("https://")) return false; // portal over TLS: can't drive it here
    int slash = u.indexOf('/');
    String hp = slash < 0 ? u : u.substring(0, slash);
    path = slash < 0 ? "/" : u.substring(slash);
    int colon = hp.indexOf(':');
    if (colon < 0) host = hp;
    else { host = hp.substring(0, colon); port = hp.substring(colon + 1).toInt(); }
    return host.length() > 0;
}

// 1 = real internet, 0 = captive (walled garden), -1 = failed.
int probe204() {
    String loc;
    bool body;
    int code = httpHead("connectivitycheck.gstatic.com", 80, "/generate_204", loc, body);
    if (code == 204) return 1;
    if ((code >= 300 && code < 400) || code == 200) {
        if (loc.length()) { xSemaphoreTake(capLock, portMAX_DELAY); capURL = loc; xSemaphoreGive(capLock); }
        return 0;
    }
    // couldn't reach the Google endpoint; try Apple's as a second opinion
    code = httpHead("captive.apple.com", 80, "/hotspot-detect.html", loc, body);
    if (code == 200 && !body) return 1;
    if (code == 200 || (code >= 300 && code < 400)) {
        if (loc.length()) { xSemaphoreTake(capLock, portMAX_DELAY); capURL = loc; xSemaphoreGive(capLock); }
        return 0;
    }
    return -1;
}

// GET a plain-HTTP URL and follow one redirect hop. Best-effort click-through
// for a simple walled garden; it cannot drive a portal that needs a form POST
// or runs over JS/HTTPS.
void knock(const String &url) {
    String h, pa;
    uint16_t po;
    if (!parseHttpURL(url, h, po, pa)) return;
    String loc;
    bool b;
    httpHead(h, po, pa, loc, b);
    if (loc.length() && parseHttpURL(loc, h, po, pa)) httpHead(h, po, pa, loc, b);
}

void capWorker(void *) {
    if (capDoAccept) {
        String url;
        xSemaphoreTake(capLock, portMAX_DELAY);
        url = capURL;
        xSemaphoreGive(capLock);
        if (url.length()) knock(url); // the login URL the portal pointed us at
        // Fall back to the gateway root: most walled gardens intercept any
        // plain-HTTP GET to the router and serve the login page there, even
        // when the probe got no Location header back (so capURL was empty).
        IPAddress gw = WiFi.gatewayIP();
        if ((uint32_t)gw) knock("http://" + gw.toString() + "/");
        capDoAccept = false;
    }
    int r = probe204();
    capState = r == 1 ? PORTAL_ONLINE : r == 0 ? PORTAL_CAPTIVE : PORTAL_FAILED;
    capTask = nullptr;
    vTaskDelete(nullptr);
}

void capStart(bool accept) {
    if (capTask) return;
    if (!capLock) capLock = xSemaphoreCreateMutex();
    if (!capLock || WiFi.status() != WL_CONNECTED) { capState = PORTAL_FAILED; return; }
    capDoAccept = accept;
    capState = accept ? PORTAL_JOINING : PORTAL_CHECKING;
    if (xTaskCreatePinnedToCore(capWorker, "grim-cap", 6144, nullptr, 3, &capTask, 1) != pdPASS)
        capState = PORTAL_FAILED;
}
} // namespace

void captiveCheck() { capStart(false); }
void captiveAccept() { capStart(true); }
PortalState captiveState() { return capState; }
std::string captiveURL() {
    std::string s;
    if (capLock) { xSemaphoreTake(capLock, portMAX_DELAY); s = capURL.c_str(); xSemaphoreGive(capLock); }
    return s;
}

} // namespace net
#endif
