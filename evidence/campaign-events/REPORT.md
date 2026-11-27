# CYD performance evidence — I2S events and 365-second stress supplement

Capture status: **CAPTURE COMPLETE**. Completed physical runs: **31/31**; pending or incomplete: **0**. Explicit NOT RUN workload classes: **0**.

Board: CYD on COM21 (owner identified), ESP32-D0WD-V3 revision 3.1. Source [174a79670d4b0da656ebd615f0cd1620aa0ddbb1](https://github.com/techrote/cyd-diviner/commit/174a79670d4b0da656ebd615f0cd1620aa0ddbb1); profile `cyd-profile-link`; firmware SHA256 `a980f7e2b9409686ae2d325ebeddfd525966f06e8ded03b2d8a723b36771ab11`. Input results SHA256 `3f342828ff90ef36feb68f815a8e600713385f5ab46d0c9235de9ad36bde54d4`.

CPU 240 MHz, 2 cores; flash 4194304 bytes at 40000000 Hz; PSRAM 0 bytes. LCD SPI write/read 40000000/8000000 Hz; serial 533333 baud. SDK `v4.4.7-dirty`.

Fixed plan seed: `23063`. Ordinary requested warmup/window: 2000/10000 ms; default repetitions: 3. Per-case overrides remain in [results.json](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/events-link-01/results.json`). Reset policy: `fresh_audio_DSP_state_each_entry_seeded_no_song_persistence`. Audio-window alignment: `real_setup_and_warmup_elapsed_not_sample_aligned`.

Saved display settings at initial inventory: theme 2, background style 1, gradient mode 1, canvas depth 16 bits. Orientation/backdrop commands then override the inventory values per workload. The exact palette, intensity table, setup acknowledgements and actual scene receipts are retained in JSON/raw logs.

## Interpretation

FPS is completed physical presentations divided by the target window, with the existing 33 ms frame cap. CPU percentages use tick-sampled current-task versus idle-task classification, not cycle-accurate task or interrupt accounting. Direct rendering interleaves CPU drawing and SPI transfer; its present duration cannot be separated into independent raster and bus costs. Histogram percentiles below are **upper bounds**. Human tables tighten each bound to `min(raw powers-of-two bucket upper, exact observed maximum)`; a capped bound equal to the maximum is still a bound, not an exact percentile. Raw target records and summary CSVs retain the original bucket upper bounds unchanged. Maxima are exact observed durations. A p99 is omitted below 100 observations. Percentiles and maxima describe these finite runs; they are not deadline guarantees.

Only repetitions with matching requested settings, duration, warmup, instrumentation and actual scene/allocation receipts are pooled. A scope suffix identifies variants if the same workload name has differing receipts. Independent maxima or percentiles must not be added as if they came from the same audio block.

Instrumentation A/B scope: `histogram_insertion_only_timestamps_hooks_counts_retained`. The enable switch controls histogram insertion while timestamps, hooks, frame/block counts and retained sampling remain. The on/off rows therefore measure that insertion change only. Total overhead relative to an ordinary firmware binary is not established by this switch. Disabled histograms give unavailable timing distributions rather than zero-duration work. The approved primary image reserves 24,008 bytes in its static histogram/sample arrays alone; other profiler state, queues and task stacks are additional. These arrays remain with insertion disabled. Reported heap describes the profiling image and workload history; ordinary firmware headroom cannot be inferred by subtracting only this footprint. Detailed source and method audits are in the repository's `evidence/method-notes.md`.

## Physical scene envelopes

| Workload | reps | presents | FPS | interval p95≤/max ms | present p95≤/max ms | CPU0/1 % | min free/largest B |
| --- | --- | --- | --- | --- | --- | --- | --- |
| menu/landscape/theme | 3 | 884 | 29.47 | 35.94/35.94 | 28.09/28.09 | 6.3/85.7 | 95656/47092 |
| audio/drone/landscape/local | 3 | 882 | 29.40 | 34.13/34.13 | 24.49/24.49 | 12.5/82.4 | 79052/47092 |
| audio/seq/landscape/local | 3 | 882 | 29.40 | 34.20/34.20 | 24.65/24.65 | 13.0/79.5 | 77436/29684 |
| audio/beat/landscape/local | 3 | 744 | 24.80 | 41.15/41.15 | 24.44/24.44 | 12.5/96.7 | 92436/47092 |
| audio/drone/local-and-pcm | 3 | 913 | 30.43 | 35.23/35.23 | 25.52/25.52 | 12.5/91.6 | 76032/30708 |
| visions/fullscreen/layered/pcm | 3 | 615 | 20.50 | 50.60/50.60 | 47.59/47.59 | 6.6/97.8 | 92436/47092 |
| combined/fullscreen/gate-trails/drone | 3 | 681 | 22.70 | 45.28/45.28 | 42.64/42.64 | 12.5/97.4 | 76032/30708 |
| touch/visions-fullscreen-transitions | 3 | 675 | 22.50 | 65.54/73.76 | 40.64/40.64 | 6.3/96.1 | 50596/32756 |
| sustained/cpu-both-cores | 1 | 6326 | 17.33 | 65.54/65.93 | 56.12/56.12 | 99.0/100.0 | 87588/47092 |
| instrumentation/off/fullscreen-layered | 3 | 681 | 22.70 | —/— | —/— | 6.2/97.6 | 87588/47092 |
| instrumentation/on/fullscreen-layered | 3 | 681 | 22.70 | 45.21/45.21 | 42.67/42.67 | 6.3/97.3 | 87588/47092 |

Free/largest heap values are minima of retained samples in each scope, not continuous allocation traces. Boot-wide heap low water and task-lifetime stack low water are separate fields in the summary; they cannot identify a per-scene leak alone.

## Touchscreen polling and control dispatch

| Selected scope | reps | XPT polls n | observed polls/s | poll p95≤/max µs | dispatch n | dispatch p95≤/max µs |
| --- | --- | --- | --- | --- | --- | --- |
| menu/landscape/theme | 3 | 884 | 29.47 | 173/173 | 884 | 16/20 |
| touch/visions-fullscreen-transitions | 3 | 675 | 22.50 | 308/308 | 759 | 64/115 |

The selection includes the menu baseline, the slowest captured fullscreen scope per orientation and scripted-touch cases. Polling rate is the measured `touch_poll_us` observation count divided by target steady-window elapsed time: actual XPT2046 `getPoint` calls, including polls with no contact. It is an average call rate, not a guaranteed polling interval or physical touch response rate. `touch_dispatch_us` counts control-dispatch invocations, including no-contact samples and synthetic input; it does not count distinct physical touches or generated application keys. Scripted replay is firmware injection. No controlled physical contact stimulus, contact-to-photon acquisition or physical latency distribution was measured.

## Audio scheduling

Configured audio: 24000 Hz, 128 samples/block (5333.333 µs nominal block period). Audio task core/priority 0/5; PCM writer core/priority 1/2. I2S inventory: 4 DMA buffers × 128 frames, 2 slots, DAC GPIO 26.

| Workload | callback n | callback p95≤/max µs | lock max µs | mix max µs | enqueue p95≤/max µs | period p99≤/max µs | late/write/short |
| --- | --- | --- | --- | --- | --- | --- | --- |
| menu/landscape/theme | 5625 | 2/2 | 5 | 25 | 5224/5224 | 5378/5378 | 0/0/0 |
| audio/drone/landscape/local | 5625 | 512/536 | 6 | 25 | 4741/4741 | 5364/5364 | 0/0/0 |
| audio/seq/landscape/local | 5625 | 512/790 | 6 | 35 | 4718/4718 | 5361/5361 | 0/0/0 |
| audio/beat/landscape/local | 5625 | 325/325 | 6 | 25 | 4902/4902 | 5359/5359 | 0/0/0 |
| audio/drone/local-and-pcm | 5625 | 512/552 | 6 | 29 | 4752/4752 | 5395/5395 | 0/0/0 |
| combined/fullscreen/gate-trails/drone | 5625 | 512/560 | 7 | 31 | 4733/4733 | 5374/5374 | 0/0/0 |
| sustained/cpu-both-cores | 68437 | 2/4 | 7 | 27 | 5231/5231 | 5355/5355 | 0/0/0 |

The table includes explicit audio/combined/stress workloads and the scopes with the largest observed callback, cadence, lock or enqueue tails. Enqueue is the duration of blocking `i2s_write`, including waiting for DMA capacity. It is not an isolated compute cost. Late-period counters are inferred scheduling events. DMA underruns and analog output continuity are **unknown** without an appropriate detector/acquisition fixture. The complete lock, callback, overlay, mix, enqueue and cadence distributions are in [summary-metrics.csv](summary-metrics.csv).

## USB PCM integrity

| Run | Workload | valid | CRC | missing | duplicate | backward | discarded/tail B |
| --- | --- | --- | --- | --- | --- | --- | --- |
| r013_1 | audio/drone/local-and-pcm | 2298 | 0 | 0 | 0 | 0 | 0/0 |
| r014_2 | audio/drone/local-and-pcm | 2300 | 0 | 0 | 0 | 0 | 0/0 |
| r015_3 | audio/drone/local-and-pcm | 2298 | 0 | 0 | 0 | 0 | 0/0 |
| r016_1 | visions/fullscreen/layered/pcm | 2298 | 0 | 0 | 0 | 0 | 0/0 |
| r017_2 | visions/fullscreen/layered/pcm | 2291 | 0 | 0 | 0 | 0 | 0/0 |
| r018_3 | visions/fullscreen/layered/pcm | 2297 | 0 | 0 | 0 | 0 | 0/0 |

PCM receive windows include warmup, the steady target window, a 250 ms guard and stop-tail draining. Target counters cover the steady target window only. Missing sequence numbers can originate in a producer queue or transport; compare target queue/drop records before attribution. Loss before the first received sequence and after the last frame is unknown. Host arrival timing includes UART/OS buffering and cannot establish target audio cadence or DMA underruns. Raw per-run PCM binaries can be replayed with `capture.py decode-pcm`.

## Sustained CPU load and temperature

| Run | target s | n | sample span s | CPU0/1 % | ≥360s near100 confirmed | raw min/max | uncal °C min/max |
| --- | --- | --- | --- | --- | --- | --- | --- |
| r025_1 | 365.000 | 356 | 363.605 | 98.99/99.95 | True | 139/146 | 59.444/63.333 |

Recorded sensor method: `RTC_library_raw_undocumented_uncalibrated`. The sensor API is undocumented and its temperature accuracy is unsupported. Raw readings are uncalibrated indications. The reported °C values are the firmware's conversion, not independently calibrated die temperature. Ambient temperature was NOT MEASURED. Retained intervals, raw ticks and all sensor samples are available in [samples.csv](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/events-link-01/samples.csv`). The confirmation predicate requires a completed ≥360-second window, ≥360 seconds of retained sample span, valid CPU hooks and ≥95% weighted busy time on both cores.

Stress iteration counters are unsigned 32-bit modular progress indicators, incremented in batches of 256; they can wrap modulo 2^32. They are not throughput totals. Timed windows and retained raw per-core tick observations support the sustained-load assessment.

Across this capture, **626** retained in-window samples and outside-kernel/SPI snapshots span raw sensor codes **135–146**. These indications show the recorded trend without supplying an accuracy or calibration claim.

## SPI and portable kernels

NOT RUN / no completed observations in this capture.

SPI uses precomputed RGB565 eight-row stripes, includes commands and completion waits, and excludes artwork generation. It measures this transfer path, not an abstract SPI clock-rate ceiling.

NOT RUN / no completed observations in this capture.

Kernel timings pool exact chronological samples and include RTOS preemption. Output inspection is excluded. Working bytes describe kernel state; they are not the bytes that a remote node would need per request. The same seed and checksum checks support reproduction, while float/fixed-point precision comparisons remain separate validation evidence. Physical S3/C3 comparative execution is NOT RUN unless separately evidenced; a successful target build is not a hardware measurement.

## Actual scene and allocation receipts

| Workload | scope | actual configuration before measurement |
| --- | --- | --- |
| audio/drone/landscape/local | 1671e68464fe | scene=drone, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, root_midi=38, scale=0, voices=5, brightness=0.45, drift=0.5, wind=True, bells=True, delay_samples=4096, delay_allocated=True |
| audio/seq/landscape/local | 34dc05fe3759 | scene=seq, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, bpm=124, swing=0.08, bass_root_midi=33, playing=True, pattern=0, pattern_fields_fnv1a32=3283383751, echo_samples=4500, echo_allocated=True |
| audio/beat/landscape/local | d0ed3be028ad | scene=beat, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, preset=0, source=t*(t>>5\|t>>8), bytebeat_rate_hz=8000, sample_counter=0 |
| audio/drone/local-and-pcm | a233caefe746 | scene=drone, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, root_midi=38, scale=0, voices=5, brightness=0.45, drift=0.5, wind=True, bells=True, delay_samples=4096, delay_allocated=True |
| visions/fullscreen/layered/pcm | db8ffd2b8460 | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| combined/fullscreen/gate-trails/drone | 9c066ad951d6 | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False; scene=drone, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, root_midi=38, scale=0, voices=5, brightness=0.45, drift=0.5, wind=True, bells=True, delay_samples=4096, delay_allocated=True |
| touch/visions-fullscreen-transitions | 9ead73360c78 | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| instrumentation/off/fullscreen-layered | 8dbbe9b83e9e | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| instrumentation/on/fullscreen-layered | deb02233ef2f | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |

Visions receipts are emitted after configuration and before the next frame. For native fullscreen trails the implementation uses bounded geometric paths and releases pixel history/tunnel on its first fullscreen frame; receipt-time allocated bytes must not be presented as measured fullscreen history occupancy. Sampled heap provides the in-window evidence. These paths are not numerically identical rendering kernels.

## Wireless scope

Radio rows measure isolated controller initialization and its effect on local scenes. They perform no peer discovery, association or traffic. Radio-off acknowledgements record requested software state; disable/deinitialize return statuses and actual RF emissions are not independently exported or measured. This is a requested-state baseline, not independently verified RF-off operation. No measured goodput, RTT, jitter/loss under traffic, or Wi-Fi/BLE/ESP-NOW coexistence envelope is established by these rows.

NOT RUN / no completed observations in this capture.

## I2S driver event capability

| Run | Workload | enabled | available | observed | TX_Q_OVF | DMA_ERROR | TX_DONE | queue-full observations | drain-limit hits | max drain gap µs |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| r001_1 | menu/landscape/theme | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5356 |
| r002_2 | menu/landscape/theme | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5348 |
| r003_3 | menu/landscape/theme | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5347 |
| r004_1 | audio/drone/landscape/local | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5355 |
| r005_2 | audio/drone/landscape/local | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5356 |
| r006_3 | audio/drone/landscape/local | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5356 |
| r007_1 | audio/seq/landscape/local | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5347 |
| r008_2 | audio/seq/landscape/local | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5353 |
| r009_3 | audio/seq/landscape/local | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5353 |
| r010_1 | audio/beat/landscape/local | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5359 |
| r011_2 | audio/beat/landscape/local | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5347 |
| r012_3 | audio/beat/landscape/local | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5346 |
| r013_1 | audio/drone/local-and-pcm | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5348 |
| r014_2 | audio/drone/local-and-pcm | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5350 |
| r015_3 | audio/drone/local-and-pcm | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5396 |
| r016_1 | visions/fullscreen/layered/pcm | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5352 |
| r017_2 | visions/fullscreen/layered/pcm | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5369 |
| r018_3 | visions/fullscreen/layered/pcm | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5350 |
| r019_1 | combined/fullscreen/gate-trails/drone | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5366 |
| r020_2 | combined/fullscreen/gate-trails/drone | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5352 |
| r021_3 | combined/fullscreen/gate-trails/drone | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5357 |
| r022_1 | touch/visions-fullscreen-transitions | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5361 |
| r023_2 | touch/visions-fullscreen-transitions | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5347 |
| r024_3 | touch/visions-fullscreen-transitions | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5368 |
| r025_1 | sustained/cpu-both-cores | True | True | True | 0 | 0 | 68437 | 0 | 0 | 5355 |
| r026_1 | instrumentation/off/fullscreen-layered | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5347 |
| r027_2 | instrumentation/off/fullscreen-layered | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5347 |
| r028_3 | instrumentation/off/fullscreen-layered | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5347 |
| r029_1 | instrumentation/on/fullscreen-layered | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5349 |
| r030_2 | instrumentation/on/fullscreen-layered | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5352 |
| r031_3 | instrumentation/on/fullscreen-layered | True | True | True | 0 | 0 | 1875 | 0 | 0 | 5348 |

These are directly observed legacy I2S driver indications. TX_Q_OVF means the driver's free-TX-buffer queue was full; the raw schema's legacy `tx_underflow_indications` field counts that enum and is not a measured analog gap count. DMA_ERROR is a descriptor fault. Assignment to a measurement window is by event-drain completion time, not ISR generation time. Finite driver queues and polling can lose indications; queue/drain diagnostics bound visibility but do not establish an exact loss count. Zero indications do not prove analog continuity.

## Evidence-based recommendations

* Use scene-specific display budgets: the lowest completed cadence among the captured ordinary scopes is **20.50 FPS** for `visions/fullscreen/layered/pcm` across 3 repetitions. Use the measured tail/maximum durations and actual path when choosing VIBEWARE visual complexity; do not apply the menu rate to layered fullscreen effects.
* The largest observed main audio callback is **790 µs** in `audio/seq/landscape/local` (n=5625, nominal block period 5333.333 µs). Keep callback, lock, overlay, mix and blocking enqueue as separate budgets; verify correlated per-block deadlines and analog continuity before claiming an underrun-free audio envelope.
* Offload break-even requires local compute to exceed remote compute, packing/unpacking, queueing and measured transport for the actual input/output message shapes. A full matched-payload RTT already includes its payload transfer: do not add that transmission a second time. A fixed-overhead/throughput model instead needs independently estimated fixed overhead and directional byte-transfer costs. No numeric minimum useful ratio can be established from controller-init-only radio tests.
* Reserve memory using the observed free/largest-block minima and allocation receipts for the intended combination of effects. Saved-palette gradients, history format, fullscreen path and radio initialization are workload settings, not incidental details. Repeat matching scene transitions and long sessions before diagnosing a leak or guaranteeing a memory margin.

## Missing evidence and reproduction

NOT RUN / no completed observations in this capture.

Current unfinished planned repetitions: 0. Missing observations stay absent rather than receiving theoretical estimates.

Reproduce with the captured plan, identified board, exact firmware digest and matching source/toolchain receipts. The harness never flashes. Owner authorization, private backup verification and the flash receipt are separate evidence. The on-board identity does not independently attest the firmware binary digest; preserve the flash/build chain.

```powershell
python tools/perf/capture.py analyze evidence/physical/I2S events and 365-second stress supplement/results.json
python tools/perf/summarize.py evidence/physical/I2S events and 365-second stress supplement/results.json
python tools/perf/report.py evidence/physical/I2S events and 365-second stress supplement/results.json --label I2S events and 365-second stress supplement
```

Artifacts: [results.json](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/events-link-01/results.json`), [raw target JSONL](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/events-link-01/target.jsonl`), [raw serial bytes](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/events-link-01/serial-rx.bin`), [timestamped serial events](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/events-link-01/serial-events.jsonl`), [metrics.csv](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/events-link-01/metrics.csv`), [samples.csv](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/events-link-01/samples.csv`), [summary.json](summary.json), [summary scenes](summary-scenes.csv), [summary metrics](summary-metrics.csv), [summary kernels](summary-kernels.csv), [artifact checksums](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (archive member `physical/events-link-01/artifact-hashes.json`).

## Plots

![audio-tails](figures/audio-tails.png)

![fps-audio](figures/fps-audio.png)

![fps-radio](figures/fps-radio.png)

![fps-scenes](figures/fps-scenes.png)

![stress-temperature](figures/stress-temperature.png)

## Publication bundle

This compact folder derives from the **completed** `I2S events and 365-second stress supplement` capture and preserves status **CAPTURE COMPLETE**. The [raw capture archive](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) has SHA256 `15dcd4b987177ca3c8c7e504a1e9a41eef1f342414519519ce9610b3100cf8a7`; its `physical/events-link-01/` members preserve the full JSON, samples, target telemetry, serial RX, PCM and event logs. Extract the archive into `evidence/` while retaining its `physical/` prefix before replaying the commands above. [stress-samples.csv](stress-samples.csv) contains 356 stress-only rows with source/firmware/profile identity, target timestamps, raw ticks, CPU percentages, sensor readings, heap and stack. An empty table means that no stress workload was present in this source capture. [manifest.json](manifest.json) records input/output hashes and archive-member verification.
