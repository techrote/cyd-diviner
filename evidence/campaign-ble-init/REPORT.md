# CYD performance evidence — BLE controller initialization only

Capture status: **CAPTURE COMPLETE**. Completed physical runs: **6/6**; pending or incomplete: **0**. Explicit NOT RUN workload classes: **0**.

Board: CYD on COM21 (owner identified), ESP32-D0WD-V3 revision 3.1. Source [174a79670d4b0da656ebd615f0cd1620aa0ddbb1](https://github.com/techrote/cyd-diviner/commit/174a79670d4b0da656ebd615f0cd1620aa0ddbb1); profile `cyd-profile-link`; firmware SHA256 `a980f7e2b9409686ae2d325ebeddfd525966f06e8ded03b2d8a723b36771ab11`. Input results SHA256 `1f46018d16ccf450ae2460e7534c570e446ec3ec69be1ec703b95e25addb970b`.

CPU 240 MHz, 2 cores; flash 4194304 bytes at 40000000 Hz; PSRAM 0 bytes. LCD SPI write/read 40000000/8000000 Hz; serial 533333 baud. SDK `v4.4.7-dirty`.

Fixed plan seed: `23063`. Ordinary requested warmup/window: 2000/10000 ms; default repetitions: 3. Per-case overrides remain in [results.json](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/ble-init-link-01/results.json`). Reset policy: `fresh_audio_DSP_state_each_entry_seeded_no_song_persistence`. Audio-window alignment: `real_setup_and_warmup_elapsed_not_sample_aligned`.

Saved display settings at initial inventory: theme 2, background style 1, gradient mode 1, canvas depth 16 bits. Orientation/backdrop commands then override the inventory values per workload. The exact palette, intensity table, setup acknowledgements and actual scene receipts are retained in JSON/raw logs.

## Interpretation

FPS is completed physical presentations divided by the target window, with the existing 33 ms frame cap. CPU percentages use tick-sampled current-task versus idle-task classification, not cycle-accurate task or interrupt accounting. Direct rendering interleaves CPU drawing and SPI transfer; its present duration cannot be separated into independent raster and bus costs. Histogram percentiles below are **upper bounds**. Human tables tighten each bound to `min(raw powers-of-two bucket upper, exact observed maximum)`; a capped bound equal to the maximum is still a bound, not an exact percentile. Raw target records and summary CSVs retain the original bucket upper bounds unchanged. Maxima are exact observed durations. A p99 is omitted below 100 observations. Percentiles and maxima describe these finite runs; they are not deadline guarantees.

Only repetitions with matching requested settings, duration, warmup, instrumentation and actual scene/allocation receipts are pooled. A scope suffix identifies variants if the same workload name has differing receipts. Independent maxima or percentiles must not be added as if they came from the same audio block.

Instrumentation A/B scope: `histogram_insertion_only_timestamps_hooks_counts_retained`. The enable switch controls histogram insertion while timestamps, hooks, frame/block counts and retained sampling remain. The on/off rows therefore measure that insertion change only. Total overhead relative to an ordinary firmware binary is not established by this switch. Disabled histograms give unavailable timing distributions rather than zero-duration work. The approved primary image reserves 24,008 bytes in its static histogram/sample arrays alone; other profiler state, queues and task stacks are additional. These arrays remain with insertion disabled. Reported heap describes the profiling image and workload history; ordinary firmware headroom cannot be inferred by subtracting only this footprint. Detailed source and method audits are in the repository's `evidence/method-notes.md`.

## Physical scene envelopes

| Workload | reps | presents | FPS | interval p95≤/max ms | present p95≤/max ms | CPU0/1 % | min free/largest B |
| --- | --- | --- | --- | --- | --- | --- | --- |
| radio-init/ble/menu | 3 | 885 | 29.50 | 35.95/35.95 | 28.07/28.07 | 6.3/85.7 | 77592/47092 |
| radio-init/ble/fullscreen-layered | 3 | 681 | 22.70 | 45.22/45.22 | 43.01/43.01 | 6.3/97.5 | 77392/47092 |

Free/largest heap values are minima of retained samples in each scope, not continuous allocation traces. Boot-wide heap low water and task-lifetime stack low water are separate fields in the summary; they cannot identify a per-scene leak alone.

## Touchscreen polling and control dispatch

NOT RUN / no completed observations in this capture.

The selection includes the menu baseline, the slowest captured fullscreen scope per orientation and scripted-touch cases. Polling rate is the measured `touch_poll_us` observation count divided by target steady-window elapsed time: actual XPT2046 `getPoint` calls, including polls with no contact. It is an average call rate, not a guaranteed polling interval or physical touch response rate. `touch_dispatch_us` counts control-dispatch invocations, including no-contact samples and synthetic input; it does not count distinct physical touches or generated application keys. Scripted replay is firmware injection. No controlled physical contact stimulus, contact-to-photon acquisition or physical latency distribution was measured.

## Audio scheduling

Configured audio: 24000 Hz, 128 samples/block (5333.333 µs nominal block period). Audio task core/priority 0/5; PCM writer core/priority 1/2. I2S inventory: 4 DMA buffers × 128 frames, 2 slots, DAC GPIO 26.

| Workload | callback n | callback p95≤/max µs | lock max µs | mix max µs | enqueue p95≤/max µs | period p99≤/max µs | late/write/short |
| --- | --- | --- | --- | --- | --- | --- | --- |
| radio-init/ble/menu | 5625 | 2/2 | 5 | 27 | 5948/5948 | 6095/6095 | 1/0/0 |
| radio-init/ble/fullscreen-layered | 5625 | 256/274 | 4 | 25 | 5949/5949 | 6266/6266 | 1/0/0 |

The table includes explicit audio/combined/stress workloads and the scopes with the largest observed callback, cadence, lock or enqueue tails. Enqueue is the duration of blocking `i2s_write`, including waiting for DMA capacity. It is not an isolated compute cost. Late-period counters are inferred scheduling events. DMA underruns and analog output continuity are **unknown** without an appropriate detector/acquisition fixture. The complete lock, callback, overlay, mix, enqueue and cadence distributions are in [summary-metrics.csv](summary-metrics.csv).

## USB PCM integrity

NOT RUN / no completed observations in this capture.

PCM receive windows include warmup, the steady target window, a 250 ms guard and stop-tail draining. Target counters cover the steady target window only. Missing sequence numbers can originate in a producer queue or transport; compare target queue/drop records before attribution. Loss before the first received sequence and after the last frame is unknown. Host arrival timing includes UART/OS buffering and cannot establish target audio cadence or DMA underruns. Raw per-run PCM binaries can be replayed with `capture.py decode-pcm`.

## Sustained CPU load and temperature

NOT RUN / no completed observations in this capture.

Recorded sensor method: `RTC_library_raw_undocumented_uncalibrated`. The sensor API is undocumented and its temperature accuracy is unsupported. Raw readings are uncalibrated indications. The reported °C values are the firmware's conversion, not independently calibrated die temperature. Ambient temperature was NOT MEASURED. Retained intervals, raw ticks and all sensor samples are available in [samples.csv](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/ble-init-link-01/samples.csv`). The confirmation predicate requires a completed ≥360-second window, ≥360 seconds of retained sample span, valid CPU hooks and ≥95% weighted busy time on both cores.

Stress iteration counters are unsigned 32-bit modular progress indicators, incremented in batches of 256; they can wrap modulo 2^32. They are not throughput totals. Timed windows and retained raw per-core tick observations support the sustained-load assessment.

Across this capture, **54** retained in-window samples and outside-kernel/SPI snapshots span raw sensor codes **128–128**. These indications show the recorded trend without supplying an accuracy or calibration claim.

## SPI and portable kernels

NOT RUN / no completed observations in this capture.

SPI uses precomputed RGB565 eight-row stripes, includes commands and completion waits, and excludes artwork generation. It measures this transfer path, not an abstract SPI clock-rate ceiling.

NOT RUN / no completed observations in this capture.

Kernel timings pool exact chronological samples and include RTOS preemption. Output inspection is excluded. Working bytes describe kernel state; they are not the bytes that a remote node would need per request. The same seed and checksum checks support reproduction, while float/fixed-point precision comparisons remain separate validation evidence. Physical S3/C3 comparative execution is NOT RUN unless separately evidenced; a successful target build is not a hardware measurement.

## Actual scene and allocation receipts

| Workload | scope | actual configuration before measurement |
| --- | --- | --- |
| radio-init/ble/fullscreen-layered | 54fdde5b657d | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |

Visions receipts are emitted after configuration and before the next frame. For native fullscreen trails the implementation uses bounded geometric paths and releases pixel history/tunnel on its first fullscreen frame; receipt-time allocated bytes must not be presented as measured fullscreen history occupancy. Sampled heap provides the in-window evidence. These paths are not numerically identical rendering kernels.

## Wireless scope

Radio rows measure isolated controller initialization and its effect on local scenes. They perform no peer discovery, association or traffic. Radio-off acknowledgements record requested software state; disable/deinitialize return statuses and actual RF emissions are not independently exported or measured. This is a requested-state baseline, not independently verified RF-off operation. No measured goodput, RTT, jitter/loss under traffic, or Wi-Fi/BLE/ESP-NOW coexistence envelope is established by these rows.

| Initialized mode/local scene | reps | FPS | CPU0/1 % | min free B | min largest B |
| --- | --- | --- | --- | --- | --- |
| radio-init/ble/menu | 3 | 29.50 | 6.3/85.7 | 77592 | 47092 |
| radio-init/ble/fullscreen-layered | 3 | 22.70 | 6.3/97.5 | 77392 | 47092 |

## I2S driver event capability

| Run | Workload | enabled | available | observed | TX_Q_OVF | DMA_ERROR | TX_DONE | queue-full observations | drain-limit hits | max drain gap µs |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| r001_1 | radio-init/ble/menu | True | True | True | 0 | 0 | 1875 | 0 | 0 | 6095 |
| r002_2 | radio-init/ble/menu | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5371 |
| r003_3 | radio-init/ble/menu | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5381 |
| r004_1 | radio-init/ble/fullscreen-layered | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5378 |
| r005_2 | radio-init/ble/fullscreen-layered | True | True | True | 0 | 0 | 1875 | 0 | 0 | 6267 |
| r006_3 | radio-init/ble/fullscreen-layered | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5371 |

These are directly observed legacy I2S driver indications. TX_Q_OVF means the driver's free-TX-buffer queue was full; the raw schema's legacy `tx_underflow_indications` field counts that enum and is not a measured analog gap count. DMA_ERROR is a descriptor fault. Assignment to a measurement window is by event-drain completion time, not ISR generation time. Finite driver queues and polling can lose indications; queue/drain diagnostics bound visibility but do not establish an exact loss count. Zero indications do not prove analog continuity.

## Evidence-based recommendations

* Use scene-specific display budgets: the lowest completed cadence among the captured ordinary scopes is **22.70 FPS** for `radio-init/ble/fullscreen-layered` across 3 repetitions. Use the measured tail/maximum durations and actual path when choosing VIBEWARE visual complexity; do not apply the menu rate to layered fullscreen effects.
* The largest observed main audio callback is **274 µs** in `radio-init/ble/fullscreen-layered` (n=5625, nominal block period 5333.333 µs). Keep callback, lock, overlay, mix and blocking enqueue as separate budgets; verify correlated per-block deadlines and analog continuity before claiming an underrun-free audio envelope.
* Offload break-even requires local compute to exceed remote compute, packing/unpacking, queueing and measured transport for the actual input/output message shapes. A full matched-payload RTT already includes its payload transfer: do not add that transmission a second time. A fixed-overhead/throughput model instead needs independently estimated fixed overhead and directional byte-transfer costs. No numeric minimum useful ratio can be established from controller-init-only radio tests.
* Reserve memory using the observed free/largest-block minima and allocation receipts for the intended combination of effects. Saved-palette gradients, history format, fullscreen path and radio initialization are workload settings, not incidental details. Repeat matching scene transitions and long sessions before diagnosing a leak or guaranteeing a memory margin.

## Missing evidence and reproduction

NOT RUN / no completed observations in this capture.

Current unfinished planned repetitions: 0. Missing observations stay absent rather than receiving theoretical estimates.

Reproduce with the captured plan, identified board, exact firmware digest and matching source/toolchain receipts. The harness never flashes. Owner authorization, private backup verification and the flash receipt are separate evidence. The on-board identity does not independently attest the firmware binary digest; preserve the flash/build chain.

```powershell
python tools/perf/capture.py analyze evidence/physical/BLE controller initialization only/results.json
python tools/perf/summarize.py evidence/physical/BLE controller initialization only/results.json
python tools/perf/report.py evidence/physical/BLE controller initialization only/results.json --label BLE controller initialization only
```

Artifacts: [results.json](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/ble-init-link-01/results.json`), [raw target JSONL](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/ble-init-link-01/target.jsonl`), [raw serial bytes](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/ble-init-link-01/serial-rx.bin`), [timestamped serial events](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/ble-init-link-01/serial-events.jsonl`), [metrics.csv](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/ble-init-link-01/metrics.csv`), [samples.csv](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/ble-init-link-01/samples.csv`), [summary.json](summary.json), [summary scenes](summary-scenes.csv), [summary metrics](summary-metrics.csv), [summary kernels](summary-kernels.csv), [artifact checksums](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/ble-init-link-01/artifact-hashes.json`).

## Plots

![fps-radio](figures/fps-radio.png)

## Publication bundle

This compact folder derives from the **completed** `BLE controller initialization only` capture and preserves status **CAPTURE COMPLETE**. The [raw capture archive](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) has SHA256 `15dcd4b987177ca3c8c7e504a1e9a41eef1f342414519519ce9610b3100cf8a7`; its `physical/ble-init-link-01/` members preserve the full JSON, samples, target telemetry, serial RX, PCM and event logs. Extract the archive into `evidence/` while retaining its `physical/` prefix before replaying the commands above. [stress-samples.csv](stress-samples.csv) contains 0 stress-only rows with source/firmware/profile identity, target timestamps, raw ticks, CPU percentages, sensor readings, heap and stack. An empty table means that no stress workload was present in this source capture. [manifest.json](manifest.json) records input/output hashes and archive-member verification.
