# Supplementary I2S-event interpretation — completed capture

This analysis covers all **31 completed windows** `r001_1` through `r031_3` in
`physical/events-link-01/results.json`. The capture is `CAPTURE COMPLETE`, with
31/31 runs `MEASURED`, ending at 2026-11-27 20:08:39 UTC. It includes the
365-second extended stress test and three repetitions each of histogram
insertion disabled/enabled. The analysis reads saved evidence only and does not
access either serial port. Its draft filename is retained for the HOLD review.

The target reports clean source `174a79670d4b0da656ebd615f0cd1620aa0ddbb1`, profile
`cyd-profile-link`, source digest
`1b7d5aacd09d257dff759f95413047b74fe2b6de7592b7a3f147e39aea0c07ed`, and the host
identifies the approved binary SHA256 as
`a980f7e2b9409686ae2d325ebeddfd525966f06e8ded03b2d8a723b36771ab11`.
The final input JSON SHA256 is
`3f342828ff90ef36feb68f815a8e600713385f5ab46d0c9235de9ad36bde54d4`.
Target source identity is verified;
the UART target does not independently attest its flashed binary digest. Refer
to the separate physical flash receipt for image verification.

Thirty windows are exactly 10,000,000 microseconds long with two seconds of
excluded warmup; the stress window is exactly 365,000,000 microseconds with zero
warmup. There are no retained-sample drops. The I2S event queue is enabled and
available at capacity 32 in every run. Each short window records 1,875 drain
calls, TX_DONE events and completed audio blocks; stress records 68,437 of each.
The totals are **124,687 drain calls, 124,687 TX_DONE events and 124,687 completed
audio blocks across 665 measured seconds**. This agrees with the configured
128-frame blocks at 24,000 Hz (187.5 blocks/s), allowing the half-block endpoint
of the stress window. Event categories sum exactly to each exported event total.

Across these windows there are **zero observed TX_Q_OVF underflow indications,
zero DMA descriptor faults, zero other events, zero queue-full observations and
zero bounded-drain-limit hits**. Maximum observed queue occupancy is one event.
The longest recorded interval between drain completions is 5,396 microseconds;
the largest queue-read drain duration is 16 microseconds, excluding the later
counter fold. The observed queue has ample headroom in these particular
windows. These are consistency and saturation checks on the retained evidence,
not proof that an uninstrumented ISR event could never be lost.

The 28 histogram-enabled windows also report zero `audio_write_errors`,
`audio_short_writes`, `audio_late_periods_inferred` and `pcm_dropped_blocks`.
These histograms are disabled in the other three windows, so their zero entries
provide no error/timing evidence; independent I2S event collection and completed
block counts remain active. The period flag records
an audio-loop-start interval **greater than 6,000 microseconds**; it is a
scheduling observation, distinct from the driver's underflow indication. The
observed audio-loop intervals span 5,271–5,395 microseconds, around the nominal
5,333.333 microseconds. Zero late-period flags and zero driver underflow
indications are consistent here; they remain separate measurements.

| Workload, three repetitions each | Completed FPS range | Audio-loop interval range (µs) | Callback maximum (µs) | Raw sensor range |
|---|---:|---:|---:|---:|
| Menu, landscape/theme | 29.4–29.5 | 5,282–5,378 | 2 | 135–136 |
| Drone, local DAC | 29.4 | 5,299–5,364 | 536 | 135–137 |
| Sequencer, local DAC | 29.4 | 5,305–5,361 | 790 | 135–137 |
| Bytebeat, local DAC | 24.8 | 5,309–5,359 | 325 | 136–137 |
| Drone, local DAC plus USB PCM | 30.3–30.5 | 5,271–5,395 | 552 | 136–138 |
| Fullscreen layered Gate/Trails plus USB PCM | 20.5 | 5,293–5,369 | 186 | 137–138 |
| Fullscreen layered Gate/Trails plus drone callback | 22.7 | 5,289–5,374 | 560 | 137–138 |
| Scripted fullscreen Visions artwork taps | 22.5 | 5,304–5,369 | 172 | 137–138 |

The six USB PCM captures contain 13,782 valid protocol frames in their broader
warmup/steady/guard/stop-tail receive windows, with zero decoded CRC errors,
sequence gaps, duplicates, backward discontinuities or discarded/trailing
bytes. Their count is not expected to equal the steady-window I2S count. Missing
data before the first received sequence or after the last sequence is unknown;
UART arrival timing is not DAC timing. The slightly higher drone FPS with PCM
cannot establish a performance improvement from these separate windows.

The table above covers the first 24 windows and their 216 retained sensor
samples, raw **135–138**. Across the entire capture, 626 retained samples span
raw **135–146**. The target labels the method
`RTC_library_raw_undocumented_uncalibrated`; the numeric conversion is described
below with the stress result.

The sustained test `r025_1` completes **365.000 seconds**, with 356 retained
CPU/sensor samples from elapsed 1.008 to 364.613 seconds, a **363.605-second
sample span**. Raw tick totals give weighted busy estimates of **98.9871% on
core 0 and 99.9515% on core 1**. Core 0's first sample is 94.328%; its remaining
355 samples are at least 98.908%. Core 1's samples are 99.618–100%. Both CPU
hooks are supported. This supplies measured near-100% utilization across both
cores for a completed window longer than 360 seconds. Tick sampling remains an
estimate, with the aliasing and interrupt-attribution limits in the method notes.

During stress, raw sensor codes span **139–146**, starting at 139 and ending at
142. Raw 146 occurs at elapsed 287.813 and 290.933 seconds; the indication is not
monotonic. The firmware arithmetic `(raw - 32) / 1.8` yields approximately
**59.4–63.3 °C**, starting at 59.4 and ending at 61.1. These converted values are
**uncalibrated indications**, not established die or ambient temperatures or a
thermal safety margin. Earlier raw 128 observations came from a different boot,
image and radio/workload history, so their difference is not a controlled
temperature-rise experiment. Ambient temperature and thermal throttling were
not measured.

Stress retains 68,437 TX_DONE events and completed blocks, with zero underflow
indications, descriptor faults, queue pressure/limit hits or inferred >6 ms audio
periods. Its audio-loop interval is 5,312–5,355 microseconds; maximum drain gap is
5,355 microseconds, drain duration 16 microseconds and pending occupancy one.
The menu completes 6,326 presentations (**17.33 FPS**) under both-core stress,
versus 29.4–29.5 FPS in its earlier unstressed windows. The audio callback is
the menu's nearly empty callback (maximum 4 microseconds), so this stress result
does not establish the same envelope for the maximum sequencer/DSP workload.
The unsigned 32-bit stress iteration fields can wrap and are not throughput
totals. Retained free heap stays 87,588 bytes, largest block 47,092 bytes, with
audio/loop stack low water 2,256/3,204 bytes during this window; these are
instrumented-image observations, not ordinary-firmware memory headroom.

The post-stress histogram insertion comparison `r026_1`–`r031_3` completes
**227 frames and 1,875 audio blocks in every 10-second window** (22.7 FPS), both
disabled and enabled, with three repetitions each. Independent I2S events also
remain 1,875 TX_DONE per run with zero underflow/fault/pressure indications.
Weighted sampled CPU0/core1 percentages are 6.247/97.563 with insertion disabled
and 6.250/97.332 enabled. These small differences do not demonstrate a causal
speedup or zero instrumentation cost. The comparison changes **histogram
insertion only**: timestamp calls, hooks, counters, samples, event draining and
the profiler's static memory remain. The groups run consecutively after stress
while raw indications decline from 140–141 to 139–140. The evidence establishes
equal completed-frame counts for these windows, not total profiling overhead
relative to an ordinary binary.

The strongest supported conclusion is that **this image retained continuous
nominal-rate software audio completion and TX completion observations without
a driver-reported underflow or descriptor fault during these 31 tested
windows**, including fullscreen layered graphics, PCM streaming and a combined
drone callback. The measurements do not prove analog output continuity. In the
pinned IDF 4.4.7 driver, TX_Q_OVF means the free-TX-buffer queue is already full
of completed/empty buffers, which is a driver underflow indication; DMA_ERROR
is a separate descriptor fault. Event counts are assigned by **drain completion
time**, not interrupt timestamps: first-window drains can contain earlier events,
and events still queued after the fixed end are excluded. Queue fullness between
drains and exact event loss remain unknown. Consequently `event_loss_count` and
`analog_gap_count` correctly remain null. See [method-notes.md](method-notes.md)
for pinned driver sources and the sensor/CPU/event interpretation limits.

This supplementary image also includes the dormant link module and event
collection. It differs from the primary `a4549f8` image; do not merge its totals
into the primary campaign or interpret an absolute FPS difference as the event
instrumentation's isolated cost.

Offline artifacts are beside the input capture: `summary.json`,
`summary-scenes.csv`, `summary-metrics.csv`, `REPORT.md` and five PNG/SVG figure
pairs with provenance in `plots/manifest.json`. Kernel plots are explicitly
omitted because this capture has no kernel timing records. The independent
validation checked all run IDs, fixed durations, 19 histogram retention/counts,
sample retention and I2S event category/completion totals. Archive/publication is
deferred until the wireless captures are complete.
