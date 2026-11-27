# Owned-board wireless evidence

Source capture status: **FAILED**. Execution reached plan end: **True**. Run statuses: `{'FAILED': 18}`. Input SHA256 `1d231d28c256d1e81fe796e2c683342457893c3df8b6e3a4427faf7e7038fedd`.

[Raw capture archive](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (member `physical/link-ble-01/results.json`); redacted per-board RX/target/event logs and image/flash receipts remain separate evidence.

* **cyd**: `cyd-profile-link`, source `174a79670d4b0da656ebd615f0cd1620aa0ddbb1`, source digest `1b7d5aacd09d257dff759f95413047b74fe2b6de7592b7a3f147e39aea0c07ed`, binary SHA256 `a980f7e2b9409686ae2d325ebeddfd525966f06e8ded03b2d8a723b36771ab11`.
* **peer**: `kernel-s3-n4r2-peer`, source `174a79670d4b0da656ebd615f0cd1620aa0ddbb1`, source digest `1b7d5aacd09d257dff759f95413047b74fe2b6de7592b7a3f147e39aea0c07ed`, binary SHA256 `7ac5bf7f79bb2ed6048a40367fe1d3966a1d5e53a999b2f09f28f310456bb681`.

Only matching settings/source/image/allocation/connection contexts are aggregated. Goodput is unique application payload per initiator target second, excluding the 40-byte header and PHY overhead. RTT p50/p95/p99 values below are histogram upper bounds capped at the exact observed maximum; p99 is absent below 100 observations. The displayed not-observed fraction is an end-to-end counter difference, not isolated RF loss. A 64-sequence horizon, callback queues and the receive tail limit attribution.

## Per-repetition observations

| Run | Workload | link/profile status | received B/s | API/peer unique | not seen fwd/RTT % | RTT n / p95≤ / max µs | CYD FPS |
| --- | --- | --- | --- | --- | --- | --- | --- |
| l001_1 | direct_ble/menu/idle_connected | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l002_2 | direct_ble/menu/idle_connected | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l003_3 | direct_ble/menu/idle_connected | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l004_1 | direct_ble/menu/rtt | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l005_2 | direct_ble/menu/rtt | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l006_3 | direct_ble/menu/rtt | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l007_1 | direct_ble/menu/ble_paced | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l008_2 | direct_ble/menu/ble_paced | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l009_3 | direct_ble/menu/ble_paced | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l010_1 | direct_ble/full_gate_drone/idle_connected | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l011_2 | direct_ble/full_gate_drone/idle_connected | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l012_3 | direct_ble/full_gate_drone/idle_connected | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l013_1 | direct_ble/full_gate_drone/rtt | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l014_2 | direct_ble/full_gate_drone/rtt | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l015_3 | direct_ble/full_gate_drone/rtt | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l016_1 | direct_ble/full_gate_drone/ble_paced | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l017_2 | direct_ble/full_gate_drone/ble_paced | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l018_3 | direct_ble/full_gate_drone/ble_paced | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |

Idle cells are intentionally unavailable for RTT/goodput/loss. FAILED/BLOCKED traffic cases do not enter the successful-link aggregates; a completed local profiler window remains separately inspectable when peer traffic fails.

| Run | sender API errors | receiver dup/reordered/too-old | CRC CYD/peer | callback drops CYD/peer (session incl warmup) |
| --- | --- | --- | --- | --- |
| l001_1 | — | —/—/— | —/— | —/— |
| l002_2 | — | —/—/— | —/— | —/— |
| l003_3 | — | —/—/— | —/— | —/— |
| l004_1 | — | —/—/— | —/— | —/— |
| l005_2 | — | —/—/— | —/— | —/— |
| l006_3 | — | —/—/— | —/— | —/— |
| l007_1 | — | —/—/— | —/— | —/— |
| l008_2 | — | —/—/— | —/— | —/— |
| l009_3 | — | —/—/— | —/— | —/— |
| l010_1 | — | —/—/— | —/— | —/— |
| l011_2 | — | —/—/— | —/— | —/— |
| l012_3 | — | —/—/— | —/— | —/— |
| l013_1 | — | —/—/— | —/— | —/— |
| l014_2 | — | —/—/— | —/— | —/— |
| l015_3 | — | —/—/— | —/— | —/— |
| l016_1 | — | —/—/— | —/— | —/— |
| l017_2 | — | —/—/— | —/— | —/— |
| l018_3 | — | —/—/— | —/— | —/— |

## Matching-context envelopes

| Group / workload | valid/attempts | goodput median [min,max] B/s | RTT n / p50≤ / p95≤ / p99≤ / max µs | FPS / Δ idle | CPU0/1 % | min heap/largest B | callback p95≤/max µs | period max µs | TX_Q_OVF/DMA_ERROR |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 07996af9754b direct_ble/menu/idle_connected 64B@1pps 10000ms | 0/3 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| b64640e2ef67 direct_ble/menu/rtt 64B@20pps 10000ms | 0/3 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| 308b483ca6d3 direct_ble/menu/ble_paced 200B@50pps 10000ms | 0/3 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| 4b795c6a57bb direct_ble/full_gate_drone/idle_connected 64B@1pps 10000ms | 0/3 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| 380d2af8b399 direct_ble/full_gate_drone/rtt 64B@20pps 10000ms | 0/3 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| 106d7f11ab18 direct_ble/full_gate_drone/ble_paced 200B@50pps 10000ms | 0/3 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |

The baseline delta uses a matching topology/local/source/allocation/connection control, when present; transport resource differences remain part of the comparison. CYD utilization uses non-idle tick samples. Peer CPU is unmeasured. Enqueue duration includes DMA blocking; independent timing maxima/percentiles must not be added as one block. TX_Q_OVF counts the legacy driver's free-TX-buffer-queue-full notification, not a measured audible gap. Queue limits/drain timing can hide events; analog continuity remains unknown.

RTT jitter is the mean/max absolute difference between adjacent received RTTs within each run; repetitions are not joined. The JSON/CSV retains counters, jitter, callback/lock/mix/enqueue/period histograms, BLE negotiated parameters, CRC/duplicates/reordering/too-old/queue-drop counts and actual allocation receipts.

## Failures and measurement limits

**A runtime panic is present in the redacted raw stream.** This is firmware/runtime failure evidence, not a BLE hardware-unsupported result. The capture also records 17 mode-required rejections. A reboot invalidates volatile setup; these later rejections must not be presented as independent hardware-capacity tests. Abort/re-provision after a detected runtime reset before claiming independent repetitions.

* Raw `cyd` panic at byte 6788 in `cyd/serial-rx.redacted.bin` (host RX-chunk elapsed 2.10 s): `Guru Meditation Error: Core  0 panic'ed (StoreProhibited). Exception was unhandled.`.
* Raw `cyd` backtrace at byte 7443 in `cyd/serial-rx.redacted.bin` (host RX-chunk elapsed 2.11 s): `Backtrace: 0x40091ec9:0x3fff9970 0x40172ad4:0x3fff9980 0x4015336d:0x3fff99a0 0x4016d333:0x3fff99c0`.
* Raw `cyd` firmware_startup at byte 8251 in `cyd/serial-rx.redacted.bin` (host RX-chunk elapsed 3.63 s): `Diviner CYD: LCD ready 240x320, canvas ready 240x135, audio ready, heap 96168 bytes`.
* `l001_1` direct_ble/menu/idle_connected: **FAILED** — Endpoint acknowledgement/summary timeout (no private contents recorded).
* `l002_2` direct_ble/menu/idle_connected: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 18.302264900004957}.
* `l003_3` direct_ble/menu/idle_connected: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 19.69017930002883}.
* `l004_1` direct_ble/menu/rtt: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 21.07637840003008}.
* `l005_2` direct_ble/menu/rtt: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 22.495593300031032}.
* `l006_3` direct_ble/menu/rtt: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 23.920195100014098}.
* `l007_1` direct_ble/menu/ble_paced: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 25.34167420002632}.
* `l008_2` direct_ble/menu/ble_paced: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 26.765713999979198}.
* `l009_3` direct_ble/menu/ble_paced: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 28.187656500027515}.
* `l010_1` direct_ble/full_gate_drone/idle_connected: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 29.75372929999139}.
* `l011_2` direct_ble/full_gate_drone/idle_connected: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 31.520743699977174}.
* `l012_3` direct_ble/full_gate_drone/idle_connected: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 33.24524249997921}.
* `l013_1` direct_ble/full_gate_drone/rtt: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 34.971333599998616}.
* `l014_2` direct_ble/full_gate_drone/rtt: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 36.69339630001923}.
* `l015_3` direct_ble/full_gate_drone/rtt: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 38.414395400031935}.
* `l016_1` direct_ble/full_gate_drone/ble_paced: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 40.14250479999464}.
* `l017_2` direct_ble/full_gate_drone/ble_paced: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 41.91050549998181}.
* `l018_3` direct_ble/full_gate_drone/ble_paced: **FAILED** — Endpoint setup failed: {"schema": 1, "type": "link_ack", "action": "arm", "ok": false, "code": 22, "operation": "ble_mode_required", "host_received_elapsed_s": 43.678436500020325}.

* FAILED/BLOCKED cases and separately valid profiler windows retain their own statuses. Only MEASURED link cases enter successful-link aggregates.
* Idle has no RTT/goodput/loss observation; missing values remain null. Per-repetition goodput uses median/range, not an unsupported p99 from three repetitions.
* RTT percentiles pool bounded histogram bins across matching sessions, are upper bounds capped at exact observed max, and require>=100 samples for p99.
* Adjacent absolute RTT differences are not one-way delay jitter; cross-repetition boundaries are not joined.
* RTT histograms are conditional on valid replies received within the bounded tail. Missing responses never enter latency bins; sender/receiver/RTT counts must accompany the tails.
* Receiver counts exclude packets outside the 64-sequence deduplication horizon. too_old can include uncounted unique packets, making observed unique goodput a lower bound and the not-observed fraction an overestimate of loss.
* API acceptance is not peer receipt. Not-observed fraction includes software queues, horizon and tail effects; it is not isolated RF packet loss.
* TCP frames created in the steady window can finish API acceptance in the defined reply tail. Sender and receiver rates therefore share marked-window-plus-tail semantics.
* Peer CPU utilization is not measured by this image; CYD tick utilization is an estimate. Audio enqueue includes blocking DMA waits; driver events do not prove analog continuity.
* Only matching source/image, topology, local settings/allocation receipts, channel/power/connection parameters, payload, rate and timing scopes are pooled. Idle comparison includes transport resource/connection setup differences stated by its mode.

## Offload interpretation

Compare local compute with remote compute plus packing/unpacking, queueing and measured transport for the actual input/output message shapes. A full matched-payload RTT includes its payload transfer; do not add that transmission again. A separate fixed-overhead/throughput model needs independently estimated fixed overhead and directional byte-transfer costs. The 64-byte echo and paced one-way throughput are not a measured job RPC for arbitrary larger inputs/outputs. A throughput average or finite maximum gives no hard real-time guarantee; these links do not by themselves justify moving the local DAC deadline loop off-board.

Artifacts: [link-summary.json](link-summary.json), [per-run CSV](link-runs.csv), [group CSV](link-groups.csv), [analysis manifest](link-analysis-manifest.json).

## Publication provenance

Extract the raw archive into `evidence/`, preserving its `physical/` prefix. The publication verifies every capture-manifest member against the ZIP and preserves failed statuses. `manifest.json` pins archive, source and derived-output hashes.
