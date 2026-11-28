# Final available-hardware coverage audit — Draft / HOLD

Reviewed **28 November 2026** against the consumer
[performance mission](https://github.com/techrote/cyd-SYNTH-VIBEWARE/blob/main/docs/PERFORMANCE.md),
Git blob `5696dc50404e62a3a0c1d75fbd9d56dacb3c039b`, the current
[measurement guide](../docs/PERFORMANCE.md), [evidence index](README.md),
[recommendations](RECOMMENDATIONS-DRAFT.md), [temperature ledger](TEMPERATURE.md)
and [mission metadata](mission-metadata.json). Accepted Diviner baseline is
`7b1b37eec6ac126f334a0b008d91135071ea60fa`; the latest executed isolated SDR
source is `e8b4c8a9f5073db86706f9cebbaefb0476549ef6`.

**The available-hardware campaigns are closed. The mission's full set of physical
bounds is not measured.** This is an advisory Draft/HOLD handoff with explicit
gaps, not a claim that compilation, finite captures or host reconstruction satisfy
every requested measurement. Earlier [mission](MISSION-COVERAGE-AUDIT.md) and
[expanded](EXPANDED-COVERAGE-AUDIT.md) audits retain their historical snapshots;
their then-pending colour/PCM/SDR statements are not the current coverage state.

## Original mission requirements

| Requirement | Delivered evidence | Remaining limitation |
| --- | --- | --- |
| 1. Inventory, ordinary builds and observer overhead | Classic CYD and three S3 devices identified; clocks, flash/PSRAM, task/DMA/heap context, display/touch/SPI and exact image receipts retained. Four private full-flash backups were verified before separately authorized installs. Normal, diagnostic, PCM, native and opt-in builds have size/map/source records. Histogram insertion A/B measurements exist. | **Total observer overhead remains unmeasured.** `perf enable 0` retains timestamps, tick hooks, counters and other observers. Capped frame rates can hide cost. No ordinary/minimally counted versus fully profiled physical control establishes total CPU/memory/timing overhead. No physical C3 board was supplied. |
| 2. Bounded diagnostics and correct metric classes | Timestamped run IDs, warmups, true completed-frame/block counts, bounded histograms, CPU non-idle tick estimates, heap/minimum/largest block, stack high-water, software touch timings, callback/lock/mix/I2S enqueue, USB PCM CRC/sequence/drop evidence and separate I2S-event observations. | Histograms yield conservative percentile bounds; exact step arrays use nearest-rank samples. Tick utilization is not cycle accounting. Heap minima/stack high-water retain lifetime context. Touch processing is not contact-to-photon. Driver events/inferred late periods do not prove analog continuity or count every physical underrun. |
| 3. Representative real workloads and coexistence | **280 completed CYD baseline/continuation/fresh-kernel windows**, menu/backdrops, portrait/landscape, Visions effects and current layered fullscreen paths, decorated RGB444/RGB565 trail histories, drone/sequencer/beat, combined graphics/audio, scripted transitions, USB PCM coexistence, physical display readback outside timed windows, radio-off/on and owned-peer supplements. Three repetitions were retained where practical; failed plans remain failed. | This is a declared finite matrix, not every effect/parameter/state history. Fullscreen geometric Trails differ from decorated pixel history. Display readback is not scanout latency. Local DAC scheduling/API evidence has no independent analog detector. Two phone-AP TCP cells have two rather than three valid repetitions; BLE peer startup failed. |
| 4. Reusable comparative kernels | Eleven scalar kernels cover float/fixed oscillator, filter/modal, FFT, cellular, particles, indexed feedback and RGB565 generation. Dimensions, precision, seeds, warmup, output checks and exact timings are retained. Fresh CYD matrix: **66 repetitions / 16,896 timed steps**. S3 internal+PSRAM matrix: **132 repetitions / 33,792 timed steps**. ESP32/S3/C3 builds and host numeric oracles are separate evidence. | C3 is **compile-only**. CYD/S3 flash clocks and background workers differ, so ratios describe configured systems rather than isolated ISA/FPU advantage. Warmed PSRAM working sets do not establish general cold/streaming memory bandwidth. Step timing is not full application throughput. |
| 5. Reproducible captures, distributions and recommendations | Versioned JSON/CSV, exact source/content/image identities, board settings, units, durations, warmups, seeds, raw serial/PCM/display evidence, failed attempts, plots and archive/manifests are retained. Local graphics/audio/SPI/memory budgets and measured wireless/S3 terms support explicit offload screening. | Unsupported percentiles remain null/bounded rather than extrapolated. Echo RTT/goodput is not a matched computational RPC or proof of an offload win. Actual state/input bytes, packing, peer service and complete result latency remain missing. Summed independent p99 values are not a measured end-to-end p99. |
| 6. Focused delivery and consumer handoff | [Diviner PR #7](https://github.com/techrote/cyd-diviner/pull/7) remains Draft/HOLD. Portable Windows tools/package, instructions, image/source receipts and separate [WiDAC PR #1](https://github.com/techrote/WiDAC-ESP32S3-SDR/pull/1) record the standalone SDR work. No merge or unrelated application optimization is part of the mission. | Final artifact/index verification and the artifact/source/reproduction handoff to [consumer #8](https://github.com/techrote/cyd-SYNTH-VIBEWARE/issues/8) are separately recorded delivery steps. A test-approved image or historical `UNAPPROVED` artifact filename is not production promotion. |

## Expanded requests and SDR research arms

| Requested extension | Current evidence | Limit that remains explicit |
| --- | --- | --- |
| Fullscreen RGB111, RGB444, RGB555+one-bit alpha and maximum current layers | [Colour matrix](COLOR-FINDINGS.md): **36/36 windows**, twelve settings, three repetitions, actual packed eight-row intermediates and current one/two-effect paths. Native Gate one/two-effect controls were 25.00/22.33 FPS; added RGB111 conversion was 7.89/3.67 FPS, RGB444/RGBA5551 7.00/3.00 FPS. | RGB565 source raster, 16-bit SPI and 64,800-byte backing reservation remain. Coverage derives after existing blending; this is not native alpha raster/history. Two effects are the current implementation maximum, not a demonstrated arbitrary-layer compositor capacity or native packed-renderer limit. |
| Tiny digital audio blocks, bounded age, no retransmission and concealment | [Packet PCM](PCM-FINDINGS.md): synthetic broadcast PCM16, bounded local admission/queues/assembly, no application retransmission/unicast ACK requirement and PLC. **50 validated three-second windows among 195 attempts**; matched S3 live/deferred cohorts validate 22/48 and 25/48 and both remain FAILED. Best observed cases still conceal roughly 11–13% of rendered blocks. | A local admission deadline does not bound an already queued driver's over-air age. Admission rejects, telemetry/resource failures and unexplained deficits are not all RF loss. Optional packetized PDM is a comparison, not continuous SDR. No live ADC, receiver DAC or synchronized analog latency. |
| Bare SDR first, isolated from Diviner | Separate pinned SDK TX/RX apps remove Diviner UI/DSP/ordinary workers from timed loops. Original carrier/framed proof and `3b4161` native timing/rate/position cohorts are preserved. The new `e8b4c8a` phase has **96 closed windows per receiver**: **78 shared finite TX exposures** (3 smoke + 42 rate + 21 framed + 12 compatibility) and **18 receiver-only reference windows**. All have strict terminal acquisition receipts, yielding **192 valid receiver bodies**, not 192 independent transmissions. | SDK PHY startup/calibration, finite windows, setup/teardown/check costs, nominal clocks and deferred USB remain explicit. Source CRC/checked output/fast-clock proof establish their stated software/buffer boundaries, not physical RF edge timing, calibrated power or continuous bandwidth. Detailed signal/payload conclusions belong to [the separately frozen fast findings](METAL-FAST-FINDINGS.md). |
| DSP request/result roundtrip and heterogeneous offload | Local/S3 reusable kernels, actual message shapes, useful-payload goodput and application echo RTT support a break-even worksheet. | **No real matched DSP job RPC or offload speedup**; raw-SDR RPC latency is unmeasured. A deterministic seed does not replace arbitrary live state transfer. |
| Continuous one-way audio/control | Packet PCM and finite custom-frame/digital reconstruction prototypes exist; direct and through-AP traffic were physically compared. | No continuous on-device greenfield demodulator/clocked playout, measured source-to-sink analog latency or universal radio latency floor. |
| Remote video, lower priority | Primary-source research and architectural limits are recorded. | **No video transport prototype or physical video result**. OOK envelope/store rate cannot establish FPV video capability. |
| FPV-inspired, deliberately imperfect multichannel one-bit audio | Independent greenfield NRZ framing, CRC8 header, unchecked payload, per-channel first-order sigma-delta TDM and boxcar reconstruction; actual custom RF payloads and host PCM reconstruction exist. New paced-source integrity and native demodulation tools have separate identities and declared selection policies. | Finite precomputed replay and conditional received-frame error/quality do not prove live continuous multichannel audio. Cropped native apertures and missing headers are not a measured packet-loss denominator. Payload errors are accepted deliberately; an acquired header is not clean audio or useful sustained throughput. |

## Sustained stress and temperature

Both classic-CYD runs exceed the requested **360 seconds** and retain their
individual source/boot/radio contexts:

| Stress run | Target window | Retained 1 Hz samples / span | Weighted busy core 0 / core 1 | Raw RTC indication |
| --- | ---: | ---: | ---: | --- |
| Original continuation | 365 s | 355 / 362.939 s | 98.9947% / 99.9602% | 128 throughout |
| I2S-event supplement | 365 s | 356 / 363.605 s | 98.9871% / 99.9515% | 139–146 |

These are near-full **sampled non-idle CPU occupancy**, not cycle-exact 100%
arithmetic utilization. The undocumented raw sensor and legacy conversion do
not establish constant physical temperature, a valid failure sentinel,
calibrated thermal rise or thermal margin. There is no equivalent 360-second
S3 SDR stress measurement.

Temperature scopes stay separate: 72 colour-window raw indications, 265 S3
kernel state indications, 100 post-stop PCM endpoint readings, and **576
outside-window S3 readings** from the five closed fast-SDR captures. The latter
retain a maximum TX driver indication of **98.9 °C**, above the transmitter's
requested **10–80 °C** sensor range. Successful sensor lifecycle status is not
independent calibration or accuracy beyond that range. These separated bursts,
readouts and idle intervals are not another continuous stress test; neither
cross-board differences nor decode outcomes establish thermal causality.

## Current hardware state, publication and consistency checks

The [final application receipt](metal-final-audio-restore-receipt.json) records
COM22/COM23 restored to the approved `ad9cbfd` kernel/audio image and verified
idle. COM13, the owner's original COM14 TX board, retains the requested `e8b4c8a`
TX image with radio idle. The CYD was not changed by the final S3 phase; its
colour/audio profiler is last verified installed, but the board is currently
disconnected. This is not a fresh runtime attestation of the disconnected CYD.

[The source CI receipt](ci-e8b4c8a.json) reports **all 19 jobs passed**, including
ordinary/diagnostic/native/plugin and opt-in builds, portable kernels and host
checks. Its evidence class is build/host validation, not owner-board execution.
It cannot fill a C3, RF, audio or calibrated-temperature measurement gap.

The current derived index has **49 terminal captures and 1,157 heterogeneous
run rows**: 690 MEASURED, 255 ACQUIRED, 207 FAILED, three IN PROGRESS and two
NOT RUN. The three IN PROGRESS rows are unfinished operations retained inside
closed historical failed captures, not active campaigns or measured results.
Dual TX rows retain both endpoint identities and receiver statuses; USB-only
exact-CRC re-export is not another RF exposure. Do not pool these schema-specific
statuses into a single performance-success percentage.

The current guide, recommendations, metadata and temperature ledger agree on
the principal remaining gaps. Old audits and host-check records are scoped
snapshots, not contradictions to later execution. The older generic paced-loop
limit, later fast-loop results, original and new image hashes, and physical
position swap must remain separate cohorts. No board-brand ranking follows.
The frozen fast findings distinguish **56 fast-clock receipts** from the 78 TX
windows, and disclose the **50-word unsent target tail in one 8.832 Mbaud framed
repetition** rather than claiming exact achieved rate in every repetition.
All referenced top reports now exist. Final archive/package/index and Git-blob
verification, and the consumer artifact handoff, are separately recorded delivery
steps; this coverage review does not replace those integrity checks.

The unresolved physical bounds are total observer overhead; physical C3;
contact-to-photon touch; analog DAC continuity/fidelity/latency; calibrated
die/ambient temperature; independently calibrated RF power/spectrum/cessation
and clocks; matched-job offload/RPC; continuous on-device greenfield audio; and
video. The final handoff must retain these unknowns and failed cases. **Keep both
PRs Draft/HOLD; this audit grants no merge or production promotion.**
