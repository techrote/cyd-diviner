# Opt-in two-board link measurement protocol

The shared `perf_link` module is compiled only with `GRIM_PERF_LINK`. It starts
no radio or traffic automatically. The CYD client uses `cyd-profile-link`; the
S3 peer uses the explicitly configured peer profile. Match each flashed image
to its own build/flash receipt. Initialization and transport failure are results
to preserve, not evidence of a measured throughput ceiling.

## Provisioning and setup

Commands and responses are line-oriented. Responses begin `PERF ` and contain
schema-1 JSON. The command buffer must hold 320 bytes. Never put real network
names, passwords or session tokens in a checked-in command file or public log.
Provision from the private harness configuration, suppressing credential-bearing
TX records and private response windows. Firmware acknowledgments contain a
fixed action name rather than the command text.

| Command | Purpose |
|---|---|
| `perf link info` | Safe interface, capability and resource metadata |
| `perf link provision SSID_HEX PASSWORD_HEX` | Private RAM-only strings; `-` means an empty password |
| `perf link mode off 0` | Stop the service and radio stacks |
| `perf link mode espnow CHANNEL` | Fixed channel, no association or discovery scan |
| `perf link mode ap CHANNEL` | Provisioned temporary SoftAP, at most one station |
| `perf link mode sta CHANNEL` | Join the provisioned owner AP; channel 0 permits normal association discovery |
| `perf link mode ble 0` | Prepare directed BLE; radio/service setup occurs at serve/arm |
| `perf link allow MAC12 PEER_IPV4 PORT TOKEN32_HEX` | One explicit peer and 128-bit session token; use BT MAC for BLE |
| `perf link serve RUN_ID TRANSPORT` | Start the responder for `espnow`, `udp`, `tcp` or `ble` |
| `perf link arm TRANSPORT PATTERN PAYLOAD PPS` | Prepare initiator; patterns are `rtt` or `oneway` |
| `perf link idle` | Disarm before a run while preserving an established connection |
| `perf run RUN_ID WARMUP_MS DURATION_MS` | CYD: start the armed link on the exact profiler steady window |
| `perf link run RUN_ID WARMUP_MS DURATION_MS` | Standalone initiator convenience command |
| `perf link stop` | Stop the service, preserving its counter snapshot |
| `perf link export` | Emit a completed, stopped link summary |

Acknowledgments have `type: link_ack`, `action`, `ok`, numeric `code` and fixed
`operation`. A mode acknowledgment accepts setup; it does not prove association.
Poll `link_info` readiness outside measurement windows. TCP must be connected.
BLE must have both `ble_ready` and `ble_connected`, with
`ble_actual_payload_limit` at least the requested payload. An established BLE
idle baseline uses serve/arm, waits for readiness, then disarms with `idle` before
the ordinary CYD profiling run.

Payload bounds are 1–200 bytes for ESP-NOW/BLE and 1–1024 for UDP/TCP.
Pacing is limited to 2000 requested packets/s. PPS zero is accepted only for TCP
and means a bounded unpaced producer, not unlimited wire capacity. The worker
uses at most eight operations per batch, yields one tick between iterations,
and runs on core 1 at priority 1 with a 4096-byte stack. Requested rates may not
be achieved; record actual admitted packets and pacing lateness. Radio/GATT
queues and internal driver buffering add their own costs.

Wi-Fi credentials remain in RAM. The module requests `WIFI_STORAGE_RAM`, does
not call Diviner's saved-network writers, and suppresses SDK association logs
which can contain network names. Normal SDK PHY calibration storage is a separate
mechanism; this is not a promise that every NVS byte remains unchanged.
No captive-portal check, network sweep, public endpoint or discovery scan is used.
Directed BLE connects to the explicit public BT address and known service UUID.

## Frame format

All multibyte fields use little-endian encoding. The packed header is 40 bytes:

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | Magic `DVL1` |
| 4 | 1 | Kind: 1=data, 2=echo |
| 5 | 1 | Flags: bit 0=warmup, bit 1=request echo |
| 6 | 2 | Payload byte count |
| 8 | 16 | Session token; never exported in telemetry |
| 24 | 4 | Sequence number |
| 28 | 8 | Initiator packet-creation time, microseconds |
| 36 | 4 | CRC32 |
| 40 | variable | Deterministic payload bytes |

CRC is `esp_rom_crc32_le(0, frame, frame_length)` with the header's CRC field
temporarily zeroed. Payload byte `i` is `((sequence * 31 + i) ^ 0x5a) & 255`.
Echoes preserve sequence, payload, flags and initiator timestamp, change kind to
2 and recompute CRC. Validation checks frame size, role/kind, token and CRC.

ESP-NOW callbacks only copy a bounded frame from the allowlisted MAC into an
eight-entry queue. BLE callbacks use the adapter's bounded queues. Packet
validation, echo generation and counters run in the worker. UDP additionally
requires the configured source IP and port. TCP accepts one connection from the
configured source IP and validates each framed message. Socket I/O is nonblocking.

## Window and result interpretation

Start the responder before arming the client. Use a fresh private token per run
on both boards. CYD `perf run` supplies the link's exact steady start and end;
warmup frames are echoed but excluded from payload/RTT counts. Measurement
sequence numbering restarts at zero. After the fixed end, the client accepts
replies for 500 ms, then freezes application processing. An unfinished TCP frame
is recorded as failed and its socket is closed. Stop the responder after this
drain, then export both records. The responder's `duration_us` is zero because it
does not impose an independent synchronized clock window: its measured packet
set is selected by the initiator's warmup flag and session token.

The original CYD 19 profiler metrics and supplementary I2S event record remain
separate. Pair both link summaries with the matching profiler run ID and exact
firmware identities. Derive receiver payload goodput using unique validated
received bytes divided by the **initiator's fixed steady duration**, rather than
the responder's first-to-last receive span. Sender API admission and ESP-NOW MAC
completion do not prove application delivery.

Uniqueness tracking has a 64-packet reorder window. `too_old` observations cannot
be classified reliably as new or duplicate; nonzero values make the unique byte
count a lower bound. UDP/ESP-NOW missing delivery includes local queues and both
boards' scheduling. Through-AP results include the owner AP and its policy.
TCP retransmissions and packet loss remain unobserved by these application APIs.

RTT uses only the initiator's clock and includes packet creation, checksumming,
queuing, peer validation/echo, and local validation. It is not one-way RF latency.
`rtt_histogram_counts` contains 32 ceil-log2 bins; percentiles are upper bounds.
The adjacent absolute RTT difference is explicitly labelled as a variation
measure. Do not present a precise p99 from an insufficient observation count.

ESP-NOW completion counters use callback time in the initiator's steady window;
the responder counts its service session including warmup. The callback-drop
counter also includes warmup. These scopes differ from validated packet counts.
Record queue drops, failed sends, duplicates, too-old packets, readiness failures
and actual offered rate alongside goodput. Zero traffic or zero RTT observations
do not establish zero loss.

`link_info` records actual channel, query status, power-save setting, association,
RSSI where applicable, GATT MTU/parameters, heap and stack. S3 peer CPU utilization
is explicitly unmeasured by this module. The GATT adapter requests MTU 247,
connection interval minimum 6 and maximum 12 units of 1.25 ms, peripheral latency
0 and supervision timeout 400 units of 10 ms. Requested interval values are
constants in `perf_ble::Info`; this image exports requested MTU and the queried
actual interval, latency and timeout. Interpret actual parameters only when
`ble_conn_params_valid` is true; a request does not establish negotiated values.
Safe JSON lines use a bounded complete-
record writer. Its 6144-byte formatting allocation occurs outside measurement
windows; an info-time heap query includes that temporary buffer, while steady
CYD samples do not. Boot-global heap minima include such observer allocations.
