# CYD performance evidence — full-01

Capture status: **FAILED**. Completed physical runs: **195/286**; pending or incomplete: **91**. Explicit NOT RUN workload classes: **9**.

Board: CYD on COM21 (owner identified), ESP32-D0WD-V3 revision 3.1. Source [a4549f87cef05d19d9acf96d59ae95a2435ebb67](https://github.com/techrote/cyd-diviner/commit/a4549f87cef05d19d9acf96d59ae95a2435ebb67); profile `cyd-profile`; firmware SHA256 `fb35d75725b15ecd8b6aceb368492cf76ebaee85bbb4aeb50bd61311e229f15c`. Input results SHA256 `9adfeeca8f4ab91c8b3b3e6722128b913bda571218c16be720d702fbdbdbf715`.

CPU 240 MHz, 2 cores; flash 4194304 bytes at 40000000 Hz; PSRAM 0 bytes. LCD SPI write/read 40000000/8000000 Hz; serial 533333 baud. SDK `v4.4.7-dirty`.

Fixed plan seed: `23063`. Ordinary requested warmup/window: 2000/10000 ms; default repetitions: 3. Per-case overrides remain in [results.json](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/full-01/results.json`). Reset policy: `fresh_audio_DSP_state_each_entry_seeded_no_song_persistence`. Audio-window alignment: `real_setup_and_warmup_elapsed_not_sample_aligned`.

Saved display settings at initial inventory: theme 2, background style 1, gradient mode 1, canvas depth 16 bits. Orientation/backdrop commands then override the inventory values per workload. The exact palette, intensity table, setup acknowledgements and actual scene receipts are retained in JSON/raw logs.

## Interpretation

FPS is completed physical presentations divided by the target window, with the existing 33 ms frame cap. CPU percentages use tick-sampled current-task versus idle-task classification, not cycle-accurate task or interrupt accounting. Direct rendering interleaves CPU drawing and SPI transfer; its present duration cannot be separated into independent raster and bus costs. Histogram percentiles below are **upper bounds**. Human tables tighten each bound to `min(raw powers-of-two bucket upper, exact observed maximum)`; a capped bound equal to the maximum is still a bound, not an exact percentile. Raw target records and summary CSVs retain the original bucket upper bounds unchanged. Maxima are exact observed durations. A p99 is omitted below 100 observations. Percentiles and maxima describe these finite runs; they are not deadline guarantees.

Only repetitions with matching requested settings, duration, warmup, instrumentation and actual scene/allocation receipts are pooled. A scope suffix identifies variants if the same workload name has differing receipts. Independent maxima or percentiles must not be added as if they came from the same audio block.

Instrumentation A/B scope: `histogram_insertion_only_timestamps_hooks_counts_retained`. The enable switch controls histogram insertion while timestamps, hooks, frame/block counts and retained sampling remain. The on/off rows therefore measure that insertion change only. Total overhead relative to an ordinary firmware binary is not established by this switch. Disabled histograms give unavailable timing distributions rather than zero-duration work. The approved primary image reserves 24,008 bytes in its static histogram/sample arrays alone; other profiler state, queues and task stacks are additional. These arrays remain with insertion disabled. Reported heap describes the profiling image and workload history; ordinary firmware headroom cannot be inferred by subtracting only this footprint. Detailed source and method audits are in the repository's `evidence/method-notes.md`.

## Physical scene envelopes

| Workload | reps | presents | FPS | interval p95≤/max ms | present p95≤/max ms | CPU0/1 % | min free/largest B |
| --- | --- | --- | --- | --- | --- | --- | --- |
| menu/landscape/theme | 3 | 882 | 29.40 | 35.91/35.91 | 28.14/28.14 | 6.3/86.2 | 99408/47092 |
| menu/landscape/rain | 3 | 824 | 27.47 | 37.83/37.83 | 32.01/32.01 | 6.2/96.1 | 89152/47092 |
| menu/landscape/ashfire | 3 | 696 | 23.20 | 44.14/44.14 | 37.89/37.89 | 6.2/97.5 | 88116/47092 |
| menu/landscape/plasma | 3 | 714 | 23.80 | 43.08/43.08 | 35.94/35.94 | 6.2/95.3 | 89152/47092 |
| menu/portrait/theme | 3 | 885 | 29.50 | 36.34/36.34 | 25.58/25.58 | 6.3/70.7 | 99408/47092 |
| menu/portrait/rain | 3 | 824 | 27.47 | 37.77/37.77 | 31.97/31.97 | 6.3/96.0 | 89152/47092 |
| menu/portrait/ashfire | 3 | 735 | 24.50 | 42.13/42.13 | 35.52/35.52 | 6.3/95.4 | 88028/47092 |
| menu/portrait/plasma | 3 | 732 | 24.40 | 42.08/42.08 | 35.42/35.42 | 6.2/97.5 | 89064/47092 |
| visions/landscape/ui/plasma | 3 | 651 | 21.70 | 47.14/47.14 | 24.38/24.38 | 6.3/97.7 | 99320/47092 |
| visions/landscape/ui/gate | 3 | 732 | 24.40 | 41.15/41.15 | 24.41/24.41 | 6.3/95.1 | 85384/47092 |
| visions/landscape/ui/ashfire | 3 | 732 | 24.40 | 42.12/42.12 | 24.37/24.37 | 6.2/97.5 | 92220/47092 |
| visions/landscape/ui/rain | 3 | 887 | 29.57 | 35.20/35.20 | 24.39/24.39 | 6.3/87.2 | 99320/47092 |
| visions/landscape/ui/kaleido | 3 | 906 | 30.20 | 34.16/34.16 | 24.45/24.45 | 6.3/81.6 | 99320/47092 |
| visions/landscape/ui/trails/rgb444 | 3 | 750 | 25.00 | 41.54/41.54 | 24.46/24.46 | 6.3/96.2 | 57496/36852 |
| visions/landscape/ui/trails/rgb565 | 3 | 855 | 28.50 | 36.59/36.59 | 24.45/24.45 | 6.2/95.5 | 43576/13300 |
| visions/landscape/ui/plasma-kaleido | 3 | 624 | 20.80 | 49.14/49.14 | 24.49/24.49 | 6.2/95.9 | 99320/47092 |
| visions/landscape/ui/gate-trails | 3 | 651 | 21.70 | 47.22/47.22 | 24.45/24.45 | 6.3/95.7 | 57480/32756 |
| visions/landscape/ui/ashfire-rain | 3 | 654 | 21.80 | 48.07/48.07 | 24.44/24.44 | 6.2/96.3 | 92220/47092 |
| visions/landscape/fullscreen/plasma | 3 | 855 | 28.50 | 35.06/35.06 | 32.74/32.74 | 6.2/94.4 | 99320/47092 |
| visions/landscape/fullscreen/gate | 3 | 768 | 25.60 | 40.18/40.18 | 37.53/37.53 | 6.2/97.3 | 99320/47092 |
| visions/landscape/fullscreen/ashfire | 3 | 834 | 27.80 | 37.03/37.03 | 34.61/34.61 | 6.2/94.5 | 92220/47092 |
| visions/landscape/fullscreen/rain | 3 | 882 | 29.40 | 35.08/35.08 | 31.76/31.76 | 6.2/91.3 | 99320/47092 |
| visions/landscape/fullscreen/kaleido | 3 | 882 | 29.40 | 35.04/35.04 | 31.46/31.46 | 6.3/91.3 | 99320/47092 |
| visions/landscape/fullscreen/trails/rgb444 | 3 | 884 | 29.47 | 35.01/35.01 | 31.38/31.38 | 6.3/91.3 | 99320/47092 |
| visions/landscape/fullscreen/trails/rgb565 | 3 | 883 | 29.43 | 34.06/34.06 | 31.36/31.36 | 6.2/91.3 | 99320/47092 |
| visions/landscape/fullscreen/plasma-kaleido | 3 | 834 | 27.80 | 37.06/37.06 | 34.11/34.11 | 6.3/94.5 | 99320/47092 |
| visions/landscape/fullscreen/gate-trails | 3 | 681 | 22.70 | 45.17/45.17 | 42.40/42.40 | 6.3/97.2 | 99320/47092 |
| visions/landscape/fullscreen/ashfire-rain | 3 | 811 | 27.03 | 38.57/38.57 | 36.52/36.52 | 6.2/95.3 | 92220/47092 |
| visions/portrait/ui/plasma | 3 | 855 | 28.50 | 36.15/36.15 | 13.37/13.37 | 6.3/97.0 | 99320/47092 |
| visions/portrait/ui/gate | 3 | 884 | 29.47 | 34.15/34.15 | 13.41/13.41 | 6.3/82.4 | 85384/47092 |
| visions/portrait/ui/ashfire | 3 | 882 | 29.40 | 35.13/35.13 | 13.40/13.40 | 6.3/84.6 | 92220/47092 |
| visions/portrait/ui/rain | 3 | 886 | 29.53 | 35.12/35.12 | 13.40/13.40 | 6.3/54.8 | 99320/47092 |
| visions/portrait/ui/kaleido | 3 | 891 | 29.70 | 35.14/35.14 | 13.43/13.43 | 6.2/47.6 | 99320/47092 |
| visions/portrait/ui/trails/rgb444 | 3 | 883 | 29.43 | 35.48/35.48 | 13.42/13.42 | 6.2/80.7 | 57496/36852 |
| visions/portrait/ui/trails/rgb565 | 3 | 882 | 29.40 | 34.87/34.87 | 13.42/13.42 | 6.3/65.6 | 43576/13300 |
| visions/portrait/ui/plasma-kaleido | 3 | 810 | 27.00 | 37.76/37.76 | 13.43/13.43 | 6.3/94.6 | 99320/47092 |
| visions/portrait/ui/gate-trails | 3 | 860 | 28.67 | 36.03/36.03 | 13.41/13.41 | 6.3/94.8 | 57480/32756 |
| visions/portrait/ui/ashfire-rain | 3 | 864 | 28.80 | 37.14/37.14 | 13.46/13.46 | 6.3/95.0 | 92220/47092 |
| visions/portrait/fullscreen/plasma | 3 | 855 | 28.50 | 35.10/35.10 | 32.68/32.68 | 6.2/94.4 | 99320/47092 |
| visions/portrait/fullscreen/gate | 3 | 750 | 25.00 | 41.16/41.16 | 38.56/38.56 | 6.2/97.4 | 99320/47092 |
| visions/portrait/fullscreen/ashfire | 3 | 834 | 27.80 | 37.02/37.02 | 34.48/34.48 | 6.2/94.5 | 92220/47092 |
| visions/portrait/fullscreen/rain | 3 | 882 | 29.40 | 35.08/35.08 | 31.69/31.69 | 6.3/91.2 | 99320/47092 |
| visions/portrait/fullscreen/kaleido | 3 | 882 | 29.40 | 35.05/35.05 | 31.49/31.49 | 6.3/91.3 | 99320/47092 |
| visions/portrait/fullscreen/trails/rgb444 | 3 | 884 | 29.47 | 34.05/34.05 | 31.41/31.41 | 6.3/91.3 | 99320/47092 |
| visions/portrait/fullscreen/trails/rgb565 | 3 | 882 | 29.40 | 35.02/35.02 | 31.39/31.39 | 6.2/91.3 | 99320/47092 |
| visions/portrait/fullscreen/plasma-kaleido | 3 | 826 | 27.53 | 37.73/37.73 | 34.71/34.71 | 6.3/96.4 | 99320/47092 |
| visions/portrait/fullscreen/gate-trails | 3 | 651 | 21.70 | 46.21/46.21 | 43.89/43.89 | 6.3/95.7 | 99320/47092 |
| visions/portrait/fullscreen/ashfire-rain | 3 | 807 | 26.90 | 39.08/39.08 | 37.20/37.20 | 6.3/95.5 | 92220/47092 |
| audio/drone/landscape/local | 3 | 882 | 29.40 | 35.16/35.16 | 24.56/24.56 | 12.5/85.3 | 82916/30708 |
| audio/drone/portrait/local | 3 | 882 | 29.40 | 35.14/35.14 | 13.54/13.54 | 12.5/53.0 | 82916/30708 |
| audio/drone/local-and-pcm | 3 | 888 | 29.60 | 35.38/35.38 | 25.45/25.45 | 12.5/91.6 | 82916/30708 |
| audio/seq/landscape/local | 3 | 883 | 29.43 | 35.15/35.15 | 24.62/24.62 | 12.7/79.4 | 81300/29684 |
| audio/seq/portrait/local | 3 | 882 | 29.40 | 35.12/35.12 | 13.59/13.59 | 12.7/47.1 | 81300/29684 |
| audio/seq/local-and-pcm | 3 | 888 | 29.60 | 35.32/35.32 | 25.36/25.36 | 12.6/83.1 | 81300/29684 |
| audio/beat/landscape/local | 3 | 750 | 25.00 | 41.16/41.16 | 24.46/24.46 | 12.5/96.5 | 96300/47092 |
| audio/beat/portrait/local | 3 | 880 | 29.33 | 35.18/35.18 | 13.44/13.44 | 12.5/80.6 | 96300/47092 |
| audio/beat/local-and-pcm | 3 | 720 | 24.00 | 42.84/42.84 | 25.24/25.24 | 12.5/97.0 | 96300/47092 |
| visions/fullscreen/layered/pcm | 3 | 625 | 20.83 | 48.90/48.90 | 46.13/46.13 | 6.2/97.8 | 96300/47092 |
| combined/fullscreen/gate-trails/drone | 3 | 681 | 22.70 | 45.15/45.15 | 42.38/42.38 | 12.5/97.0 | 79896/30708 |
| combined/fullscreen/gate-trails/seq | 3 | 681 | 22.70 | 45.16/45.16 | 42.37/42.37 | 12.6/96.8 | 78280/29684 |
| combined/fullscreen/gate-trails/beat | 3 | 682 | 22.73 | 45.14/45.14 | 42.34/42.34 | 12.5/96.4 | 96300/47092 |
| instrumentation/off/menu | 3 | 882 | 29.40 | —/— | —/— | 6.2/86.1 | 96300/47092 |
| instrumentation/on/menu | 3 | 883 | 29.43 | 35.93/35.93 | 28.14/28.14 | 6.3/86.3 | 96300/47092 |
| radio-init/wifi/menu | 3 | 882 | 29.40 | 35.84/35.84 | 28.14/28.14 | 6.3/86.4 | 46700/31732 |
| radio-init/wifi/fullscreen-layered | 3 | 681 | 22.70 | 45.18/45.18 | 42.39/42.39 | 6.3/97.2 | 46700/31732 |

Free/largest heap values are minima of retained samples in each scope, not continuous allocation traces. Boot-wide heap low water and task-lifetime stack low water are separate fields in the summary; they cannot identify a per-scene leak alone.

## Touchscreen polling and control dispatch

| Selected scope | reps | XPT polls n | observed polls/s | poll p95≤/max µs | dispatch n | dispatch p95≤/max µs |
| --- | --- | --- | --- | --- | --- | --- |
| menu/landscape/theme | 3 | 882 | 29.40 | 158/158 | 882 | 8/12 |
| visions/landscape/fullscreen/gate-trails | 3 | 681 | 22.70 | 314/314 | 681 | 21/21 |
| visions/portrait/fullscreen/gate-trails | 3 | 651 | 21.70 | 301/301 | 651 | 16/18 |

The selection includes the menu baseline, the slowest captured fullscreen scope per orientation and scripted-touch cases. Polling rate is the measured `touch_poll_us` observation count divided by target steady-window elapsed time: actual XPT2046 `getPoint` calls, including polls with no contact. It is an average call rate, not a guaranteed polling interval or physical touch response rate. `touch_dispatch_us` counts control-dispatch invocations, including no-contact samples and synthetic input; it does not count distinct physical touches or generated application keys. Scripted replay is firmware injection. No controlled physical contact stimulus, contact-to-photon acquisition or physical latency distribution was measured.

## Audio scheduling

Configured audio: 24000 Hz, 128 samples/block (5333.333 µs nominal block period). Audio task core/priority 0/5; PCM writer core/priority 1/2. I2S inventory: 4 DMA buffers × 128 frames, 2 slots, DAC GPIO 26.

| Workload | callback n | callback p95≤/max µs | lock max µs | mix max µs | enqueue p95≤/max µs | period p99≤/max µs | late/write/short |
| --- | --- | --- | --- | --- | --- | --- | --- |
| menu/landscape/rain | 5625 | 2/2 | 6 | 25 | 5236/5236 | 5350/5350 | 0/0/0 |
| visions/landscape/ui/kaleido | 5625 | 215/215 | 7 | 23 | 5029/5029 | 5342/5342 | 0/0/0 |
| audio/drone/landscape/local | 5625 | 512/523 | 4 | 23 | 4754/4754 | 5342/5342 | 0/0/0 |
| audio/drone/portrait/local | 5625 | 512/520 | 4 | 23 | 4749/4749 | 5340/5340 | 0/0/0 |
| audio/drone/local-and-pcm | 5625 | 503/503 | 10 | 22 | 4756/4756 | 5360/5360 | 0/0/0 |
| audio/seq/landscape/local | 5625 | 512/759 | 4 | 30 | 4769/4769 | 5356/5356 | 0/0/0 |
| audio/seq/portrait/local | 5625 | 512/752 | 4 | 30 | 4760/4760 | 5347/5347 | 0/0/0 |
| audio/seq/local-and-pcm | 5625 | 512/753 | 4 | 22 | 4750/4750 | 5345/5345 | 0/0/0 |
| audio/beat/landscape/local | 5625 | 325/325 | 4 | 23 | 4907/4907 | 5342/5342 | 0/0/0 |
| audio/beat/portrait/local | 5625 | 324/324 | 6 | 23 | 4917/4917 | 5347/5347 | 0/0/0 |
| audio/beat/local-and-pcm | 5625 | 324/324 | 7 | 22 | 4963/4963 | 5407/5407 | 0/0/0 |
| combined/fullscreen/gate-trails/drone | 5625 | 512/521 | 4 | 25 | 4775/4775 | 5368/5368 | 0/0/0 |
| combined/fullscreen/gate-trails/seq | 5625 | 512/763 | 4 | 29 | 4778/4778 | 5366/5366 | 0/0/0 |
| combined/fullscreen/gate-trails/beat | 5625 | 324/324 | 4 | 24 | 4909/4909 | 5342/5342 | 0/0/0 |
| radio-init/wifi/menu | 5625 | 2/2 | 4 | 26 | 5239/5239 | 5384/5384 | 0/0/0 |

The table includes explicit audio/combined/stress workloads and the scopes with the largest observed callback, cadence, lock or enqueue tails. Enqueue is the duration of blocking `i2s_write`, including waiting for DMA capacity. It is not an isolated compute cost. Late-period counters are inferred scheduling events. DMA underruns and analog output continuity are **unknown** without an appropriate detector/acquisition fixture. The complete lock, callback, overlay, mix, enqueue and cadence distributions are in [summary-metrics.csv](summary-metrics.csv).

## USB PCM integrity

| Run | Workload | valid | CRC | missing | duplicate | backward | discarded/tail B |
| --- | --- | --- | --- | --- | --- | --- | --- |
| r151_1 | audio/drone/local-and-pcm | 2296 | 0 | 0 | 0 | 0 | 0/0 |
| r152_2 | audio/drone/local-and-pcm | 2292 | 0 | 0 | 0 | 0 | 0/0 |
| r153_3 | audio/drone/local-and-pcm | 2293 | 0 | 0 | 0 | 0 | 0/0 |
| r160_1 | audio/seq/local-and-pcm | 2297 | 0 | 0 | 0 | 0 | 0/0 |
| r161_2 | audio/seq/local-and-pcm | 2296 | 0 | 0 | 0 | 0 | 0/0 |
| r162_3 | audio/seq/local-and-pcm | 2292 | 0 | 0 | 0 | 0 | 0/0 |
| r169_1 | audio/beat/local-and-pcm | 2295 | 0 | 0 | 0 | 0 | 0/0 |
| r170_2 | audio/beat/local-and-pcm | 2296 | 0 | 0 | 0 | 0 | 0/0 |
| r171_3 | audio/beat/local-and-pcm | 2295 | 0 | 0 | 0 | 0 | 0/0 |
| r172_1 | visions/fullscreen/layered/pcm | 2295 | 0 | 0 | 0 | 0 | 0/0 |
| r173_2 | visions/fullscreen/layered/pcm | 2295 | 0 | 0 | 0 | 0 | 0/0 |
| r174_3 | visions/fullscreen/layered/pcm | 2295 | 0 | 0 | 0 | 0 | 0/0 |

PCM receive windows include warmup, the steady target window, a 250 ms guard and stop-tail draining. Target counters cover the steady target window only. Missing sequence numbers can originate in a producer queue or transport; compare target queue/drop records before attribution. Loss before the first received sequence and after the last frame is unknown. Host arrival timing includes UART/OS buffering and cannot establish target audio cadence or DMA underruns. Raw per-run PCM binaries can be replayed with `capture.py decode-pcm`.

## Sustained CPU load and temperature

NOT RUN / no completed observations in this capture.

Recorded sensor method: `ESP32_ROM_raw_uncalibrated`. The approved primary a4549f8 image's legacy tag says ROM, while its linked implementation resolves through SDK `librtc.a` / `rtc_analog.o`; the tag does not establish a mask-ROM implementation. The sensor API is undocumented and its temperature accuracy is unsupported. Raw readings are uncalibrated indications. The reported °C values are the firmware's conversion, not independently calibrated die temperature. Ambient temperature was NOT MEASURED. Retained intervals, raw ticks and all sensor samples are available in [samples.csv](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/full-01/samples.csv`). The confirmation predicate requires a completed ≥360-second window, ≥360 seconds of retained sample span, valid CPU hooks and ≥95% weighted busy time on both cores.

Stress iteration counters are unsigned 32-bit modular progress indicators, incremented in batches of 256; they can wrap modulo 2^32. They are not throughput totals. Timed windows and retained raw per-core tick observations support the sustained-load assessment.

Across this capture, **1755** retained in-window samples and outside-kernel/SPI snapshots span raw sensor codes **130–151**. These indications show the recorded trend without supplying an accuracy or calibration claim.

## SPI and portable kernels

NOT RUN / no completed observations in this capture.

SPI uses precomputed RGB565 eight-row stripes, includes commands and completion waits, and excludes artwork generation. It measures this transfer path, not an abstract SPI clock-rate ceiling.

NOT RUN / no completed observations in this capture.

Kernel timings pool exact chronological samples and include RTOS preemption. Output inspection is excluded. Working bytes describe kernel state; they are not the bytes that a remote node would need per request. The same seed and checksum checks support reproduction, while float/fixed-point precision comparisons remain separate validation evidence. Physical S3/C3 comparative execution is NOT RUN unless separately evidenced; a successful target build is not a hardware measurement.

## Actual scene and allocation receipts

| Workload | scope | actual configuration before measurement |
| --- | --- | --- |
| visions/landscape/ui/plasma | a73c4518edf5 | actual_base_effect=plasma, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/ui/gate | c6ca1984d42e | actual_base_effect=gate, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/ui/ashfire | 14834e246fd2 | actual_base_effect=ashfire, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=True, fire_payload_bytes=7081, native_fullscreen_at_receipt=False |
| visions/landscape/ui/rain | a8fbea40fbe7 | actual_base_effect=rain, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/ui/kaleido | 1d2677a249b7 | actual_base_effect=kaleido, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/ui/trails/rgb444 | ce528461c161 | actual_base_effect=trails, actual_layered=False, history_requested=True, history_ready=True, history_format=RGB444, history_payload_bytes=41760, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/ui/trails/rgb565 | 7d3e8b90f8cd | actual_base_effect=trails, actual_layered=False, history_requested=True, history_ready=True, history_format=RGB565, history_payload_bytes=55680, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/ui/plasma-kaleido | bd04bc485715 | actual_base_effect=plasma, actual_layered=True, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/ui/gate-trails | 9efd5378632e | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/ui/ashfire-rain | 57af6efb0f52 | actual_base_effect=ashfire, actual_layered=True, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=True, fire_payload_bytes=7081, native_fullscreen_at_receipt=False |
| visions/landscape/fullscreen/plasma | 32bd71bf0e21 | actual_base_effect=plasma, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/fullscreen/gate | 858a9eb38b4a | actual_base_effect=gate, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/fullscreen/ashfire | 877dcc17fad5 | actual_base_effect=ashfire, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=True, fire_payload_bytes=7081, native_fullscreen_at_receipt=False |
| visions/landscape/fullscreen/rain | 06fdc03079b5 | actual_base_effect=rain, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/fullscreen/kaleido | f34482cb0c14 | actual_base_effect=kaleido, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/fullscreen/trails/rgb444 | 29cbf2edf20a | actual_base_effect=trails, actual_layered=False, history_requested=True, history_ready=True, history_format=RGB444, history_payload_bytes=41760, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/fullscreen/trails/rgb565 | c718bde908d2 | actual_base_effect=trails, actual_layered=False, history_requested=True, history_ready=True, history_format=RGB565, history_payload_bytes=55680, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/fullscreen/plasma-kaleido | 3d7dcb5f11de | actual_base_effect=plasma, actual_layered=True, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/fullscreen/gate-trails | a71fa7b4bd63 | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/landscape/fullscreen/ashfire-rain | 49c13c6f2914 | actual_base_effect=ashfire, actual_layered=True, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=True, fire_payload_bytes=7081, native_fullscreen_at_receipt=False |
| visions/portrait/ui/plasma | db578d876164 | actual_base_effect=plasma, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/ui/gate | b678de18484c | actual_base_effect=gate, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/ui/ashfire | f11ef215c38b | actual_base_effect=ashfire, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=True, fire_payload_bytes=7081, native_fullscreen_at_receipt=False |
| visions/portrait/ui/rain | 7c0192226e66 | actual_base_effect=rain, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/ui/kaleido | 6961006b684d | actual_base_effect=kaleido, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/ui/trails/rgb444 | 5068aa10fd46 | actual_base_effect=trails, actual_layered=False, history_requested=True, history_ready=True, history_format=RGB444, history_payload_bytes=41760, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/ui/trails/rgb565 | 8cb723775abd | actual_base_effect=trails, actual_layered=False, history_requested=True, history_ready=True, history_format=RGB565, history_payload_bytes=55680, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/ui/plasma-kaleido | dec3a55f295a | actual_base_effect=plasma, actual_layered=True, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/ui/gate-trails | 8467f8dbda98 | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/ui/ashfire-rain | b026d6e84aad | actual_base_effect=ashfire, actual_layered=True, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=True, fire_payload_bytes=7081, native_fullscreen_at_receipt=False |
| visions/portrait/fullscreen/plasma | 9aad80684ba2 | actual_base_effect=plasma, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/fullscreen/gate | 321fa01ecf06 | actual_base_effect=gate, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/fullscreen/ashfire | 952de0b3926e | actual_base_effect=ashfire, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=True, fire_payload_bytes=7081, native_fullscreen_at_receipt=False |
| visions/portrait/fullscreen/rain | 9e5e50c2f1f5 | actual_base_effect=rain, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/fullscreen/kaleido | 3d4a70854d08 | actual_base_effect=kaleido, actual_layered=False, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/fullscreen/trails/rgb444 | 909d6b2cd186 | actual_base_effect=trails, actual_layered=False, history_requested=True, history_ready=True, history_format=RGB444, history_payload_bytes=41760, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/fullscreen/trails/rgb565 | 194e96a2087f | actual_base_effect=trails, actual_layered=False, history_requested=True, history_ready=True, history_format=RGB565, history_payload_bytes=55680, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/fullscreen/plasma-kaleido | 0ba6a3c4ca05 | actual_base_effect=plasma, actual_layered=True, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/fullscreen/gate-trails | 299e74b5fe52 | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| visions/portrait/fullscreen/ashfire-rain | 6447a6855ebe | actual_base_effect=ashfire, actual_layered=True, history_requested=False, history_ready=False, history_format=NONE, history_payload_bytes=0, tunnel_allocated=False, tunnel_payload_bytes=0, fire_allocated=True, fire_payload_bytes=7081, native_fullscreen_at_receipt=False |
| audio/drone/landscape/local | 1671e68464fe | scene=drone, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, root_midi=38, scale=0, voices=5, brightness=0.45, drift=0.5, wind=True, bells=True, delay_samples=4096, delay_allocated=True |
| audio/drone/portrait/local | 63868926491c | scene=drone, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, root_midi=38, scale=0, voices=5, brightness=0.45, drift=0.5, wind=True, bells=True, delay_samples=4096, delay_allocated=True |
| audio/drone/local-and-pcm | a233caefe746 | scene=drone, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, root_midi=38, scale=0, voices=5, brightness=0.45, drift=0.5, wind=True, bells=True, delay_samples=4096, delay_allocated=True |
| audio/seq/landscape/local | 34dc05fe3759 | scene=seq, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, bpm=124, swing=0.08, bass_root_midi=33, playing=True, pattern=0, pattern_fields_fnv1a32=3283383751, echo_samples=4500, echo_allocated=True |
| audio/seq/portrait/local | 42ebeb264315 | scene=seq, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, bpm=124, swing=0.08, bass_root_midi=33, playing=True, pattern=0, pattern_fields_fnv1a32=3283383751, echo_samples=4500, echo_allocated=True |
| audio/seq/local-and-pcm | eaf767acfc3c | scene=seq, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, bpm=124, swing=0.08, bass_root_midi=33, playing=True, pattern=0, pattern_fields_fnv1a32=3283383751, echo_samples=4500, echo_allocated=True |
| audio/beat/landscape/local | d0ed3be028ad | scene=beat, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, preset=0, source=t*(t>>5\|t>>8), bytebeat_rate_hz=8000, sample_counter=0 |
| audio/beat/portrait/local | d9367869e0e1 | scene=beat, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, preset=0, source=t*(t>>5\|t>>8), bytebeat_rate_hz=8000, sample_counter=0 |
| audio/beat/local-and-pcm | 3d0da6aa5aeb | scene=beat, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, preset=0, source=t*(t>>5\|t>>8), bytebeat_rate_hz=8000, sample_counter=0 |
| visions/fullscreen/layered/pcm | db8ffd2b8460 | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |
| combined/fullscreen/gate-trails/drone | 9c066ad951d6 | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False; scene=drone, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, root_midi=38, scale=0, voices=5, brightness=0.45, drift=0.5, wind=True, bells=True, delay_samples=4096, delay_allocated=True |
| combined/fullscreen/gate-trails/seq | 614f6fb725e8 | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False; scene=seq, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, bpm=124, swing=0.08, bass_root_midi=33, playing=True, pattern=0, pattern_fields_fnv1a32=3283383751, echo_samples=4500, echo_allocated=True |
| combined/fullscreen/gate-trails/beat | 6bc75abcd75f | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False; scene=beat, seed=23063, reset=fresh_DSP_state_each_entry, song_persistence=False, preset=0, source=t*(t>>5\|t>>8), bytebeat_rate_hz=8000, sample_counter=0 |
| radio-init/wifi/fullscreen-layered | 39214c92ace2 | actual_base_effect=gate, actual_layered=True, history_requested=True, history_ready=True, history_format=LUT8, history_payload_bytes=27840, tunnel_allocated=True, tunnel_payload_bytes=13920, fire_allocated=False, fire_payload_bytes=0, native_fullscreen_at_receipt=False |

Visions receipts are emitted after configuration and before the next frame. For native fullscreen trails the implementation uses bounded geometric paths and releases pixel history/tunnel on its first fullscreen frame; receipt-time allocated bytes must not be presented as measured fullscreen history occupancy. Sampled heap provides the in-window evidence. These paths are not numerically identical rendering kernels.

## Wireless scope

Radio rows measure isolated controller initialization and its effect on local scenes. They perform no peer discovery, association or traffic. Radio-off acknowledgements record requested software state; disable/deinitialize return statuses and actual RF emissions are not independently exported or measured. This is a requested-state baseline, not independently verified RF-off operation. No measured goodput, RTT, jitter/loss under traffic, or Wi-Fi/BLE/ESP-NOW coexistence envelope is established by these rows.

| Initialized mode/local scene | reps | FPS | CPU0/1 % | min free B | min largest B |
| --- | --- | --- | --- | --- | --- |
| radio-init/wifi/menu | 3 | 29.40 | 6.3/86.4 | 46700 | 31732 |
| radio-init/wifi/fullscreen-layered | 3 | 22.70 | 6.3/97.2 | 46700 | 31732 |

## I2S driver event capability

Driver event queue capability reported: `UNKNOWN / not exposed in this image`. No direct I2S event records occur in this capture. TX_Q_OVF and DMA_ERROR counts are unknown; the primary image's write-error/short-write counters and inferred late periods do not substitute for them. A supplementary events image must retain its own source/firmware identity and evidence folder.

## Evidence-based recommendations

* Use scene-specific display budgets: the lowest completed cadence among the captured ordinary scopes is **20.80 FPS** for `visions/landscape/ui/plasma-kaleido` across 3 repetitions. Use the measured tail/maximum durations and actual path when choosing VIBEWARE visual complexity; do not apply the menu rate to layered fullscreen effects.
* The largest observed main audio callback is **763 µs** in `combined/fullscreen/gate-trails/seq` (n=5625, nominal block period 5333.333 µs). Keep callback, lock, overlay, mix and blocking enqueue as separate budgets; verify correlated per-block deadlines and analog continuity before claiming an underrun-free audio envelope.
* Offload break-even requires local compute to exceed remote compute, packing/unpacking, queueing and measured transport for the actual input/output message shapes. A full matched-payload RTT already includes its payload transfer: do not add that transmission a second time. A fixed-overhead/throughput model instead needs independently estimated fixed overhead and directional byte-transfer costs. No numeric minimum useful ratio can be established from controller-init-only radio tests.
* Reserve memory using the observed free/largest-block minima and allocation receipts for the intended combination of effects. Saved-palette gradients, history format, fullscreen path and radio initialization are workload settings, not incidental details. Repeat matching scene transitions and long sessions before diagnosing a leak or guaranteeing a memory margin.

## Missing evidence and reproduction

| Workload class | status | reason |
| --- | --- | --- |
| physical/contact-to-photon | NOT RUN | Requires owner-operated physical touch stimulus and external optical timing fixture. |
| audio/usb-only-dac-disabled | NOT RUN | Existing PCM streams the same mixed blocks sent to the local DAC; independent DAC-disable path is not assumed. |
| audio/external-dac-loopback | NOT RUN | External acquisition fixture required to detect analog gaps/distortion independently. |
| wireless/wifi-goodput-rtt | NOT RUN | Requires explicitly authorized owned AP/peer and configured traffic endpoint; no network probes are automatic. |
| wireless/ble-goodput-rtt | NOT RUN | Requires explicitly authorized BLE peer; no scans/probes of unrelated devices. |
| wireless/espnow-goodput-rtt | NOT RUN | Requires explicitly authorized ESP-NOW peer and owned channel. |
| wireless/coexistence | NOT RUN | Requires explicitly authorized Wi-Fi/BLE/ESP-NOW peers and separately captured local-workload baseline. |
| portable/esp32-s3-physical | NOT RUN | Physical S3 board identity/backup/flash authorization required; compile-only is distinct evidence. |
| portable/esp32-c3-physical | NOT RUN | Physical C3 board identity/backup/flash authorization required; compile-only is distinct evidence. |

Current unfinished planned repetitions: 91. Missing observations stay absent rather than receiving theoretical estimates.

Reproduce with the captured plan, identified board, exact firmware digest and matching source/toolchain receipts. The harness never flashes. Owner authorization, private backup verification and the flash receipt are separate evidence. The on-board identity does not independently attest the firmware binary digest; preserve the flash/build chain.

```powershell
python tools/perf/capture.py analyze evidence/physical/full-01/results.json
python tools/perf/summarize.py evidence/physical/full-01/results.json
python tools/perf/report.py evidence/physical/full-01/results.json --label full-01
```

Artifacts: [results.json](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/full-01/results.json`), [raw target JSONL](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/full-01/target.jsonl`), [raw serial bytes](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/full-01/serial-rx.bin`), [timestamped serial events](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/full-01/serial-events.jsonl`), [metrics.csv](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/full-01/metrics.csv`), [samples.csv](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/full-01/samples.csv`), [summary.json](summary.json), [summary scenes](summary-scenes.csv), [summary metrics](summary-metrics.csv), [summary kernels](summary-kernels.csv), [artifact checksums](../PHYSICAL-CAPTURES-BASELINE.zip) (archive member `physical/full-01/artifact-hashes.json`).

## Plots

![audio-tails](figures/audio-tails.png)

![fps-audio](figures/fps-audio.png)

![fps-radio](figures/fps-radio.png)

![fps-scenes-1](figures/fps-scenes-1.png)

![fps-scenes-2](figures/fps-scenes-2.png)

![fps-scenes-3](figures/fps-scenes-3.png)

## Publication bundle

This compact folder derives from the **terminal partial** `full-01` capture and preserves status **FAILED**. Only independently completed MEASURED windows are summarized; this is not a completed campaign. The [raw capture archive](../PHYSICAL-CAPTURES-BASELINE.zip) has SHA256 `41f122a120c5f4207f6d30b01d3c0b4be67728a200ba89154fa3d957d38a7797`; its `physical/full-01/` members preserve the full JSON, samples, target telemetry, serial RX, PCM and event logs. Extract the archive into `evidence/` while retaining its `physical/` prefix before replaying the commands above. [stress-samples.csv](stress-samples.csv) contains 0 stress-only rows with source/firmware/profile identity, target timestamps, raw ticks, CPU percentages, sensor readings, heap and stack. An empty table means that no stress workload was present in this source capture. [manifest.json](manifest.json) records input/output hashes and archive-member verification.
