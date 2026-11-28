# Owner evaluation — Draft/HOLD

This mission has not accessed or flashed physical boards. Every hardware cell
below starts **NOT RUN**. Use the exact candidate's CI artifact and manifest;
historical `dist/` images are not the convergence delivery.

## Identify and install

Record the board marking, silicon model/revision, flash capacity, port, source
SHA, firmware SHA-256, partition table and existing application. Keep a verified
full-flash backup privately before a layout change; it may contain credentials.
The candidate's normal profiles target classic ESP32 CYD and ESP32-S3 Cardputer
ADV. No S3/C3 auxiliary firmware is supplied or implied.

Compare `SHA256SUMS.txt` and `manifest.json` from the artifact. App-only firmware
must fit the actual installed app partition. The old 1600 KiB Cardputer Launcher
slot is not sufficient for the expanded application. A full image starts at
zero and contains a different standalone partition layout; it is not an
in-place Launcher update and does not preserve unspecified partitions.

After a separately authorized installation, verify the `diviner_identity`
startup line against source/profile/tree identity. The line identifies compiled
source; independently preserve the flash verification and image hash receipt.

## Instrument and touch acceptance

| Instrument | Exercise | Record |
| --- | --- | --- |
| CANON | Enter pitches/rests, drag a phrase cell, select scale, adjust tempo/spread, realign and hold | Four independent reels, audible phase divergence/convergence, finger usability |
| GLYPHSONG | Compose/edit text including `day3_ time5`, move caret, all four modes, change pitch/pace during singing | Articulation, transitions, sustained choir, no torn text, no mute/stall |
| SPECTRA | Wi-Fi observation, BLE switch, hold/release, voice inspection, air/scale controls | Actual discoveries, stable pitch identities, fading departures, scan latency and audio interference |
| COVEN | Local solo, member arrivals/departures, calls, leader-only root/mode changes, sanitized DYAD text | Local synthesis remains playable with radio off/unavailable; pulse behavior with peers |
| DRONE | All six styles, pitch/scale, XY brightness/drift, freeze/reseed/wind/bells | Timbre, register, delay, persistent settings across repeated visits |

Repeat in all four display orientations and at least two palettes. Check that
Menu is always reachable, a held contact cannot activate the next screen, and
the GLYPHSONG keyboard preserves symbols/annotations. Evaluate resistive-touch
precision with an actual finger/stylus; mouse simulation does not establish it.

Recheck Palette Save/Discard and reboot persistence, compact carousel,
Rain/Ashfire/Plasma backdrops, all layered Visions, solo RGB444/RGB565 trails,
fullscreen entry/exit, SEQ/BEAT and original radio utilities.

## Audio and performance capture

`cyd-diagnostics` accepts existing `status`, `tap x y`, `touch x y`, `release`
and `shot` serial commands at 115200 baud. `status` now includes an `audio_perf`
line: block size 128, 24,000 Hz, deadline **5,333.333 µs**, logarithmic-bin
p50/p95/p99 **upper bounds**, exact observed maximum, callback-over-deadline
count, lock-wait p99/max and I2S API write errors. Percentiles are bounds, not
exact quantiles; these metrics do not measure acoustic latency or DAC continuity.

Warm up each scene for two seconds, then collect at least three 30-second
windows (new boot or a recorded initial counter snapshot). Measure choir,
continuous singing, CANON at maximal tempo/spread, dense SPECTRA and COVEN traffic,
and bell-heavy DRONE. Record heap/largest block before entry and after exit for
100 transitions. Preserve raw serial output and derive counts from the start/end
counter difference; histogram summaries are cumulative since boot.

Diagnostic observer overhead versus ordinary firmware is unmeasured. Do not
interpret a zero callback-overrun count as proof of zero I2S underruns. Use an
external audio recording/loopback fixture for continuity, clipping and latency.
Full RGB565 transfer (~32.28 ms) and prior wireless/UI interactions in PR #7 are
historical physical baselines, not measurements of these new instrument images.

Cardputer uses 256-sample blocks, **10,666.667 µs** at 24 kHz. Its audio is queued
to M5's codec worker; evaluate both callback cost and downstream audible delivery.
Native WAV paths use 256-sample blocks; portable instrument benchmarks use 128.

## USB PCM and VLC

Use `cyd-pcm` and the existing Windows VLC plugin at 533333 baud. Validate CYD1
CRC, sequence, sample count, stop/restart and host-loss recovery while playing
each new instrument. Diagnostics/debug text is suppressed during a live binary
PCM session. Existing protocol source and plugin behavior remain unchanged;
this candidate still requires a physical end-to-end retest.

## Distributed hardware matrix

| Pair/population | Initial classification | Procedure |
| --- | --- | --- |
| CYD + CYD | NOT RUN | Same candidate, channel 1; arrivals, calls, lowest-MAC election, leader removal |
| CYD + Cardputer ADV | NOT RUN | Both qualified build profiles; confirm IDF4/5 reception and rhythm |
| CYD + upstream Cardputer | NOT RUN | CV1 wire compatibility plus explicit old/new election behavior |
| CYD + ESP32-S3 | NOT RUN | Requires separately developed/qualified compatible node firmware |
| CYD + ESP32-C3 | NOT RUN | Requires separately developed/qualified compatible node firmware |
| 3–11 compatible nodes | NOT RUN | Bounded 10 remote peers per participant; partition/reunion and heavy unrelated traffic |

Capture pulses simultaneously on one external clock to measure musical phase
error. ESP-NOW receive time/RSSI is not sample synchronization or physical
distance. CV1 is unauthenticated, lacks a boot nonce, and cannot guarantee
adversarial replay protection; isolate evaluation to owned participants.
The same-channel broadcast ESP-NOW musical coordination does not distribute a single
sample-synchronous DSP graph. A leader loss uses the documented 15-second lease.

Retain each cell's source, image, topology, duration, PASS/FAIL result and raw
evidence. Do not convert unexecuted hardware cells to PASS based on simulations.
