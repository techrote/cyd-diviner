# Expanded mission coverage audit

Audit snapshot: **2026-11-27T22:27:25.172896+00:00**. This supplements the baseline performance mission
with the explicitly requested packed-colour, PCM and greenfield SDR work. It
uses closed captures/analyses only. The matched S3↔S3 PCM campaign was still
running at this snapshot and is excluded; near-metal SDR measurements are
pending. This file does not replace the mission-status or recommendations files.

| Requested work | Evidence established | Remaining limit |
| --- | --- | --- |
| RGB111, RGB444, RGBA5551 fullscreen paths | 36/36 measured windows; three repetitions of twelve settings; actual packed eight-row intermediates and coverage/alpha handling | Existing renderer's one/two effect paths only; no arbitrary-layer compositor or full resident packed framebuffer; RGB565 raster/SPI unchanged |
| Tiny-block PCM transport MVP | Three valid finite S3→CYD digital reconstruction windows across two separately retained closed cohorts | Same 24 kHz/stereo/8-frame/requested-1Mbps case only; broad bidirectional plan failed; no DAC or synchronized one-way latency |
| Original proprietary SDR/PDM MVP | Actual carrier keying plus S3 IQ; 13 acquired framed runs with known payload comparison; one incomplete acquisition preserved/skipped | Finite precomputed 500–2,000 Hz sources, host/batch decoder; missing leading frames and mono startup errors; no continuous live audio or DAC |
| Continuous-bitstream impairment model | Portable sigma-delta encoder/reconstruction and deterministic raw-bit impairment outputs, separately classified OFFLINE_MODEL | Model results are neither radio measurements nor a complete physical audio link |
| Bare near-metal SDR limits | Separate classic/S3 TX and S3 snapshot RX projects, pinned source/ABI/build/test evidence | Compilation and CPU/control-flow tests only at this snapshot; physical RF throughput/rate/quality pending |

## Packed-colour scope is complete for the implemented paths

[COLOR-FINDINGS.md](COLOR-FINDINGS.md) and the
[published report](campaign-color/REPORT.md) retain the 36 completed windows.
The classic CYD ran the separately approved `cyd-profile-audio` image at
240 MHz with 40 MHz LCD SPI: source
`ad9cbfd670e456de5e08fca19810fb93f7f1db47`, image SHA256
`407eb96f8fe2b74ed1cf45f4a163558eacde0ff287a4b878a9ed1faadfaccc93`.

Native Gate measured 25.00 FPS with one effect and 22.33 FPS with Gate+Trails.
The added generic conversion/compositing implementation measured 7.89/3.67 FPS
for RGB111 and 7.00/3.00 FPS for both RGB444 and RGBA5551. Plasma+Kaleido and
Ashfire+Rain also exercised the existing two effect paths. These results do not
establish limits of a renderer designed to rasterize directly into packed data.

The eight-row packed base payloads are 960/3,840/5,120 bytes for RGB111/RGB444/
RGBA5551. Two-effect storage is 2,240/8,000/10,240 bytes, including the external
one-bit coverage mask for RGB111/444. Two RGB565 transfer strips add 10,240
bytes. Existing canvas backing remains 64,800 bytes. Thus there is **no measured
backing reservation saving or SPI byte reduction** in these results. RGB5551
binary coverage is implemented in bit 0, with 5/5/5 colour; it is not a native
alpha rasterizer. Portable packing correctness tests and measured render cost
are separate evidence.

Seventy-two retained colour-window sensor samples reported classic RTC-library
raw codes 136–138, uncalibrated. This is not a calibrated die-temperature result
or the extended stress test. No contact-to-photon/analog fixture is added by the
colour experiment. The [raw colour archive](PHYSICAL-CAPTURES-COLOR.zip) and
[receipt](color-archive-receipt.json) preserve all completed windows.

## PCM: measured digital behavior and failed resource/metadata coverage

Two analyzed closed scopes are available:

* [Fresh deferred single](audio-deferred-fresh-analysis/AUDIO-REPORT.md):
  capture COMPLETE, one MEASURED run.
* [Bidirectional deferred plan](audio-bidirectional-deferred-analysis/AUDIO-REPORT.md):
  capture FAILED, 48 attempted repetitions, only two MEASURED and 46 FAILED.
  Forty-five failures report `bounded telemetry allocation or length failure`;
  one reports an acknowledgment/summary timeout. They are preserved as failures,
  not silently counted as packet loss, zero throughput or completed windows.

All three valid runs are the same **S3 peer→classic CYD, PCM16, 24 kHz, two
channels, eight frames/block, requested PHY 1 Mbps**, with a 20-byte application
header, one packet in flight, no MAC acknowledgment requirement and no
application retransmission. They each cover 9,000 source blocks over three
seconds. Other rates, block lengths and the opposite direction do not acquire
valid performance results merely because they were present in the failed plan.

| Closed scope / run | TX API accepted / RX admitted packets | PLC blocks / 9,000 rendered | PLC fraction | Source/error SNR per channel, dB |
| --- | ---: | ---: | ---: | --- |
| Fresh / a001_1 | 1,896 / 1,756 | 7,244 | 80.489% | 1.645 / 1.126 |
| Bidirectional plan / a001_1 | 2,076 / 1,976 | 7,024 | 78.044% | 2.005 / 1.254 |
| Bidirectional plan / a003_3 | 2,110 / 2,025 | 6,975 | 77.500% | 2.038 / 1.331 |

Quality includes held/faded concealment against the unshifted deterministic
source on rendered blocks. All scheduled blocks were rendered in these three
windows, so the poor quality is not concealed by excluding unrendered source
coverage. The target exports energy/error sums; correlation is unavailable and
must not be invented. API acceptance and a successful broadcast-completion
callback do not prove receiver delivery. Their difference from admitted unique
RX packets includes callback/queue, stale/deadline and observation effects as
well as RF; it is not an isolated RF loss rate.

The source configuration also has a simple **calculated nominal budget**:
24,000 / 8 = 3,000 blocks/s; each carries 32 PCM bytes plus 20 application-header
bytes. This is **1.248 Mbps before any 802.11 framing/contention/PHY overhead**,
so it cannot fit an actually honored 1 Mbps PHY setting even with otherwise
ideal delivery. This arithmetic is separate from measured transport counters.
The requested PHY setting returned success, but active over-air rate/channel/
power-save readback was not measured in the deferred cohort. The calculation
must not be presented as that missing RF readback.

Maximum callback ages at the receiver were 179–226 µs, slot dwell 2,190–2,402 µs
and rendering lateness 307–384 µs. These are **receiver-local scheduling**
observations. Sender broadcast-callback maxima were 6,885–8,965 µs. None is
synchronized source-to-output latency. Receiver summary heap minima were
14,228–14,296 bytes under their recorded allocator scope; those fields alone do
not establish byte-capable contiguous telemetry allocation headroom. In
particular, a combined allocation-or-length diagnostic does not itself isolate
its two possible failure causes.

These measurements used source `ad9cbfd...`, source-content digest
`49fcfc731717626ed1b102f9d4d4ef5f135b4dbb91f1e323f6853eaa2c357ea6`.
The CYD image is the colour image above; the S3 `kernel-s3-n4r2-audio` image is
`db7a8db3115f061ee01b82850d679e8487467a05f0f220bab2d2957b87eb22b8`.
Matching start ACKs plus separately retained post-stop summaries validate only
the marked MEASURED windows. Post-stop info cannot be substituted for live RF
readback. Both analysis manifests' source/output hashes were checked during
this audit. Raw [fresh](physical/audio-deferred-fresh-01/results.json) and
[failed broad plan](physical/audio-bidirectional-deferred-01/results.json)
receipts retain their original statuses.

Earlier failed setup/single attempts and the zero-run matched-S3 attempt are
also retained under `physical/audio-*`. They do not establish opposite-direction
or matched-S3 performance. The currently running matched-S3 campaign is
intentionally excluded from this snapshot. No microphone input, I2S/DAC output,
analog fidelity, calibrated temperature or continuous SDR result follows from
these packetized PCM tests.

## Original SDR: physical finite proof, not continuous audio

[SDR-PROOF.md](SDR-PROOF.md), [JSON](SDR-PROOF.json) and [CSV](SDR-PROOF.csv)
separate finite source-frame coverage from conditional payload BER. Three
23 ksymbol/s, 2 kHz stereo OSR4 runs each recovered sequences 1–15 out of sixteen
source frames, with zero errors in 3,840 compared payload bits. Two slower mono
runs recovered all headers but each accepted 101 corrupted payload bits in
sequence 0. Missing sequence 0 and these startup errors remain explicit.

This is original NRZ/OOK framing and TDM-PDM carried by actual carrier keying
and raw IQ, **not PDM over ESP-NOW**. The licensed, pinned ESP-SDR receiver
backend is separately attributed. Physical source triangles are 30/47/64/81 Hz
at 500–2,000 samples/s per channel, with intentionally coarse first-order OSR4
and simple boxcar reconstruction. In the zero-compared-error received frames,
reconstruction matches ideal boxcar samples exactly while still having large
codec distortion versus the source. Conditional zero observed payload BER is
not complete delivery or a future error-rate guarantee.

The [raw SDR archive](PHYSICAL-CAPTURES-SDR.zip) and
[receipt](sdr-archive-receipt.json) retain all ten closed raw capture directories,
including failed/partial/zero-run attempts: 259 verified manifest entries and
269 archived files. The three framed analysis folders are separate derivatives;
earlier v1 failed analysis is retained rather than overwritten. The separate
[calibration interpretation](SDR-CALIBRATION-INTERPRETATION.md) explains the
receiver-offset confound, positive centered envelope and 250 kS/s sample gaps.

A 43.478 µs nominal symbol period at 23 ksymbol/s is arithmetic serialization,
not one-way latency. The maximum tested source rate here is 2 kHz stereo. The
experiment does not establish 24/48 kHz multichannel continuous RF, live on-device
demodulation, DAC output, RF range/sensitivity/spectral quality or DSP RPC
turnaround.

## Near-metal measurements remain pending

The isolated [classic TX](../experiments/sdr-metal/tx/README.md),
[S3 TX](../experiments/sdr-metal/tx-s3/README.md) and
[S3 snapshot RX](../experiments/sdr-metal/rx/README.md) remove the Diviner and
continuous-IQ FIR/USB workload from the next test context. RX reserves the real
64 KiB SRAM aperture, supports nominal 16/40/80 MS/s finite captures with
post-capture raw transfer, and has a no-export repeated-capture cost mode.
Default direct PHY initialization has explicit Wi-Fi-NULL fallback. Source,
SDK, binary and raw-word provenance remain distinct.

At this audit snapshot these have **build, source/ABI review and host tests**,
not measured physical RF results. CPU MMIO writes/s, traced software completion
or nominal sample-clock selection must not become claims of radiated symbol
throughput, recovered bits/s or analog/audio latency. Snapshot gaps and capture
horizons must remain explicit. Owner review/authorization and exact final images
precede their separate physical tests; no approved image is silently replaced.
