// Checked asynchronous IDF APIs from the pinned Arduino 2.0.17 / IDF 4.4.7.
// BLEDevice's convenience connect waits without a caller timeout and its
// write/notify wrappers return void; this adapter needs bounded setup and the
// actual API admission status. No Arduino BLE callbacks are replaced while
// another initialized Bluedroid owner exists.
#ifdef GRIM_PERF_LINK
#include "perf_ble.h"
#include <Arduino.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gatt_common_api.h>
#include <esp_gattc_api.h>
#include <esp_gatts_api.h>
#include <esp_timer.h>
#include <atomic>
#include <cstring>
#include <algorithm>

namespace perf_ble {
namespace {
constexpr uint16_t AppId = 0x517E;
static_assert(AppId <= ESP_APP_ID_MAX, "GATT application ID must fit the pinned SDK range");
constexpr uint64_t SetupTimeoutUs = 20000000;
enum class Kind : uint8_t {
    ServerRegistered, AttributesCreated, ServiceStarted, Connected, Disconnected,
    Mtu, AdvDataReady, WhitelistReady, AdvStarted, ClientRegistered, ClientOpened,
    ServiceCacheReady, SearchResult, SearchDone, NotifyRegistered, DescriptorWritten, WriteFailed,
    Congestion
};
struct Event {
    Kind kind{};
    int status = 0;
    esp_gatt_if_t interface = ESP_GATT_IF_NONE;
    uint16_t conn = 0, a = 0, b = 0, handles[4]{};
    uint8_t peer[6]{};
    bool flag = false;
};
struct Received {
    uint16_t length = 0, conn = 0, handle = 0, offset = 0;
    uint8_t peer[6]{}, bytes[MaxFrame]{};
    bool prepared = false, notification = false;
};
QueueHandle_t events = nullptr, received = nullptr;
std::atomic<bool> callbackEnabled{false};
std::atomic<uint32_t> rxDrops{0}, eventDrops{0};
Info state;
RxFn receiveFn = nullptr;
uint8_t expectedPeer[6]{};
esp_gatt_if_t gattIf = ESP_GATT_IF_NONE;
uint16_t connection = 0, handles[4]{}, serviceStart = 0, serviceEnd = 0;
uint16_t valueHandle = 0, descriptorHandle = 0;
bool ownController = false, ownBluedroid = false, serviceReady = false;
bool advDataReady = false, whitelistReady = false, advertisingRequested = false;
bool mtuExchanged = false, serviceCacheReady = false, serviceSearchRequested = false;
uint64_t setupStarted = 0;
uint8_t serviceBytes[16] = {0x01,0x8d,0xe7,0xa8,0x25,0xb7,0x4b,0x8e,0x0e,0x4c,0x68,0x4f,0xe1,0x72,0xe8,0xc1};
uint8_t characteristicBytes[16] = {0x02,0x8d,0xe7,0xa8,0x25,0xb7,0x4b,0x8e,0x0e,0x4c,0x68,0x4f,0xe1,0x72,0xe8,0xc1};
uint16_t primaryUuid = ESP_GATT_UUID_PRI_SERVICE, declarationUuid = ESP_GATT_UUID_CHAR_DECLARE, cccdUuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
uint8_t properties = ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
uint8_t attributeValue[MaxFrame]{}, cccdValue[2]{};
esp_gatts_attr_db_t attributes[4]{};
uint8_t advertisingData[27]{};

void failure(int code, const char *stage) { state.last_error = code; state.stage = stage; state.ready = false; }
bool checked(int code, const char *stage) { if (code != ESP_OK) { failure(code, stage); return false; } return true; }
bool samePeer(const uint8_t *peer) { return std::memcmp(peer, expectedPeer, 6) == 0; }
void enqueue(const Event &event) {
    if (!callbackEnabled.load(std::memory_order_acquire)) return;
    if (xQueueSend(events, &event, 0) != pdTRUE) eventDrops.fetch_add(1, std::memory_order_relaxed);
}
void enqueueReceived(uint16_t conn, uint16_t handle, const uint8_t *peer,
                     const uint8_t *data, size_t length, uint16_t offset, bool prepared, bool notification) {
    if (!callbackEnabled.load(std::memory_order_acquire)) return;
    if (!data || length > MaxFrame) { rxDrops.fetch_add(1, std::memory_order_relaxed); return; }
    Received item;
    item.conn = conn; item.handle = handle; item.length = uint16_t(length); item.offset = offset;
    item.prepared = prepared; item.notification = notification;
    std::memcpy(item.peer, peer, 6); std::memcpy(item.bytes, data, length);
    if (xQueueSend(received, &item, 0) != pdTRUE) rxDrops.fetch_add(1, std::memory_order_relaxed);
}
// Callbacks only copy bounded metadata/data into queues. They perform no GATT
// operation, packet/CRC computation, echo, heap allocation or UART output.
void gapCallback(esp_gap_ble_cb_event_t kind, esp_ble_gap_cb_param_t *parameter) {
    Event event;
    switch (kind) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        event.kind = Kind::AdvDataReady; event.status = parameter->adv_data_raw_cmpl.status; break;
    case ESP_GAP_BLE_UPDATE_WHITELIST_COMPLETE_EVT:
        event.kind = Kind::WhitelistReady; event.status = parameter->update_whitelist_cmpl.status; break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        event.kind = Kind::AdvStarted; event.status = parameter->adv_start_cmpl.status; break;
    default: return;
    }
    enqueue(event);
}
void serverCallback(esp_gatts_cb_event_t kind, esp_gatt_if_t interface, esp_ble_gatts_cb_param_t *p) {
    Event event; event.interface = interface;
    switch (kind) {
    case ESP_GATTS_REG_EVT:
        if (p->reg.app_id != AppId) return;
        event.kind = Kind::ServerRegistered; event.status = p->reg.status; break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        event.kind = Kind::AttributesCreated; event.status = p->add_attr_tab.status; event.a = p->add_attr_tab.num_handle;
        if (p->add_attr_tab.status == ESP_GATT_OK && p->add_attr_tab.num_handle == 4 && p->add_attr_tab.handles)
            std::memcpy(event.handles, p->add_attr_tab.handles, sizeof(event.handles)); break;
    case ESP_GATTS_START_EVT:
        event.kind = Kind::ServiceStarted; event.status = p->start.status; break;
    case ESP_GATTS_CONNECT_EVT:
        event.kind = Kind::Connected; event.conn = p->connect.conn_id; std::memcpy(event.peer, p->connect.remote_bda, 6); break;
    case ESP_GATTS_DISCONNECT_EVT:
        event.kind = Kind::Disconnected; event.conn = p->disconnect.conn_id; event.status = p->disconnect.reason; break;
    case ESP_GATTS_MTU_EVT:
        event.kind = Kind::Mtu; event.conn = p->mtu.conn_id; event.a = p->mtu.mtu; break;
    case ESP_GATTS_CONGEST_EVT:
        event.kind = Kind::Congestion; event.conn = p->congest.conn_id; event.flag = p->congest.congested; break;
    case ESP_GATTS_WRITE_EVT:
        enqueueReceived(p->write.conn_id, p->write.handle, p->write.bda, p->write.value,
                        p->write.len, p->write.offset, p->write.is_prep, false); return;
    default: return;
    }
    enqueue(event);
}
void clientCallback(esp_gattc_cb_event_t kind, esp_gatt_if_t interface, esp_ble_gattc_cb_param_t *p) {
    Event event; event.interface = interface;
    switch (kind) {
    case ESP_GATTC_REG_EVT:
        if (p->reg.app_id != AppId) return;
        event.kind = Kind::ClientRegistered; event.status = p->reg.status; break;
    case ESP_GATTC_OPEN_EVT:
        event.kind = Kind::ClientOpened; event.conn = p->open.conn_id; event.status = p->open.status; event.a = p->open.mtu;
        std::memcpy(event.peer, p->open.remote_bda, 6); break;
    case ESP_GATTC_DISCONNECT_EVT:
        event.kind = Kind::Disconnected; event.conn = p->disconnect.conn_id; event.status = p->disconnect.reason; break;
    case ESP_GATTC_CFG_MTU_EVT:
        event.kind = Kind::Mtu; event.conn = p->cfg_mtu.conn_id; event.status = p->cfg_mtu.status; event.a = p->cfg_mtu.mtu; break;
    case ESP_GATTC_DIS_SRVC_CMPL_EVT:
        event.kind = Kind::ServiceCacheReady; event.conn = p->dis_srvc_cmpl.conn_id; event.status = p->dis_srvc_cmpl.status; break;
    case ESP_GATTC_SEARCH_RES_EVT:
        event.kind = Kind::SearchResult; event.conn = p->search_res.conn_id;
        event.a = p->search_res.start_handle; event.b = p->search_res.end_handle; break;
    case ESP_GATTC_SEARCH_CMPL_EVT:
        event.kind = Kind::SearchDone; event.conn = p->search_cmpl.conn_id; event.status = p->search_cmpl.status; break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        event.kind = Kind::NotifyRegistered; event.a = p->reg_for_notify.handle; event.status = p->reg_for_notify.status; break;
    case ESP_GATTC_WRITE_DESCR_EVT:
        event.kind = Kind::DescriptorWritten; event.conn = p->write.conn_id; event.a = p->write.handle; event.status = p->write.status; break;
    case ESP_GATTC_WRITE_CHAR_EVT:
        if (p->write.status == ESP_GATT_OK) return;
        event.kind = Kind::WriteFailed; event.conn = p->write.conn_id; event.status = p->write.status; break;
    case ESP_GATTC_CONGEST_EVT:
        event.kind = Kind::Congestion; event.conn = p->congest.conn_id; event.flag = p->congest.congested; break;
    case ESP_GATTC_NOTIFY_EVT:
        enqueueReceived(p->notify.conn_id, p->notify.handle, p->notify.remote_bda, p->notify.value,
                        p->notify.value_len, 0, false, p->notify.is_notify); return;
    default: return;
    }
    enqueue(event);
}
void attribute(unsigned index, uint16_t uuidLength, uint8_t *uuid, uint16_t permission,
               uint16_t maximum, uint16_t length, uint8_t *value) {
    auto &a = attributes[index]; a.attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
    a.att_desc.uuid_length = uuidLength; a.att_desc.uuid_p = uuid; a.att_desc.perm = permission;
    a.att_desc.max_length = maximum; a.att_desc.length = length; a.att_desc.value = value;
}
void prepareAttributes() {
    std::memset(cccdValue, 0, sizeof(cccdValue));
    attribute(0, ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&primaryUuid), ESP_GATT_PERM_READ, 16, 16, serviceBytes);
    attribute(1, ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&declarationUuid), ESP_GATT_PERM_READ, 1, 1, &properties);
    attribute(2, ESP_UUID_LEN_128, characteristicBytes, ESP_GATT_PERM_WRITE, MaxFrame, 0, attributeValue);
    attribute(3, ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&cccdUuid), ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 2, cccdValue);
}
esp_bt_uuid_t uuid(const uint8_t *bytes) {
    esp_bt_uuid_t value{}; value.len = ESP_UUID_LEN_128; std::memcpy(value.uuid.uuid128, bytes, 16); return value;
}
void updateReadiness() {
    state.ready = state.initialized && state.connected && serviceReady && state.notifications_enabled && state.mtu >= 43 && !eventDrops.load();
    if (state.ready) state.stage = "ready";
}
void requestConnectionParameters() {
    esp_ble_conn_update_params_t parameters{};
    std::memcpy(parameters.bda, expectedPeer, 6);
    parameters.min_int = state.requested_min_interval; parameters.max_int = state.requested_max_interval;
    parameters.latency = state.requested_latency; parameters.timeout = state.requested_timeout;
    state.connection_param_status = esp_ble_gap_update_conn_params(&parameters);
}
void maybeAdvertise() {
    if (!state.server || !serviceReady || !advDataReady || !whitelistReady || advertisingRequested) return;
    esp_ble_adv_params_t parameters{};
    parameters.adv_int_min = 0x20; parameters.adv_int_max = 0x40;
    parameters.adv_type = ADV_TYPE_IND; parameters.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    parameters.channel_map = ADV_CHNL_ALL;
    parameters.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_WLST_CON_WLST;
    if (checked(esp_ble_gap_start_advertising(&parameters), "advertising_start_request")) advertisingRequested = true;
}
void discoverCharacteristic() {
    if (!serviceStart || !serviceEnd) { failure(ESP_ERR_NOT_FOUND, "known_service_not_found"); return; }
    esp_gattc_char_elem_t characteristic{}; uint16_t count = 1;
    const auto requested = uuid(characteristicBytes);
    if (!checked(esp_ble_gattc_get_char_by_uuid(gattIf, connection, serviceStart, serviceEnd, requested, &characteristic, &count), "known_characteristic_lookup") || count != 1) {
        if (!state.last_error) failure(ESP_ERR_NOT_FOUND, "known_characteristic_not_found"); return;
    }
    if (!(characteristic.properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) || !(characteristic.properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY)) {
        failure(ESP_ERR_NOT_SUPPORTED, "characteristic_properties"); return;
    }
    valueHandle = characteristic.char_handle;
    esp_gattc_descr_elem_t descriptor{}; count = 1;
    esp_bt_uuid_t cccd{}; cccd.len = ESP_UUID_LEN_16; cccd.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
    if (!checked(esp_ble_gattc_get_descr_by_char_handle(gattIf, connection, valueHandle, cccd, &descriptor, &count), "notification_descriptor_lookup") || count != 1) {
        if (!state.last_error) failure(ESP_ERR_NOT_FOUND, "notification_descriptor_not_found"); return;
    }
    descriptorHandle = descriptor.handle; serviceReady = true;
    checked(esp_ble_gattc_register_for_notify(gattIf, expectedPeer, valueHandle), "notification_registration_request");
}
void maybeSearchService() {
    // The pinned IDF performs automatic cache discovery on connection. Its
    // example searches only after DIS_SRVC_CMPL; also finish MTU exchange
    // before submitting another GATT procedure. Call exactly once per setup.
    if (state.server || !state.connected || !mtuExchanged || !serviceCacheReady || serviceSearchRequested) return;
    auto requested = uuid(serviceBytes);
    serviceSearchRequested = checked(esp_ble_gattc_search_service(gattIf, connection, &requested), "known_service_discovery_request");
}
void process(const Event &event) {
    if (event.kind == Kind::WriteFailed) { ++state.tx_failures; state.last_error = event.status; return; }
    if (event.status && event.kind != Kind::Disconnected) { failure(event.status, "asynchronous_GATT_or_GAP_status"); return; }
    switch (event.kind) {
    case Kind::ServerRegistered:
        gattIf = event.interface;
        checked(esp_ble_gatts_create_attr_tab(attributes, gattIf, 4, 0), "attribute_table_request"); break;
    case Kind::AttributesCreated:
        if (event.a != 4) { failure(ESP_ERR_INVALID_SIZE, "attribute_table_count"); break; }
        std::memcpy(handles, event.handles, sizeof(handles)); valueHandle = handles[2]; descriptorHandle = handles[3];
        checked(esp_ble_gatts_start_service(handles[0]), "service_start_request"); break;
    case Kind::ServiceStarted: serviceReady = true; maybeAdvertise(); break;
    case Kind::AdvDataReady: advDataReady = true; maybeAdvertise(); break;
    case Kind::WhitelistReady: whitelistReady = true; maybeAdvertise(); break;
    case Kind::AdvStarted: state.stage = "advertising_known_peer_only"; break;
    case Kind::ClientRegistered:
        gattIf = event.interface; state.stage = "direct_connect_known_public_MAC";
        checked(esp_ble_gattc_open(gattIf, expectedPeer, BLE_ADDR_TYPE_PUBLIC, true), "direct_connection_request"); break;
    case Kind::Connected:
    case Kind::ClientOpened:
        if (!samePeer(event.peer) || (state.connected && event.conn != connection)) {
            ++state.rx_wrong_peer;
            if (state.server) esp_ble_gatts_close(event.interface, event.conn); else esp_ble_gattc_close(event.interface, event.conn);
            break;
        }
        connection = event.conn; state.connected = true; state.mtu = event.a ? event.a : 23; state.stage = "connected_negotiating";
        setupStarted = esp_timer_get_time();
        requestConnectionParameters();
        if (!state.server) {
            checked(esp_ble_gattc_send_mtu_req(gattIf, connection), "MTU_exchange_request");
        }
        break;
    case Kind::Disconnected:
        if (event.conn == connection) { state.connected = false; state.ready = false; state.notifications_enabled = false; state.mtu = 23; state.stage = "disconnected"; state.last_error = event.status; }
        break;
    case Kind::Mtu:
        if (event.conn == connection) { state.mtu = event.a; mtuExchanged = true; maybeSearchService(); }
        break;
    case Kind::ServiceCacheReady:
        if (event.conn == connection) { serviceCacheReady = true; maybeSearchService(); }
        break;
    case Kind::SearchResult: if (event.conn == connection) { serviceStart = event.a; serviceEnd = event.b; } break;
    case Kind::SearchDone: if (event.conn == connection) discoverCharacteristic(); break;
    case Kind::NotifyRegistered:
        if (event.a == valueHandle) { uint8_t enable[2] = {1, 0}; checked(esp_ble_gattc_write_char_descr(gattIf, connection, descriptorHandle, 2, enable, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE), "notification_CCCD_write_request"); }
        break;
    case Kind::DescriptorWritten:
        if (event.conn == connection && event.a == descriptorHandle) state.notifications_enabled = true;
        break;
    case Kind::Congestion: if (event.conn == connection) state.congested = event.flag; break;
    case Kind::WriteFailed: break;
    }
    updateReadiness();
}
} // namespace

bool begin(bool server, const uint8_t peerMac[6], RxFn receive) {
    stop();
    if (state.initialized || esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED || esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
        failure(ESP_ERR_INVALID_STATE, "Bluetooth_stack_already_owned_or_stop_failed"); return false;
    }
    state = {}; state.server = server; receiveFn = receive;
    if (!peerMac || !receive || (peerMac[0] & 1)) { failure(ESP_ERR_INVALID_ARG, "explicit_public_peer_required"); return false; }
    std::memcpy(expectedPeer, peerMac, 6);
    if (!events) events = xQueueCreate(EventDepth, sizeof(Event));
    if (!received) received = xQueueCreate(RxDepth, sizeof(Received));
    if (!events || !received) { failure(ESP_ERR_NO_MEM, "bounded_queue_allocation"); return false; }
    xQueueReset(events); xQueueReset(received); rxDrops.store(0); eventDrops.store(0);
    gattIf = ESP_GATT_IF_NONE; connection = valueHandle = descriptorHandle = serviceStart = serviceEnd = 0;
    serviceReady = advDataReady = whitelistReady = advertisingRequested = false;
    mtuExchanged = serviceCacheReady = serviceSearchRequested = false;
    esp_bt_controller_config_t configuration = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
#if CONFIG_IDF_TARGET_ESP32
    configuration.mode = ESP_BT_MODE_BLE;
    configuration.ble_max_conn = 1;
#endif
    if (!checked(esp_bt_controller_init(&configuration), "controller_init")) return false;
    ownController = true;
    if (!checked(esp_bt_controller_enable(ESP_BT_MODE_BLE), "controller_enable_BLE")) { stop(); return false; }
    if (!checked(esp_bluedroid_init(), "bluedroid_init")) { stop(); return false; }
    ownBluedroid = true;
    if (!checked(esp_bluedroid_enable(), "bluedroid_enable")) { stop(); return false; }
    state.initialized = true;
    if (!checked(esp_ble_gap_register_callback(gapCallback), "GAP_callback_register") ||
        !checked(esp_ble_gatt_set_local_mtu(RequestedMtu), "local_MTU_request")) { stop(); return false; }
    callbackEnabled.store(true, std::memory_order_release);
    setupStarted = esp_timer_get_time(); state.stage = "registering_GATT_application";
    if (server) {
        prepareAttributes();
        advertisingData[0] = 2; advertisingData[1] = ESP_BLE_AD_TYPE_FLAG; advertisingData[2] = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;
        advertisingData[3] = 17; advertisingData[4] = ESP_BLE_AD_TYPE_128SRV_CMPL; std::memcpy(advertisingData + 5, serviceBytes, 16);
        advertisingData[21] = 5; advertisingData[22] = ESP_BLE_AD_TYPE_INT_RANGE;
        advertisingData[23] = 6; advertisingData[24] = 0; advertisingData[25] = 12; advertisingData[26] = 0;
        if (!checked(esp_ble_gatts_register_callback(serverCallback), "GATTS_callback_register") ||
            !checked(esp_ble_gap_update_whitelist(true, expectedPeer, BLE_WL_ADDR_TYPE_PUBLIC), "known_peer_whitelist_request") ||
            !checked(esp_ble_gap_config_adv_data_raw(advertisingData, sizeof(advertisingData)), "advertising_data_request") ||
            !checked(esp_ble_gatts_app_register(AppId), "GATTS_application_request")) { stop(); return false; }
    } else if (!checked(esp_ble_gattc_register_callback(clientCallback), "GATTC_callback_register") ||
               !checked(esp_ble_gattc_app_register(AppId), "GATTC_application_request")) { stop(); return false; }
    return true;
}
void poll() {
    if (!state.initialized) return;
    Event event;
    for (unsigned n = 0; n < PollBatch && xQueueReceive(events, &event, 0) == pdTRUE; ++n) process(event);
    Received item;
    for (unsigned n = 0; n < PollBatch && xQueueReceive(received, &item, 0) == pdTRUE; ++n) {
        if (!state.connected || item.conn != connection || !samePeer(item.peer)) { ++state.rx_wrong_peer; continue; }
        if (item.prepared || item.offset || item.length > state.mtu - 3) { ++state.rx_oversize_or_fragmented; continue; }
        if (state.server && item.handle == descriptorHandle && item.length == 2) {
            state.notifications_enabled = item.bytes[0] == 1 && item.bytes[1] == 0; updateReadiness(); continue;
        }
        if (item.handle != valueHandle || (!state.server && !item.notification) || !state.ready) { ++state.rx_oversize_or_fragmented; continue; }
        receiveFn(item.bytes, item.length);
    }
    if (eventDrops.load()) failure(ESP_ERR_NO_MEM, "control_event_queue_overflow");
    if (!state.ready && (!state.server || state.connected || !advertisingRequested) && esp_timer_get_time() - setupStarted > SetupTimeoutUs && !state.last_error)
        failure(ESP_ERR_TIMEOUT, "setup_timeout_no_auto_retry");
}
bool send(const uint8_t *bytes, size_t length) {
    if (!state.ready || state.congested || !bytes || length > MaxFrame || length > size_t(state.mtu - 3)) {
        ++state.tx_failures; return false;
    }
    const esp_err_t result = state.server
        ? esp_ble_gatts_send_indicate(gattIf, connection, valueHandle, uint16_t(length), const_cast<uint8_t *>(bytes), false)
        : esp_ble_gattc_write_char(gattIf, connection, valueHandle, uint16_t(length), const_cast<uint8_t *>(bytes), ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
    if (result == ESP_OK) { ++state.tx_api_accepted; return true; }
    ++state.tx_failures; state.last_error = result; return false;
}
void stop() {
    callbackEnabled.store(false, std::memory_order_release);
    state.ready = false;
    if (ownBluedroid) {
        if (state.server) esp_ble_gap_stop_advertising();
        if (state.connected && gattIf != ESP_GATT_IF_NONE) {
            if (state.server) esp_ble_gatts_close(gattIf, connection); else esp_ble_gattc_close(gattIf, connection);
        }
        if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) checked(esp_bluedroid_disable(), "bluedroid_disable");
        if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED) checked(esp_bluedroid_deinit(), "bluedroid_deinit");
        ownBluedroid = esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED;
    }
    if (ownController && !ownBluedroid) {
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) checked(esp_bt_controller_disable(), "controller_disable");
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) checked(esp_bt_controller_deinit(), "controller_deinit");
        ownController = esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE;
    }
    state.initialized = ownController || ownBluedroid; state.connected = false; state.notifications_enabled = false;
    if (!state.initialized && !state.last_error) state.stage = "off";
    // Queue allocations are bounded and reused. Never free memory that a
    // callback already in flight might still enqueue into during shutdown.
    if (events) xQueueReset(events); if (received) xQueueReset(received);
}
Info info() {
    Info result = state;
    result.rx_drops = rxDrops.load(); result.event_drops = eventDrops.load();
    if (state.connected) {
        esp_gap_conn_params_t parameters{};
        result.connection_param_status = esp_ble_get_current_conn_params(expectedPeer, &parameters);
        result.connection_params_valid = result.connection_param_status == ESP_OK;
        if (result.connection_params_valid) {
            result.actual_interval = parameters.interval; result.actual_latency = parameters.latency; result.actual_timeout = parameters.timeout;
        }
    }
    return result;
}
} // namespace perf_ble
#endif
