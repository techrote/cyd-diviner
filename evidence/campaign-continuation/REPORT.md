# CYD performance evidence — continuation-01

Capture status: **FAILED**. Completed physical runs: **19/85**; pending or incomplete: **66**. Explicit NOT RUN workload classes: **11**.

Board: CYD on COM21 (owner identified), ESP32-D0WD-V3 revision 3.1. Source [a4549f87cef05d19d9acf96d59ae95a2435ebb67](https://github.com/techrote/cyd-diviner/commit/a4549f87cef05d19d9acf96d59ae95a2435ebb67); profile `cyd-profile`; firmware SHA256 `fb35d75725b15ecd8b6aceb368492cf76ebaee85bbb4aeb50bd61311e229f15c`. Input results SHA256 `0fe7607d7276050cbc1a2cf7f84c1d3182aba8e0b225c9f6d13b1dc1a3ffcfc4`.

CPU 240 MHz, 2 cores; flash 4194304 bytes at 40000000 Hz; PSRAM 0 bytes. LCD SPI write/read 40000000/8000000 Hz; serial 533333 baud. SDK `v4.4.7-dirty`.

Fixed plan seed: `23063`. Ordinary requested warmup/window: 2000/10000 ms; default repetitions: 3. Per-case overrides remain in [results.json](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/continuation-01/results.json`). Reset policy: `fresh_audio_DSP_state_each_entry_seeded_no_song_persistence`. Audio-window alignment: `real_setup_and_warmup_elapsed_not_sample_aligned`.

Saved display settings at initial inventory: theme 2, background style 1, gradient mode 1, canvas depth 16 bits. Orientation/backdrop commands then override the inventory values per workload. The exact palette, intensity table, setup acknowledgements and actual scene receipts are retained in JSON/raw logs.

## Interpretation

FPS is completed physical presentations divided by the target window, with the existing 33 ms frame cap. CPU percentages use tick-sampled current-task versus idle-task classification, not cycle-accurate task or interrupt accounting. Direct rendering interleaves CPU drawing and SPI transfer; its present duration cannot be separated into independent raster and bus costs. Histogram percentiles below are **upper bounds**. Human tables tighten each bound to `min(raw powers-of-two bucket upper, exact observed maximum)`; a capped bound equal to the maximum is still a bound, not an exact percentile. Raw target records and summary CSVs retain the original bucket upper bounds unchanged. Maxima are exact observed durations. A p99 is omitted below 100 observations. Percentiles and maxima describe these finite runs; they are not deadline guarantees.

Only repetitions with matching requested settings, duration, warmup, instrumentation and actual scene/allocation receipts are pooled. A scope suffix identifies variants if the same workload name has differing receipts. Independent maxima or percentiles must not be added as if they came from the same audio block.

Instrumentation A/B scope: `histogram_insertion_only_timestamps_hooks_counts_retained`. The enable switch controls histogram insertion while timestamps, hooks, frame/block counts and retained sampling remain. The on/off rows therefore measure that insertion change only. Total overhead relative to an ordinary firmware binary is not established by this switch. Disabled histograms give unavailable timing distributions rather than zero-duration work. The approved primary image reserves 24,008 bytes in its static histogram/sample arrays alone; other profiler state, queues and task stacks are additional. These arrays remain with insertion disabled. Reported heap describes the profiling image and workload history; ordinary firmware headroom cannot be inferred by subtracting only this footprint. Detailed source and method audits are in the repository's `evidence/method-notes.md`.

## Physical scene envelopes

| Workload | reps | presents | FPS | interval p95≤/max ms | present p95≤/max ms | CPU0/1 % | min free/largest B |
| --- | --- | --- | --- | --- | --- | --- | --- |
| radio-init/espnow/menu | 3 | 882 | 29.40 | 35.91/35.91 | 28.16/28.16 | 6.3/86.4 | 46404/29684 |
| radio-init/espnow/fullscreen-layered | 3 | 683 | 22.77 | 45.18/45.18 | 42.39/42.39 | 6.3/97.3 | 46404/29684 |
| touch/scripted-menu-rapid | 3 | 882 | 29.40 | 35.94/35.94 | 28.14/28.14 | 6.2/86.4 | 79724/31732 |
| touch/visions-fullscreen-transitions | 3 | 675 | 22.50 | 65.54/74.01 | 40.62/40.62 | 6.3/96.2 | 37884/13300 |
| sustained/cpu-both-cores | 1 | 6304 | 17.27 | 65.54/67.34 | 56.30/56.30 | 99.0/100.0 | 74876/31732 |
| post-stress/visions-fullscreen-layered | 3 | 682 | 22.73 | 45.18/45.18 | 42.43/42.43 | 6.3/97.6 | 74876/31732 |

Free/largest heap values are minima of retained samples in each scope, not continuous allocation traces. Boot-wide heap low water and task-lifetime stack low water are separate fields in the summary; they cannot identify a per-scene leak alone.

## Touchscreen polling and control dispatch

| Selected scope | reps | XPT polls n | observed polls/s | poll p95≤/max µs | dispatch n | dispatch p95≤/max µs |
| --- | --- | --- | --- | --- | --- | --- |
| touch/scripted-menu-rapid | 3 | 882 | 29.40 | 215/215 | 900 | 8/36 |
| touch/visions-fullscreen-transitions | 3 | 675 | 22.50 | 321/321 | 759 | 64/114 |

The selection includes the menu baseline, the slowest captured fullscreen scope per orientation and scripted-touch cases. Polling rate is the measured `touch_poll_us` observation count divided by target steady-window elapsed time: actual XPT2046 `getPoint` calls, including polls with no contact. It is an average call rate, not a guaranteed polling interval or physical touch response rate. `touch_dispatch_us` counts control-dispatch invocations, including no-contact samples and synthetic input; it does not count distinct physical touches or generated application keys. Scripted replay is firmware injection. No controlled physical contact stimulus, contact-to-photon acquisition or physical latency distribution was measured.

## Audio scheduling

Configured audio: 24000 Hz, 128 samples/block (5333.333 µs nominal block period). Audio task core/priority 0/5; PCM writer core/priority 1/2. I2S inventory: 4 DMA buffers × 128 frames, 2 slots, DAC GPIO 26.

| Workload | callback n | callback p95≤/max µs | lock max µs | mix max µs | enqueue p95≤/max µs | period p99≤/max µs | late/write/short |
| --- | --- | --- | --- | --- | --- | --- | --- |
| radio-init/espnow/menu | 5625 | 2/2 | 6 | 23 | 5224/5224 | 5346/5346 | 0/0/0 |
| radio-init/espnow/fullscreen-layered | 5625 | 240/240 | 4 | 29 | 5117/5117 | 5428/5428 | 0/0/0 |
| touch/visions-fullscreen-transitions | 5625 | 238/238 | 4 | 29 | 5091/5091 | 5399/5399 | 0/0/0 |
| sustained/cpu-both-cores | 68437 | 2/4 | 6 | 26 | 5237/5237 | 5359/5359 | 0/0/0 |
| post-stress/visions-fullscreen-layered | 5625 | 240/240 | 4 | 29 | 5033/5033 | 5357/5357 | 0/0/0 |

The table includes explicit audio/combined/stress workloads and the scopes with the largest observed callback, cadence, lock or enqueue tails. Enqueue is the duration of blocking `i2s_write`, including waiting for DMA capacity. It is not an isolated compute cost. Late-period counters are inferred scheduling events. DMA underruns and analog output continuity are **unknown** without an appropriate detector/acquisition fixture. The complete lock, callback, overlay, mix, enqueue and cadence distributions are in [summary-metrics.csv](summary-metrics.csv).

## USB PCM integrity

NOT RUN / no completed observations in this capture.

PCM receive windows include warmup, the steady target window, a 250 ms guard and stop-tail draining. Target counters cover the steady target window only. Missing sequence numbers can originate in a producer queue or transport; compare target queue/drop records before attribution. Loss before the first received sequence and after the last frame is unknown. Host arrival timing includes UART/OS buffering and cannot establish target audio cadence or DMA underruns. Raw per-run PCM binaries can be replayed with `capture.py decode-pcm`.

## Sustained CPU load and temperature

| Run | target s | n | sample span s | CPU0/1 % | ≥360s near100 confirmed | raw min/max | uncal °C min/max |
| --- | --- | --- | --- | --- | --- | --- | --- |
| r013_1 | 365.000 | 355 | 362.939 | 98.99/99.96 | True | 128/128 | 53.333/53.333 |

Recorded sensor method: `ESP32_ROM_raw_uncalibrated`. The approved primary a4549f8 image's legacy tag says ROM, while its linked implementation resolves through SDK `librtc.a` / `rtc_analog.o`; the tag does not establish a mask-ROM implementation. The sensor API is undocumented and its temperature accuracy is unsupported. Raw readings are uncalibrated indications. The reported °C values are the firmware's conversion, not independently calibrated die temperature. Ambient temperature was NOT MEASURED. Retained intervals, raw ticks and all sensor samples are available in [samples.csv](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/continuation-01/samples.csv`). The confirmation predicate requires a completed ≥360-second window, ≥360 seconds of retained sample span, valid CPU hooks and ≥95% weighted busy time on both cores.

Stress iteration counters are unsigned 32-bit modular progress indicators, incremented in batches of 256; they can wrap modulo 2^32. They are not throughput totals. Timed windows and retained raw per-core tick observations support the sustained-load assessment.

Across this capture, **496** retained in-window samples and outside-kernel/SPI snapshots span raw sensor codes **128–151**. These indications show the recorded trend without supplying an accuracy or calibration claim.

## SPI and portable kernels

| SPI workload | reps | n | bytes/frame | median ms | p95 ms | p99 ms | max ms | median MB/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| spi/full-frame-rgb565 | 3 | 384 | 153600 | 32.28 | 32.29 | 32.29 | 32.29 | 4.76 |

SPI uses precomputed RGB565 eight-row stripes, includes commands and completion waits, and excludes artwork generation. It measures this transfer path, not an abstract SPI clock-rate ceiling.

NOT RUN / no completed observations in this capture.

Kernel timings pool exact chronological samples and include RTOS preemption. Output inspection is excluded. Working bytes describe kernel state; they are not the bytes that a remote node would need per request. The same seed and checksum checks support reproduction, while float/fixed-point precision comparisons remain separate validation evidence. Physical S3/C3 comparative execution is NOT RUN unless separately evidenced; a successful target build is not a hardware measurement.

## Actual scene and allocation receipts

| Workload | scope | actual configuration before measurement |
| --- | --- | --- |
| radio-init/espnow/fullscreen-layered | 8951abfc1838 | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| touch/visions-fullscreen-transitions | 9ead73360c78 | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| post-stress/visions-fullscreen-layered | 908780633ca5 | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |

Visions receipts are emitted after configuration and before the next frame. For native fullscreen trails the implementation uses bounded geometric paths and releases pixel history/tunnel on its first fullscreen frame; receipt-time allocated bytes must not be presented as measured fullscreen history occupancy. Sampled heap provides the in-window evidence. These paths are not numerically identical rendering kernels.

## Wireless scope

Radio rows measure isolated controller initialization and its effect on local scenes. They perform no peer discovery, association or traffic. Radio-off acknowledgements record requested software state; disable/deinitialize return statuses and actual RF emissions are not independently exported or measured. This is a requested-state baseline, not independently verified RF-off operation. No measured goodput, RTT, jitter/loss under traffic, or Wi-Fi/BLE/ESP-NOW coexistence envelope is established by these rows.

| Initialized mode/local scene | reps | FPS | CPU0/1 % | min free B | min largest B |
| --- | --- | --- | --- | --- | --- |
| radio-init/espnow/menu | 3 | 29.40 | 6.3/86.4 | 46404 | 29684 |
| radio-init/espnow/fullscreen-layered | 3 | 22.77 | 6.3/97.3 | 46404 | 29684 |

## I2S driver event capability

Driver event queue capability reported: `UNKNOWN / not exposed in this image`. No direct I2S event records occur in this capture. TX_Q_OVF and DMA_ERROR counts are unknown; the primary image's write-error/short-write counters and inferred late periods do not substitute for them. A supplementary events image must retain its own source/firmware identity and evidence folder.

## Evidence-based recommendations

* Use scene-specific display budgets: the lowest completed cadence among the captured ordinary scopes is **22.73 FPS** for `post-stress/visions-fullscreen-layered` across 3 repetitions. Use the measured tail/maximum durations and actual path when choosing VIBEWARE visual complexity; do not apply the menu rate to layered fullscreen effects.
* The largest observed main audio callback is **240 µs** in `radio-init/espnow/fullscreen-layered` (n=5625, nominal block period 5333.333 µs). Keep callback, lock, overlay, mix and blocking enqueue as separate budgets; verify correlated per-block deadlines and analog continuity before claiming an underrun-free audio envelope.
* Budget full RGB565 transfers using the measured stripe path: median **32.28 ms** and p99 **32.29 ms** for 153600 bytes (n=384). Artwork computation adds to or interleaves with this transfer work according to the path.
* Offload break-even requires local compute to exceed remote compute, packing/unpacking, queueing and measured transport for the actual input/output message shapes. A full matched-payload RTT already includes its payload transfer: do not add that transmission a second time. A fixed-overhead/throughput model instead needs independently estimated fixed overhead and directional byte-transfer costs. No numeric minimum useful ratio can be established from controller-init-only radio tests.
* Reserve memory using the observed free/largest-block minima and allocation receipts for the intended combination of effects. Saved-palette gradients, history format, fullscreen path and radio initialization are workload settings, not incidental details. Repeat matching scene transitions and long sessions before diagnosing a leak or guaranteeing a memory margin.

## Missing evidence and reproduction

| Workload class | status | reason |
| --- | --- | --- |
| radio-init/ble/menu | NOT RUN | Initial a4549f8 BLE-controller setup returned radio mode unsupported; failed attempt retained in full-01. Further BLE investigation uses separately identified firmware. |
| radio-init/ble/fullscreen-layered | NOT RUN | Initial a4549f8 BLE-controller setup returned radio mode unsupported; failed attempt retained in full-01. Further BLE investigation uses separately identified firmware. |
| physical/contact-to-photon | NOT RUN | Requires owner-operated physical touch stimulus and external optical timing fixture. |
| audio/usb-only-dac-disabled | NOT RUN | Existing PCM streams the same mixed blocks sent to the local DAC; independent DAC-disable path is not assumed. |
| audio/external-dac-loopback | NOT RUN | External acquisition fixture required to detect analog gaps/distortion independently. |
| wireless/wifi-goodput-rtt | NOT RUN | Requires explicitly authorized owned AP/peer and configured traffic endpoint; no network probes are automatic. |
| wireless/ble-goodput-rtt | NOT RUN | Requires explicitly authorized BLE peer; no scans/probes of unrelated devices. |
| wireless/espnow-goodput-rtt | NOT RUN | Requires explicitly authorized ESP-NOW peer and owned channel. |
| wireless/coexistence | NOT RUN | Requires explicitly authorized Wi-Fi/BLE/ESP-NOW peers and separately captured local-workload baseline. |
| portable/esp32-s3-physical | NOT RUN | Physical S3 board identity/backup/flash authorization required; compile-only is distinct evidence. |
| portable/esp32-c3-physical | NOT RUN | Physical C3 board identity/backup/flash authorization required; compile-only is distinct evidence. |

Current unfinished planned repetitions: 66. Missing observations stay absent rather than receiving theoretical estimates.

Reproduce with the captured plan, identified board, exact firmware digest and matching source/toolchain receipts. The harness never flashes. Owner authorization, private backup verification and the flash receipt are separate evidence. The on-board identity does not independently attest the firmware binary digest; preserve the flash/build chain.

```powershell
python tools/perf/capture.py analyze evidence/physical/continuation-01/results.json
python tools/perf/summarize.py evidence/physical/continuation-01/results.json
python tools/perf/report.py evidence/physical/continuation-01/results.json --label continuation-01
```

Artifacts: [results.json](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/continuation-01/results.json`), [raw target JSONL](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/continuation-01/target.jsonl`), [raw serial bytes](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/continuation-01/serial-rx.bin`), [timestamped serial events](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/continuation-01/serial-events.jsonl`), [metrics.csv](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/continuation-01/metrics.csv`), [samples.csv](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/continuation-01/samples.csv`), [summary.json](summary.json), [summary scenes](summary-scenes.csv), [summary metrics](summary-metrics.csv), [summary kernels](summary-kernels.csv), [artifact checksums](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/continuation-01/artifact-hashes.json`).

## Plots

![audio-tails](figures/audio-tails.png)

![fps-radio](figures/fps-radio.png)

![fps-scenes](figures/fps-scenes.png)

![stress-temperature](figures/stress-temperature.png)

## Publication bundle

This compact folder derives from the **terminal partial** `continuation-01` capture and preserves status **FAILED**. Only independently completed MEASURED windows are summarized; this is not a completed campaign. The [raw capture archive](../PHYSICAL-CAPTURES-BASELINE.zip) has SHA256 `41f122a120c5f4207f6d30b01d3c0b4be67728a200ba89154fa3d957d38a7797`; its `physical/continuation-01/` members preserve the full JSON, samples, target telemetry, serial RX, PCM and event logs. Extract the archive into `evidence/` while retaining its `physical/` prefix before replaying the commands above. [stress-samples.csv](stress-samples.csv) contains 355 stress-only rows with source/firmware/profile identity, target timestamps, raw ticks, CPU percentages, sensor readings, heap and stack. An empty table means that no stress workload was present in this source capture. [manifest.json](manifest.json) records input/output hashes and archive-member verification.
