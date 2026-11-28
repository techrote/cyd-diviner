# Owned-board wireless evidence

Source capture status: **FAILED**. Execution reached plan end: **True**. Run statuses: `{'MEASURED': 23, 'FAILED': 7}`. Input SHA256 `c1cbc539a4f5bc38f1f26b9b5878888ee765f51dd45cb3a2531b5a08f4272b35`.

[Raw capture archive](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (member `physical/link-wifi-retry-01/results.json`); redacted per-board RX/target/event logs and image/flash receipts remain separate evidence.

* **cyd**: `cyd-profile-link`, source `174a79670d4b0da656ebd615f0cd1620aa0ddbb1`, source digest `1b7d5aacd09d257dff759f95413047b74fe2b6de7592b7a3f147e39aea0c07ed`, binary SHA256 `a980f7e2b9409686ae2d325ebeddfd525966f06e8ded03b2d8a723b36771ab11`.
* **peer**: `kernel-s3-n4r2-peer`, source `174a79670d4b0da656ebd615f0cd1620aa0ddbb1`, source digest `1b7d5aacd09d257dff759f95413047b74fe2b6de7592b7a3f147e39aea0c07ed`, binary SHA256 `7ac5bf7f79bb2ed6048a40367fe1d3966a1d5e53a999b2f09f28f310456bb681`.

Only matching settings/source/image/allocation/connection contexts are aggregated. Goodput is unique application payload per initiator target second, excluding the 40-byte header and PHY overhead. RTT p50/p95/p99 values below are histogram upper bounds capped at the exact observed maximum; p99 is absent below 100 observations. The displayed not-observed fraction is an end-to-end counter difference, not isolated RF loss. A 64-sequence horizon, callback queues and the receive tail limit attribution.

## Per-repetition observations

| Run | Workload | link/profile status | received B/s | API/peer unique | not seen fwd/RTT % | RTT n / p95≤ / max µs | CYD FPS |
| --- | --- | --- | --- | --- | --- | --- | --- |
| l001_1 | direct_softap/menu/tcp_goodput | MEASURED/MEASURED | 55603.20 | 543/543 | 0.00/— | 0/—/— | 29.30 |
| l002_2 | direct_softap/menu/tcp_goodput | MEASURED/MEASURED | 58163.20 | 568/568 | 0.00/— | 0/—/— | 29.20 |
| l003_3 | direct_softap/menu/tcp_goodput | MEASURED/MEASURED | 39321.60 | 448/384 | 14.29/— | 0/—/— | 29.30 |
| l004_1 | direct_softap/full_gate_drone/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 22.20 |
| l005_2 | direct_softap/full_gate_drone/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 22.20 |
| l006_3 | direct_softap/full_gate_drone/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 22.20 |
| l007_1 | direct_softap/full_gate_drone/rtt | MEASURED/MEASURED | 5734.40 | 896/896 | 0.00/0.22 | 894/101786/101786 | 17.50 |
| l008_2 | direct_softap/full_gate_drone/rtt | MEASURED/MEASURED | 5728.00 | 895/895 | 0.00/0.00 | 895/110612/110612 | 17.50 |
| l009_3 | direct_softap/full_gate_drone/rtt | MEASURED/MEASURED | 5734.40 | 896/896 | 0.00/0.00 | 896/119563/119563 | 17.50 |
| l010_1 | direct_softap/full_gate_drone/udp_paced | MEASURED/MEASURED | 138240.00 | 1420/1350 | 4.93/— | 0/—/— | 18.00 |
| l011_2 | direct_softap/full_gate_drone/udp_paced | MEASURED/MEASURED | 141824.00 | 1463/1385 | 5.33/— | 0/—/— | 18.00 |
| l012_3 | direct_softap/full_gate_drone/udp_paced | MEASURED/MEASURED | 132300.80 | 1381/1292 | 6.44/— | 0/—/— | 18.00 |
| l013_1 | direct_softap/full_gate_drone/tcp_goodput | MEASURED/MEASURED | 71680.00 | 806/700 | 13.15/— | 0/—/— | 18.60 |
| l014_2 | direct_softap/full_gate_drone/tcp_goodput | MEASURED/MEASURED | 79052.80 | 772/772 | 0.00/— | 0/—/— | 18.60 |
| l015_3 | direct_softap/full_gate_drone/tcp_goodput | MEASURED/MEASURED | 80998.40 | 791/791 | 0.00/— | 0/—/— | 18.50 |
| l016_1 | through_ap/menu/tcp_goodput | FAILED/MEASURED | — | —/— | —/— | —/—/— | 29.50 |
| l017_2 | through_ap/menu/tcp_goodput | FAILED/MEASURED | — | —/— | —/— | —/—/— | 29.40 |
| l018_3 | through_ap/menu/tcp_goodput | MEASURED/MEASURED | 46387.20 | 453/453 | 0.00/— | 0/—/— | 29.40 |
| l019_1 | through_ap/full_gate_drone/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 22.20 |
| l020_2 | through_ap/full_gate_drone/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 22.20 |
| l021_3 | through_ap/full_gate_drone/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 22.20 |
| l022_1 | through_ap/full_gate_drone/rtt | MEASURED/MEASURED | 5241.60 | 877/819 | 6.61/9.58 | 793/262144/294411 | 17.70 |
| l023_2 | through_ap/full_gate_drone/rtt | MEASURED/MEASURED | 5190.40 | 871/811 | 6.89/10.33 | 781/262144/326464 | 17.80 |
| l024_3 | through_ap/full_gate_drone/rtt | MEASURED/MEASURED | 5273.60 | 876/824 | 5.94/8.33 | 803/262144/281466 | 17.80 |
| l025_1 | through_ap/full_gate_drone/udp_paced | FAILED/MEASURED | 0.00 | 0/0 | —/— | 0/—/— | 18.60 |
| l026_2 | through_ap/full_gate_drone/udp_paced | MEASURED/MEASURED | 121753.60 | 1207/1189 | 1.49/— | 0/—/— | 18.10 |
| l027_3 | through_ap/full_gate_drone/udp_paced | FAILED/MEASURED | 0.00 | 0/0 | —/— | 0/—/— | 18.50 |
| l028_1 | through_ap/full_gate_drone/tcp_goodput | FAILED/MEASURED | 0.00 | 0/0 | —/— | 0/—/— | 19.70 |
| l029_2 | through_ap/full_gate_drone/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l030_3 | through_ap/full_gate_drone/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |

Idle cells are intentionally unavailable for RTT/goodput/loss. FAILED/BLOCKED traffic cases do not enter the successful-link aggregates; a completed local profiler window remains separately inspectable when peer traffic fails.

| Run | sender API errors | receiver dup/reordered/too-old | CRC CYD/peer | callback drops CYD/peer (session incl warmup) |
| --- | --- | --- | --- | --- |
| l001_1 | 0 | 0/0/0 | 0/0 | 0/0 |
| l002_2 | 0 | 0/0/0 | 0/0 | 0/0 |
| l003_3 | 0 | 2/62/63 | 0/0 | 0/0 |
| l004_1 | — | —/—/— | —/— | —/— |
| l005_2 | — | —/—/— | —/— | —/— |
| l006_3 | — | —/—/— | —/— | —/— |
| l007_1 | 0 | 0/0/0 | 0/0 | 0/0 |
| l008_2 | 0 | 0/0/0 | 0/0 | 0/0 |
| l009_3 | 0 | 0/0/0 | 0/0 | 0/0 |
| l010_1 | 242 | 0/0/0 | 0/0 | 0/0 |
| l011_2 | 198 | 0/0/0 | 0/0 | 0/0 |
| l012_3 | 275 | 0/0/0 | 0/0 | 0/0 |
| l013_1 | 0 | 2/62/105 | 0/0 | 0/0 |
| l014_2 | 0 | 0/0/0 | 0/0 | 0/0 |
| l015_3 | 0 | 0/0/0 | 0/0 | 0/0 |
| l016_1 | — | —/—/— | —/— | —/— |
| l017_2 | — | —/—/— | —/— | —/— |
| l018_3 | 0 | 0/0/0 | 0/0 | 0/0 |
| l019_1 | — | —/—/— | —/— | —/— |
| l020_2 | — | —/—/— | —/— | —/— |
| l021_3 | — | —/—/— | —/— | —/— |
| l022_1 | 22 | 0/0/0 | 0/0 | 0/0 |
| l023_2 | 30 | 0/0/0 | 0/0 | 0/0 |
| l024_3 | 27 | 0/0/0 | 0/0 | 0/0 |
| l025_1 | 1672 | 0/0/0 | 0/0 | 0/0 |
| l026_2 | 451 | 0/0/0 | 0/0 | 0/0 |
| l027_3 | 1658 | 0/0/0 | 0/0 | 0/0 |
| l028_1 | 0 | 0/0/0 | 0/0 | 0/0 |
| l029_2 | — | —/—/— | —/— | —/— |
| l030_3 | — | —/—/— | —/— | —/— |

## Matching-context envelopes

| Group / workload | valid/attempts | goodput median [min,max] B/s | RTT n / p50≤ / p95≤ / p99≤ / max µs | FPS / Δ idle | CPU0/1 % | min heap/largest B | callback p95≤/max µs | period max µs | TX_Q_OVF/DMA_ERROR |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| c90f931683a1 direct_softap/menu/tcp_goodput 1024B@0pps 10000ms | 3/3 | 55603.20 [39321.60,58163.20] | 0/—/—/—/— | 29.30/— | 29.08/95.72 | 16932/13300 | 2/397 | 16010 | 0/0 |
| aaf527b9fa8d direct_softap/full_gate_drone/idle 0B@0pps 10000ms | 3/3 | — [—,—] | —/—/—/—/— | 22.20/0.00 | 14.05/95.82 | 37036/13300 | 1024/2008 | 6628 | 0/0 |
| 38dda90c6c3a direct_softap/full_gate_drone/rtt 64B@100pps 10000ms | 3/3 | 5734.40 [5728.00,5734.40] | 2685/65536/119563/119563/119563 | 17.50/-4.70 | 50.33/98.10 | 33772/13300 | 4096/8445 | 11685 | 0/0 |
| a6bcceb8d205 direct_softap/full_gate_drone/udp_paced 1024B@200pps 10000ms | 3/3 | 138240.00 [132300.80,141824.00] | 0/—/—/—/— | 18.00/-4.20 | 41.75/98.14 | 18320/13300 | 2048/6662 | 11274 | 0/0 |
| 8f39e5094a5d direct_softap/full_gate_drone/tcp_goodput 1024B@0pps 10000ms | 3/3 | 79052.80 [71680.00,80998.40] | 0/—/—/—/— | 18.60/— | 53.16/98.15 | 17900/13300 | 4096/14332 | 18123 | 0/0 |
| 704a1faeea54 through_ap/menu/tcp_goodput 1024B@0pps 10000ms | 1/3 | 46387.20 [46387.20,46387.20] | 0/—/—/—/— | 29.40/— | 25.74/95.40 | 18172/13300 | 2/8 | 16540 | 0/0 |
| f6ca05a634e1 through_ap/full_gate_drone/idle 0B@0pps 10000ms | 3/3 | — [—,—] | —/—/—/—/— | 22.20/0.00 | 14.29/95.88 | 20264/13300 | 1024/1851 | 6520 | 0/0 |
| 8e18aa526b4d through_ap/full_gate_drone/rtt 64B@100pps 10000ms | 3/3 | 5241.60 [5190.40,5273.60] | 2377/65536/262144/262144/326464 | 17.80/-4.40 | 45.66/98.06 | 14944/13300 | 4096/7540 | 13389 | 0/0 |
| 50cb2fb09c0c through_ap/full_gate_drone/udp_paced 1024B@200pps 10000ms | 0/2 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| 8e3dee991647 through_ap/full_gate_drone/udp_paced 1024B@200pps 10000ms | 1/1 | 121753.60 [121753.60,121753.60] | 0/—/—/—/— | 18.10/— | 37.55/98.20 | 19544/13300 | 2048/5050 | 8717 | 0/0 |
| 32755deda48b through_ap/full_gate_drone/tcp_goodput 1024B@0pps 10000ms | 0/1 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| c2e9ef4c336e through_ap/full_gate_drone/tcp_goodput 1024B@0pps 10000ms | 0/2 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |

The baseline delta uses a matching topology/local/source/allocation/connection control, when present; transport resource differences remain part of the comparison. CYD utilization uses non-idle tick samples. Peer CPU is unmeasured. Enqueue duration includes DMA blocking; independent timing maxima/percentiles must not be added as one block. TX_Q_OVF counts the legacy driver's free-TX-buffer-queue-full notification, not a measured audible gap. Queue limits/drain timing can hide events; analog continuity remains unknown.

RTT jitter is the mean/max absolute difference between adjacent received RTTs within each run; repetitions are not joined. The JSON/CSV retains counters, jitter, callback/lock/mix/enqueue/period histograms, BLE negotiated parameters, CRC/duplicates/reordering/too-old/queue-drop counts and actual allocation receipts.

## Failures and measurement limits

* `l016_1` through_ap/menu/tcp_goodput: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 247.31439379998483}.
* `l017_2` through_ap/menu/tcp_goodput: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 262.4596124999807}.
* `l025_1` through_ap/full_gate_drone/udp_paced: **FAILED** — No valid end-to-end traffic/RTT established; target profile retained separately.
* `l027_3` through_ap/full_gate_drone/udp_paced: **FAILED** — No valid end-to-end traffic/RTT established; target profile retained separately.
* `l028_1` through_ap/full_gate_drone/tcp_goodput: **FAILED** — No valid end-to-end traffic/RTT established; target profile retained separately.
* `l029_2` through_ap/full_gate_drone/tcp_goodput: **FAILED** — Known-peer connection was not established; no unrelated endpoints attempted.
* `l030_3` through_ap/full_gate_drone/tcp_goodput: **FAILED** — Known-peer connection was not established; no unrelated endpoints attempted.

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
