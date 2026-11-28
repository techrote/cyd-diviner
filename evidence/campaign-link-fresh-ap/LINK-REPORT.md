# Owned-board wireless evidence

Source capture status: **FAILED**. Execution reached plan end: **False**. Run statuses: `{'FAILED': 2, 'MEASURED': 7}`. Input SHA256 `d2cb18d13e6cf48d94cea8674a3db68a250db619a17cd59f45d28cace9ccb69c`.

[Raw capture archive](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (member `physical/link-fresh-ap-01/results.json`); redacted per-board RX/target/event logs and image/flash receipts remain separate evidence.

* **cyd**: `cyd-profile-link`, source `174a79670d4b0da656ebd615f0cd1620aa0ddbb1`, source digest `1b7d5aacd09d257dff759f95413047b74fe2b6de7592b7a3f147e39aea0c07ed`, binary SHA256 `a980f7e2b9409686ae2d325ebeddfd525966f06e8ded03b2d8a723b36771ab11`.
* **peer**: `kernel-s3-n4r2-peer`, source `174a79670d4b0da656ebd615f0cd1620aa0ddbb1`, source digest `1b7d5aacd09d257dff759f95413047b74fe2b6de7592b7a3f147e39aea0c07ed`, binary SHA256 `7ac5bf7f79bb2ed6048a40367fe1d3966a1d5e53a999b2f09f28f310456bb681`.

Only matching settings/source/image/allocation/connection contexts are aggregated. Goodput is unique application payload per initiator target second, excluding the 40-byte header and PHY overhead. RTT p50/p95/p99 values below are histogram upper bounds capped at the exact observed maximum; p99 is absent below 100 observations. The displayed not-observed fraction is an end-to-end counter difference, not isolated RF loss. A 64-sequence horizon, callback queues and the receive tail limit attribution.

## Per-repetition observations

| Run | Workload | link/profile status | received B/s | API/peer unique | not seen fwd/RTT % | RTT n / p95≤ / max µs | CYD FPS |
| --- | --- | --- | --- | --- | --- | --- | --- |
| l001_1 | through_ap/menu/tcp_goodput | FAILED/MEASURED | 0.00 | 0/0 | —/— | 0/—/— | 29.40 |
| l002_2 | through_ap/menu/tcp_goodput | MEASURED/MEASURED | 34201.60 | 334/334 | 0.00/— | 0/—/— | 29.40 |
| l003_3 | through_ap/menu/tcp_goodput | MEASURED/MEASURED | 36966.40 | 361/361 | 0.00/— | 0/—/— | 29.30 |
| l004_1 | through_ap/full_gate_drone/udp_paced | MEASURED/MEASURED | 131993.60 | 1381/1289 | 6.66/— | 0/—/— | 18.10 |
| l005_2 | through_ap/full_gate_drone/udp_paced | MEASURED/MEASURED | 130150.40 | 1356/1271 | 6.27/— | 0/—/— | 18.00 |
| l006_3 | through_ap/full_gate_drone/udp_paced | MEASURED/MEASURED | 139059.20 | 1443/1358 | 5.89/— | 0/—/— | 18.00 |
| l007_1 | through_ap/full_gate_drone/tcp_goodput | MEASURED/MEASURED | 49254.40 | 481/481 | 0.00/— | 0/—/— | 19.00 |
| l008_2 | through_ap/full_gate_drone/tcp_goodput | MEASURED/MEASURED | 64000.00 | 625/625 | 0.00/— | 0/—/— | 18.80 |
| l009_3 | through_ap/full_gate_drone/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |

Idle cells are intentionally unavailable for RTT/goodput/loss. FAILED/BLOCKED traffic cases do not enter the successful-link aggregates; a completed local profiler window remains separately inspectable when peer traffic fails.

| Run | sender API errors | receiver dup/reordered/too-old | CRC CYD/peer | callback drops CYD/peer (session incl warmup) |
| --- | --- | --- | --- | --- |
| l001_1 | 0 | 0/0/0 | 0/0 | 0/0 |
| l002_2 | 0 | 0/0/0 | 0/0 | 0/0 |
| l003_3 | 0 | 0/0/0 | 0/0 | 0/0 |
| l004_1 | 283 | 0/0/0 | 0/0 | 0/0 |
| l005_2 | 307 | 0/0/0 | 0/0 | 0/0 |
| l006_3 | 229 | 0/0/0 | 0/0 | 0/0 |
| l007_1 | 0 | 0/0/0 | 0/0 | 0/0 |
| l008_2 | 0 | 0/0/0 | 0/0 | 0/0 |
| l009_3 | — | —/—/— | —/— | —/— |

## Matching-context envelopes

| Group / workload | valid/attempts | goodput median [min,max] B/s | RTT n / p50≤ / p95≤ / p99≤ / max µs | FPS / Δ idle | CPU0/1 % | min heap/largest B | callback p95≤/max µs | period max µs | TX_Q_OVF/DMA_ERROR |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 9d990c949070 through_ap/menu/tcp_goodput 1024B@0pps 10000ms | 2/3 | 35584.00 [34201.60,36966.40] | 0/—/—/—/— | 29.35/— | 21.14/94.97 | 18712/13300 | 2/1740 | 14676 | 0/0 |
| 8cee261c5fd4 through_ap/full_gate_drone/udp_paced 1024B@200pps 10000ms | 3/3 | 131993.60 [130150.40,139059.20] | 0/—/—/—/— | 18.00/— | 40.14/98.20 | 19184/13300 | 2048/5504 | 9899 | 0/0 |
| 8c0283b3976e through_ap/full_gate_drone/tcp_goodput 1024B@0pps 10000ms | 2/3 | 56627.20 [49254.40,64000.00] | 0/—/—/—/— | 18.90/— | 40.63/97.89 | 17500/13300 | 4096/13044 | 16932 | 0/0 |

The baseline delta uses a matching topology/local/source/allocation/connection control, when present; transport resource differences remain part of the comparison. CYD utilization uses non-idle tick samples. Peer CPU is unmeasured. Enqueue duration includes DMA blocking; independent timing maxima/percentiles must not be added as one block. TX_Q_OVF counts the legacy driver's free-TX-buffer-queue-full notification, not a measured audible gap. Queue limits/drain timing can hide events; analog continuity remains unknown.

RTT jitter is the mean/max absolute difference between adjacent received RTTs within each run; repetitions are not joined. The JSON/CSV retains counters, jitter, callback/lock/mix/enqueue/period histograms, BLE negotiated parameters, CRC/duplicates/reordering/too-old/queue-drop counts and actual allocation receipts.

## Failures and measurement limits

* `l001_1` through_ap/menu/tcp_goodput: **FAILED** — No valid end-to-end traffic/RTT established; target profile retained separately.
* `l009_3` through_ap/full_gate_drone/tcp_goodput: **FAILED** — Malformed public telemetry; redacted bytes retained.

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

Artifacts: [link-summary.json](link-summary.json), [per-run CSV](link-runs.csv), [group CSV](link-groups.csv), [publication manifest](manifest.json).

## Publication provenance

Extract the raw archive into `evidence/`, preserving its `physical/` prefix. The publication verifies every capture-manifest member against the ZIP and preserves failed statuses. `manifest.json` pins archive, source and derived-output hashes.
