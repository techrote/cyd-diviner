# Finite PCM broadcast findings — DRAFT / HOLD

The synthetic PCM16 prototype executed finite digital reconstruction over
ESP-NOW broadcast, with no application retransmission and bounded local
buffering/PLC. The closed S3-pair cohorts validate **22/48 live-metadata attempts**
and **25/48 deferred-metadata attempts**; both overall captures remain **FAILED**.
At 24 kHz stereo, 32 frames per block and requested 6 Mbps PHY, nonconcealed
source coverage is **87.87–88.84%** in the live cohort and **86.62–87.56%** in the
deferred cohort. The latter has no valid reverse-direction repetition at that
setting. These are conditional digital packet-stream results, not high-fidelity
or analog low-latency audio proof.

This report covers all eight closed PCM attempt directories: 50 validated
three-second windows among 195 attempted windows, including three validated
classic-CYD receiver windows. It preserves failed and zero-run captures;
no unfinished capture enters the analysis. The two S3 metadata policies remain
separate contexts throughout.
The archives also retain a separate setup-only `pcm-preflight-01` diagnostic;
it contributes no timed workload window to those totals.

## Scope and provenance

All reported physical PCM cases use source
`ad9cbfd670e456de5e08fca19810fb93f7f1db47`, source-content digest
`49fcfc731717626ed1b102f9d4d4ef5f135b4dbb91f1e323f6853eaa2c357ea6`.
The classic CYD image is `cyd-profile-audio`, SHA-256
`407eb96f8fe2b74ed1cf45f4a163558eacde0ff287a4b878a9ed1faadfaccc93`.
The headless S3 image is `kernel-s3-n4r2-audio`, SHA-256
`db7a8db3115f061ee01b82850d679e8487467a05f0f220bab2d2957b87eb22b8`.
These are the separately authorized Arduino/IDF 4.4.7 packet firmware, not the
new IDF 5.5.5 SDR-metal images.

The [live-metadata S3 analysis](audio-s3-pair-02-analysis/AUDIO-REPORT.md),
[JSON](audio-s3-pair-02-analysis/audio-summary.json),
[CSV](audio-s3-pair-02-analysis/audio-summary.csv) and
[analysis manifest](audio-s3-pair-02-analysis/audio-analysis-manifest.json)
retain every attempt and exact endpoint identity. In that capture, logical
`cyd` is **the second ESP32-S3 on COM23**, not the classic CYD; `peer` is the
ESP32-S3 on COM22. Both use the same S3 image, 240 MHz CPU, 80 MHz configured
flash and initialized PSRAM, without ordinary Diviner display/audio workers.
Hardware/image matching does not remove boot history, RF conditions or
software scheduling differences. The input results SHA-256 is
`a42e9881c8b3197170309d5095f4a9dce48969b7ace2494313d3acabbe5f127d`.
The [deferred S3 analysis](audio-s3-pair-deferred-analysis/AUDIO-REPORT.md),
[JSON/CSV manifest](audio-s3-pair-deferred-analysis/audio-analysis-manifest.json)
and [numeric CSV](audio-s3-pair-deferred-analysis/audio-summary.csv) retain the
same physical endpoints/image identities under the changed host policy. Its
input results SHA-256 is
`2f9b69490f7e008b50c52a464d94c10bd9159290ee0a2cdd1eca847c4be29f35`.

Raw streams and unsuccessful attempts are retained in the
[CYD PCM archive](PHYSICAL-CAPTURES-PCM-CYD.zip)
([receipt](pcm-cyd-archive-receipt.json), 573,779 bytes, SHA-256
`aa1ed05719f9fb466cf93c21a46d18d1fe02007fb18fdeca11acd90173f588f9`)
and [S3 PCM archive](PHYSICAL-CAPTURES-PCM-S3.zip)
([receipt](pcm-s3-archive-receipt.json), 332,632 bytes, SHA-256
`8b51ab1ac3944f62efb64dec120d08dcc5243af1af8751c6fef02507d0d12d32`).
Both complete archive hashes matched during this review. Setup-only, failed
and zero-run directories remain distinct from successful measured windows.

| Closed cohort | Retained status | Validated runs / attempts | Interpretation |
| --- | --- | ---: | --- |
| Original CYD/S3 bidirectional matrix | FAILED | 0 / 48 | 47 bounded-telemetry failures and one readiness failure; no performance result follows from an absent start receipt. |
| Fresh CYD/S3 single case | FAILED | 0 / 1 | Fresh boot did not by itself produce the required live telemetry proof. |
| CYD/S3 deferred single case | FAILED | 0 / 1 | Missing required proof remains failed despite changing host metadata timing. |
| [Fresh CYD deferred case](audio-deferred-fresh-analysis/AUDIO-REPORT.md) | CAPTURE COMPLETE | 1 / 1 | Completed digital counters under the explicitly narrower deferred-metadata policy. |
| [CYD/S3 deferred matrix](audio-bidirectional-deferred-analysis/AUDIO-REPORT.md) | FAILED | 2 / 48 | Two valid repetitions, 45 telemetry failures and one timeout; not a completed bidirectional matrix. |
| First S3-pair attempt | FAILED | 0 / 0 | Startup acknowledgement/summary timeout; no workload result. |
| [S3-pair live-metadata matrix](audio-s3-pair-02-analysis/AUDIO-REPORT.md) | FAILED | 22 / 48 | Fourteen of sixteen workload types have at least one valid run. Sixteen timeouts and ten power-save readiness mismatches remain failed. |
| [S3-pair deferred-metadata matrix](audio-s3-pair-deferred-analysis/AUDIO-REPORT.md) | FAILED | 25 / 48 | Thirteen workload types have a valid run. Nine missing matching start ACKs and fourteen timeouts remain failed. Active power-save/channel readback was not taken. |

Only MEASURED runs that pass the current validator enter the numeric tables.
Failed windows are not zero-throughput samples. Mixed hardware, boot histories
and metadata policies are not pooled as repeated identical trials.

## Resource and observability failures

The first CYD raw stream contains two bounded-telemetry errors immediately
after the internal link stop/mode ACKs. Its post-cleanup summary retains the
new RX run ID and zero driver error, showing that initialization and RX arming
preceded reporting failure. The actual success ACK needs about 93 bytes and
the initial RX run receipt 221 bytes, within their 384/512-byte buffers. Normal
bounded record length therefore does not explain those failures.

Allocation pressure is the stronger inference, with an important measurement
gap: the approved firmware's Arduino heap counters query `MALLOC_CAP_INTERNAL`,
whereas telemetry requires `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`. The observed
approximately 14,048-byte internal minimum and 13,300-byte largest block do not
prove that byte-accessible space was available. They may include 32-bit-only
internal memory. Exact byte-capable exhaustion versus fragmentation was not
captured. The [diagnostic note](../experiments/sdr-metal/PCM-HEAP-DIAGNOSTICS.md)
describes the missing capability-matched counters; no approved firmware was
changed to obtain them.

Deferred metadata requires matching successful start ACKs; it does not excuse
an absent ACK or arbitrary error. It permits only the documented subsequent
bounded-formatter failure, stops both radios before export, and validates the
finite counters/configuration afterward. **Active channel/power-save readback
is NOT MEASURED** in that policy. Checked source/API setup and later PHY status
are distinct from observing the active radio. Post-stop info describes released
radios; post-stop last-error fields describe those later commands.

All ten S3-pair readiness failures show an active initialized receiver, channel 6,
successful channel/PHY queries, and **power-save enum 1 rather than required 0**.
They are actual readback mismatches, not inferred receiver timeouts. Their cause
is not isolated by this capture. Valid live-metadata runs observed enum 0 at the
readiness query; that single readback does not prove the setting stayed unchanged
throughout the streaming window. The later timeout cases retain incomplete
observability rather than invented performance values.

## Matched S3 digital delivery

Each valid source window is three seconds. The following ranges retain each
direction, requested PHY setting and metadata policy separately; `n` is the
number of valid repetitions, out of three planned. A zero `n` is unavailable
performance, not measured zero throughput. Ranges avoid implying statistical
precision from one or two trials. Nonconcealed coverage uses the complete
scheduled source count, including source blocks discarded before transmission.
All sixteen workload types have a valid observation across the two contexts;
neither context completes the planned matrix, and their repetitions are not pooled.

| Direction | Sample rate | Frames/block | PHY request | Live n / nonconcealed % | Deferred n / nonconcealed % |
| --- | ---: | ---: | ---: | ---: | ---: |
| COM22 → COM23 | 24 kHz | 8 | 1 Mbps | 1 / 22.32 | 3 / 16.22–19.82 |
| COM22 → COM23 | 24 kHz | 16 | 1 Mbps | 0 / — | 2 / 31.98–32.36 |
| COM22 → COM23 | 24 kHz | 32 | 1 Mbps | 2 / 50.40–50.76 | 3 / 48.62–50.58 |
| COM22 → COM23 | 48 kHz | 16 | 1 Mbps | 1 / 16.87 | 1 / 16.13 |
| COM22 → COM23 | 24 kHz | 8 | 6 Mbps | 2 / 54.81–58.43 | 2 / 51.54–51.78 |
| COM22 → COM23 | 24 kHz | 16 | 6 Mbps | 1 / 0.69 | 1 / 81.51 |
| COM22 → COM23 | 24 kHz | 32 | 6 Mbps | 2 / 87.87–88.84 | 2 / 86.62–87.56 |
| COM22 → COM23 | 48 kHz | 16 | 6 Mbps | 2 / 46.62–49.04 | 0 / — |
| COM23 → COM22 | 24 kHz | 8 | 1 Mbps | 0 / — | 2 / 20.18–20.41 |
| COM23 → COM22 | 24 kHz | 16 | 1 Mbps | 3 / 32.38–33.51 | 1 / 34.18 |
| COM23 → COM22 | 24 kHz | 32 | 1 Mbps | 1 / 49.64 | 0 / — |
| COM23 → COM22 | 48 kHz | 16 | 1 Mbps | 1 / 16.70 | 2 / 14.20–16.51 |
| COM23 → COM22 | 24 kHz | 8 | 6 Mbps | 2 / 52.59–54.16 | 1 / 55.88 |
| COM23 → COM22 | 24 kHz | 16 | 6 Mbps | 1 / 82.96 | 3 / 80.31–82.53 |
| COM23 → COM22 | 24 kHz | 32 | 6 Mbps | 1 / 88.00 | 0 / — |
| COM23 → COM22 | 48 kHz | 16 | 6 Mbps | 2 / 46.43–47.78 | 2 / 46.58–47.40 |

All 22 valid live-metadata runs report zero application CRC errors, callback-queue drops and
expired callback packets. Those zero observations apply only to packets handed
to this software path; they do not establish zero RF bit errors or zero loss.
There are 2,333 stale-packet rejections and 3,891 future-window rejections across
these valid runs. In the conspicuously poor `a018_3` case, the sender accepted
4,016 packets while the receiver admitted 31; **3,891 future-window rejections**
already explain most of that admission deficit. The remaining deficit is not
assigned to RF. Source deadline discards, the receive-window policy, driver
delivery, on-air loss and finite observation boundaries remain distinct causes.

The longest observed send-completion callback delay is 30,867 µs; maximum
receiver callback queue age is 121 µs and maximum render lateness 235 µs across
valid windows. These are separate local observations, not additive latency
components from the same block. Broadcast callback success is driver completion,
not peer acknowledgement. The one-in-flight policy and deadline drops prevent
an indefinitely growing retransmission queue, but do not guarantee delivery.

The 25 valid deferred-metadata runs separately report zero application CRC,
callback-queue and expired-callback errors, with 3,298 stale and 292 future-window
rejections. Their maximum observed send callback delay is 18,234 µs, callback
queue age 144 µs and render lateness 228 µs. Deferring metadata therefore did
not establish a reliable stream or complete campaign; nine missing successful
matching ACKs and fourteen timeouts still fail validation. These counts are
not added to the live cohort to estimate one shared loss rate.

A declared-format sanity check illustrates one limit: 24 kHz stereo, eight-frame
PCM16 blocks require 3,000 packets/s. Their 32-byte PCM plus 20-byte application
header alone total **1.248 Mbps**, before MAC/PHY overhead. A requested 1 Mbps
PHY cannot carry that entire ideal stream. This arithmetic is not a prediction
of the measured loss fraction or proof that larger/6 Mbps settings are reliable.

## Reconstruction, latency and sensor limits

For the best 24 kHz / 32-frame / 6 Mbps settings, sequence-aligned digital SNR
over rendered samples, including PLC and recovery slew, is **8.56–9.28 dB**.
Across all 22 valid windows it ranges from **−0.0075 to 9.28 dB**. The deterministic
PCM reference is unshifted; no fitting removes missing/PLC samples. Nonconcealed
blocks can still contain recovery slew changes. Correlation is unavailable
because the target exports energy/error sums without cross-moments. Source
blocks outside the receiver anchor/obsolete-work window remain coverage loss,
not unobserved samples with an assumed zero error.
The separate deferred cohort has **8.11–8.82 dB** at that setting and
**0.87–8.82 dB** across its 25 valid windows. Its best setting retains
**12.44–13.38% PLC**, compared with **11.16–12.13%** in the live cohort.

The three validated classic-CYD receiver windows under deferred metadata,
all 24 kHz stereo / eight frames / requested 1 Mbps, retain **19.51–22.50%**
nonconcealed source coverage and **77.50–80.49%** PLC. Their per-channel SNR is
**1.13–2.04 dB**. They are separate observations from the S3 pair and do not
establish a causal chip-performance difference.

The configured two-block receiver reserve is calculated as 0.667 ms for
24 kHz/eight frames or 48 kHz/16 frames, 1.333 ms for 24 kHz/16 frames and
2.667 ms for 24 kHz/32 frames. It starts from receiver-local worker admission.
These are scheduling offsets, not measured one-way audio latency. The synthetic
source has no microphone/ADC fill path, receiver output is digital reconstruction
only, and no DAC/I2S/analog loopback or synchronized board clocks are present.
Thus neither a few-millisecond analog-audio claim nor a hardware RF latency
floor follows from these data.

The live S3 capture retains 22 post-stop temperature readings per endpoint:
COM23 driver indications span **43.0–61.0 °C**, COM22 **54.1–68.1 °C**. They are
outside streaming windows and are not independently calibrated; boot/radio
history and settings differ over the series, so these ranges are not a controlled
thermal comparison. The three successful CYD windows retain raw indication 128,
with the existing undocumented/uncalibrated caveat. No sensor-code conversion
is promoted to calibrated die or ambient temperature.
The deferred S3 cohort separately retains 25 post-stop readings per endpoint:
COM23 **47.0–55.0 °C**, COM22 **52.1–68.1 °C**. The
[temperature record CSV](pcm-temperature/PCM-TEMPERATURE-SAMPLES.csv),
[ranges/counts](pcm-temperature/PCM-TEMPERATURE-SUMMARY.csv),
[manifest](pcm-temperature/PCM-TEMPERATURE-MANIFEST.json) and
[temperature interpretation](TEMPERATURE.md) preserve exact sensor methods,
endpoint identities and outside-window scope. None is a sustained S3 CPU stress
test or an independently calibrated thermal result.

The PCM MVP is useful evidence about packet-stack scheduling, bounded loss
handling and digital quality under these conditions. Optional packetized PDM
would still be a packet transport. The separately requested proprietary
continuous-radio arm and its RF/frame results belong in the
[SDR proof report](SDR-PROOF.md) and subsequent isolated SDR-metal evidence.
