// Directed two-board GATT adapter; packet validation remains in perf_link.
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef GRIM_PERF_LINK
namespace perf_ble {
constexpr uint16_t RequestedMtu = 247;
constexpr size_t MaxFrame = 240; // 40-byte link header + at most 200 payload bytes.
constexpr unsigned RxDepth = 8, EventDepth = 16, PollBatch = 8;
constexpr const char *ServiceUuid = "c1e872e1-4f68-4c0e-8e4b-b725a8e78d01";
constexpr const char *CharacteristicUuid = "c1e872e1-4f68-4c0e-8e4b-b725a8e78d02";
using RxFn = void (*)(const uint8_t *bytes, size_t length);
struct Info {
    bool initialized = false, server = false, ready = false, connected = false;
    bool congested = false, notifications_enabled = false, connection_params_valid = false;
    uint16_t requested_mtu = RequestedMtu, mtu = 23;
    uint16_t requested_min_interval = 6, requested_max_interval = 12;
    uint16_t requested_latency = 0, requested_timeout = 400;
    uint16_t actual_interval = 0, actual_latency = 0, actual_timeout = 0;
    uint32_t rx_drops = 0, event_drops = 0, tx_failures = 0, tx_api_accepted = 0;
    uint32_t rx_wrong_peer = 0, rx_oversize_or_fragmented = 0;
    int last_error = 0, connection_param_status = 0;
    const char *stage = "off";
};
// All public calls must be serialized by perf_link's control mutex. begin()
// accepts asynchronous setup; server advertising does not wait for a client.
// Client opens the explicit public BT address directly. No discovery scan.
bool begin(bool server, const uint8_t peerMac[6], RxFn receive);
bool send(const uint8_t *bytes, size_t length); // ESP_OK admission, not proof of receipt.
void poll(); // bounded callback-queue drain; invokes RxFn with no BLE lock held.
void stop(); // no irreversible BT memory release; queues retained/reused safely.
Info info();
} // namespace perf_ble
#endif
