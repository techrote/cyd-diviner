# Raw-radio SDR research for CYD and the owned S3

Review date: **2026-11-27**. This is primary-source research, not a physical
measurement on COM21/COM22. No board was accessed, no firmware was changed,
and no imported SDR implementation was built or flashed for this review.

This research-only snapshot predates the separately authorized physical stage.
[SDR calibration interpretation](SDR-CALIBRATION-INTERPRETATION.md) now records
actual received-envelope observations; it does not turn the external projects'
claims below into measurements on the owned boards.

The developments are real: raw receive samples can bypass the Wi-Fi modem,
and one S3 project demonstrates radiating data through tone keying. The
reviewed releases do **not** supply an autonomous classic-ESP32 ↔ ESP32-S3
transport with a measured few-thousand-microsecond job turnaround. Receiving
I/Q quickly, moving I/Q to a PC, and completing a validated radio request and
result are different measurements.

## Updated research scope

The four uses are DSP request/result RPC; continuous one-way audio/control
streaming with a preserved PCM/reliability baseline; remote video/display at
lower priority; and FPV-inspired non-video multichannel audio that explicitly
accepts noise, distortion or erasures for low delay. They are not mutually
exclusive choices. ESP-NOW, raw 802.11, SDR, external packet radios and analog
FM are technologies to compare across those uses.

[LOW-LATENCY-AUDIO-RESEARCH.md](LOW-LATENCY-AUDIO-RESEARCH.md) provides the
four-arm matrix, 24/48 kHz and 2/4/8-channel payload budgets, 8/16/32-frame
fill periods, precision changes, deadline/FEC tradeoffs and analog/PDM limits.
It also distinguishes the current IDF 4.4.7 CRC-error receive restriction from
an untested, version-specific bad-FCS filter declaration in IDF v6.1. This
extension does not add an SDR or audio-link measurement to the campaign.

## Source snapshots

All source links below are pinned to the reviewed commits. Dates are Git
metadata from the repository's public GitHub commit API, not inferred from
article publication dates.

| Project | Reviewed commit | Author / committer UTC | Actual role |
| --- | --- | --- | --- |
| [lozaning/ESP32SDR](https://github.com/lozaning/ESP32SDR/commit/dbff7bb8ca8470e92e18de9cfd3f81aabe55f6f1) | `dbff7bb8ca8470e92e18de9cfd3f81aabe55f6f1` | Both 2026-09-30 09:28:08 | S3 burst RX and experimental TX; PC-orchestrated messages. |
| [ESPARGOS/esp-sdr](https://github.com/ESPARGOS/esp-sdr/commit/3ab3b65bb62f2d9427ee33c2c3b627427e729294) | `3ab3b65bb62f2d9427ee33c2c3b627427e729294` | 2026-11-14 09:06:35 / 2026-11-16 21:13:26 | Receive-only firmware, including newer S3 continuous modes. |
| [h0m3us3r/eSpDR](https://github.com/h0m3us3r/eSpDR/commit/f279bf823eee41796dfd1ac21f13e1ed9b418c82) | `f279bf823eee41796dfd1ac21f13e1ed9b418c82` | Both 2026-10-02 02:21:18 | S3 receive stream through an external FPGA and USB 3 bridge. |

Metadata: [ESP32SDR commit API](https://api.github.com/repos/lozaning/ESP32SDR/commits/dbff7bb8ca8470e92e18de9cfd3f81aabe55f6f1),
[esp-sdr commit API](https://api.github.com/repos/ESPARGOS/esp-sdr/commits/3ab3b65bb62f2d9427ee33c2c3b627427e729294),
[eSpDR commit API](https://api.github.com/repos/h0m3us3r/eSpDR/commits/f279bf823eee41796dfd1ac21f13e1ed9b418c82).

## What the transmit demonstration establishes

ESP32SDR reports board-to-board OOK/ASK and two-frequency data with zero bit
errors in its demonstration, using two nearby LILYGO T-Dongle-S3 boards.
Its successful TX primitive keys the PHY's RF-test carrier, rather than
replaying arbitrary complex samples. The named GFSK choice uses the same
unshaped two-frequency path. The README supplies no trial count, autonomous
bitrate or latency-tail dataset for the zero-error claim. The repository
description's “full duplex” must not be read as proof of simultaneous
bidirectional application traffic on one device.
[ESP32SDR README](https://github.com/lozaning/ESP32SDR/blob/dbff7bb8ca8470e92e18de9cfd3f81aabe55f6f1/README.md)

The implemented carrier calls `esp_phy_wifi_tx_tone`; TX entry invalidates
RX readiness, and returning to receive stops TX and restores the receive
path. Snapshot acquisition stops before packing, CRC, USB transmission and
the optional LCD waveform update. Those operations create receive gaps.
This source contains useful raw-radio primitives, but no independent
scheduled peer modem or concurrent two-way protocol.
[S3 receiver/TX source](https://github.com/lozaning/ESP32SDR/blob/dbff7bb8ca8470e92e18de9cfd3f81aabe55f6f1/main/targets/esp32s3/receiver.c)

The message function on the **PC** sets each transmitted bit, takes three
16,380-sample receive captures by default, then decides that bit using NumPy
power or FFT calculations. It adds five framing bytes and limits payloads to
30 bytes. There is no fixed autonomous symbol clock or receiver clock recovery
in that function. From these constants, at nominal 80 MS/s, capture time
alone is **614.25 µs per bit**, at least **29.484 ms for one payload byte** or
**171.990 ms for 30 bytes**. These are calculated lower bounds, excluding
calibration, USB, commands and PC processing; they are **not measured link
latencies**. This demonstration therefore does not establish a 1–3 ms packet
or job service.
[Host message implementation](https://github.com/lozaning/ESP32SDR/blob/dbff7bb8ca8470e92e18de9cfd3f81aabe55f6f1/gui/link.py)

Each capture transfers 32,760 bytes of signed 8-bit I/Q to the PC and checks
its CRC before returning. The host opening the native USB port at “115200”
does not make that USB payload a 115200-baud UART stream; USB throughput and
scheduling would need separate measurement.
[Host device/capture implementation](https://github.com/lozaning/ESP32SDR/blob/dbff7bb8ca8470e92e18de9cfd3f81aabe55f6f1/gui/devices.py)

Arbitrary-I/Q TX, BPSK and QPSK are not working features of this release.
There is an evidence distinction: its README describes the replay path as
non-radiating, while the retained reverse-engineering notes still list
unresolved routing/format questions. Treat this as **no demonstrated usable
arbitrary-waveform transmitter here**, not a proof that all future S3 work
must fail. A completed engine trigger is not proof of the commanded RF
waveform at the antenna.
[Retained TX findings](https://github.com/lozaning/ESP32SDR/blob/dbff7bb8ca8470e92e18de9cfd3f81aabe55f6f1/docs/FINDINGS.md)

This build explicitly accepts **ESP32-S3 only**. Its code cannot be assumed
to provide a classic-ESP32 transmitter for the CYD.
[Target restriction](https://github.com/lozaning/ESP32SDR/blob/dbff7bb8ca8470e92e18de9cfd3f81aabe55f6f1/main/CMakeLists.txt)

## Reception: bursts versus genuinely continuous modes

ESPARGOS currently supports classic ESP32 and S3 reception at nominal
16/40/80 MS/s. Current S3 support includes continuous capture; an older
description calling every S3 mode burst-only would now be wrong. Accepted
frequency-command limits are not the PLL's usable tuning range: the reviewed
classic/S3 hardware remains a receiver around its 2.4 GHz region, with the
documented lower-frequency extension, rather than arbitrary 100–6000 MHz
coverage. This project remains receive-only.
[esp-sdr README](https://github.com/ESPARGOS/esp-sdr/blob/3ab3b65bb62f2d9427ee33c2c3b627427e729294/README.md)

The classic backend reserves a fixed **64 KiB SRAM aperture** outside normal
allocation, triggers a bounded snapshot and stops the engine before accessing
the result. It does not expose the S3 continuous path. Integrating this on a
running CYD requires memory/linker and radio-lifecycle work, rather than
calling a receive function against an arbitrary heap buffer.
[Classic backend](https://github.com/ESPARGOS/esp-sdr/blob/3ab3b65bb62f2d9427ee33c2c3b627427e729294/main/targets/esp32/receiver.c)

For snapshot raw I/Q, finite sampling time is not the same as continuous
coverage. The dump engine writes SRAM; the CPU must regain the bank before
reading it. Circular modes instead rotate banks and explicitly detect late
or overwritten work. A 16,380-pair burst at 80 MS/s spans only **204.75 µs**;
that duration does not include processing or export.
[Capture mechanism](https://github.com/ESPARGOS/esp-sdr/blob/3ab3b65bb62f2d9427ee33c2c3b627427e729294/docs/how-it-works.md)

The UART transport defaults to **2 Mbaud, 8N1**. As a calculated upper bound
on raw CAP16 snapshot duty cycle, sending 32,760 payload bytes requires at
least 163.8 ms, before headers or computation. For the same 16,380 pairs:

| Nominal ADC rate | Captured RF time | Payload-only UART time | Upper bound on captured-time fraction |
| --- | --- | --- | --- |
| 80 MS/s | 204.75 µs | 163,800 µs | 0.12484% |
| 40 MS/s | 409.50 µs | 163,800 µs | 0.24938% |
| 16 MS/s | 1,023.75 µs | 163,800 µs | 0.62112% |

These are arithmetic bounds for sequential capture/export, **not observed
CYD SDR duty cycles**. Native USB and on-chip reduction have different limits.
[UART implementation and framing](https://github.com/ESPARGOS/esp-sdr/blob/3ab3b65bb62f2d9427ee33c2c3b627427e729294/main/common/burst_serial.c)

On S3, current `IQS` filters and decimates the nominal 16 MS/s input to
**15.625–250 kS/s** over native USB. The project reports gap-free 60-second
runs for 4/8-bit components at 250 kS/s and 16-bit components at 125 kS/s;
16-bit at 250 kS/s delivered about 80% of samples. Frame indices, gap flags
and CRCs make losses inspectable. Core 1 performs the filtering. These are
reported receive/export tests, not RF payload goodput or request/result RTT.
[Continuous I/Q protocol and reported tests](https://github.com/ESPARGOS/esp-sdr/blob/3ab3b65bb62f2d9427ee33c2c3b627427e729294/docs/iq-stream.md)

Continuous spectral capture also does not mean every sample is FFT-analyzed.
The implementation reports analyzed coverage and skipped work. Its S3 worker
uses SIMD unpacking/FFTs while core 0 rotates banks and feeds USB. Continuous
capture masks interrupts for the run; its hot code/data live in internal RAM.
The supplied S3 profile disables task/interrupt watchdogs for this mode. These
scheduling choices cannot be copied into Diviner while assuming its existing
audio cadence and display tasks continue normally.
[Spectral scheduling and coverage](https://github.com/ESPARGOS/esp-sdr/blob/3ab3b65bb62f2d9427ee33c2c3b627427e729294/docs/spectrum.md),
[S3 configuration](https://github.com/ESPARGOS/esp-sdr/blob/3ab3b65bb62f2d9427ee33c2c3b627427e729294/sdkconfig.defaults.esp32s3)

The S3 circular path reserves **three 64 KiB banks, 192 KiB total**, and uses a
dedicated second-core worker. That is a substantial internal-memory and CPU
commitment, independent of this board's external PSRAM.
[Bank definitions and interrupt contract](https://github.com/ESPARGOS/esp-sdr/blob/3ab3b65bb62f2d9427ee33c2c3b627427e729294/main/common/ring_capture.h)

## What eSpDR's 80 MS/s breakthrough means

eSpDR continuously extracts **80 million receive I/Q pairs/s** from an S3,
then uses both cores bare metal, 16 dedicated GPIO data wires, an Alchitry Au
FPGA, DDR3 buffering and FT600 USB 3 to reach a PC. The reported typical
compressed host flow is about 95–100 MB/s. It is receive-only and requires
additional hardware; this is not 80 Mbit/s of application data exchanged by
two unmodified ESP boards. Its roughly 190 µs link units refer to the wired
sample-export framing, not a measured wireless RPC. Its published receiver
tuning reaches roughly 1.84–2.79 GHz, with board-dependent endpoints.
[eSpDR architecture and measurements](https://github.com/h0m3us3r/eSpDR/blob/f279bf823eee41796dfd1ac21f13e1ed9b418c82/README.md)

The file named `transmit.S` packs received samples onto the GPIO connection.
Likewise, the host stream decoder validates/decompresses sample records;
its name is not evidence of an on-board RF packet demodulator. Those paths
do not add a wireless transmitter or autonomously decoded return channel.
[GPIO output kernel](https://github.com/h0m3us3r/eSpDR/blob/f279bf823eee41796dfd1ac21f13e1ed9b418c82/esp32s3/src/transmit.S),
[Host stream decoder](https://github.com/h0m3us3r/eSpDR/blob/f279bf823eee41796dfd1ac21f13e1ed9b418c82/host/src/decoder.cpp)

## Implication for the owned pair and FPV-style transmission

| Need | Evidence for classic CYD + owned S3 |
| --- | --- |
| Observe non-Wi-Fi RF samples | Supported receive primitives on both chip families; not yet tested on these owned boards. |
| Continuous S3 samples for a PC | Demonstrated by the projects, with the bandwidth, CPU and external-hardware distinctions above. |
| Simple custom RF symbols | S3 carrier-keying demonstration exists; classic TX and autonomous two-way operation are not supplied by it. |
| Few-thousand-µs validated control exchange | No measured autonomous endpoint-to-endpoint tail distribution in the reviewed paths. Requires a new modem/protocol experiment. |
| DSP job offload | Also needs framing, clock recovery, CRC, loss/retry policy, queues and timed compute/result receipt. I/Q sample rate establishes none of those terms. |
| Continuous analog camera transmission | No demonstrated composite-video modulation/demodulation chain in these releases; arbitrary-waveform TX is not working in the reviewed S3 TX project. |

The FPV analogy is useful as a question about avoiding packet queues and
frame buffering. It does not make an RF-test tone or a receive-only ADC dump
into an analog video modem. In particular, the few-hundred-microsecond
capture/unit durations above are sample-acquisition/export intervals, not
camera-to-display latency or validated application-message turnaround.

For VIBEWARE, these projects nominate a **separate raw-radio research
experiment**, with autonomous on-device modulation/demodulation and a
single-clock end-to-end latency measurement. They do not currently supersede
the mission's measured ESP-NOW/Wi-Fi/BLE transports. A first experiment should
use small control payloads and quantify delivery/tails plus simultaneous
audio/display impact. Porting a PC-coordinated tone demo would not by itself
demonstrate a useful DSP-offload service.

The local kernel evidence already limits the allowance for offloading these
same scalar jobs: selected CYD minus S3 median differences are only 17–912 µs,
before transfer or packing. Even a future few-millisecond radio exchange
would not beat those particular local alternatives on latency. Larger peer
workloads or peer-retained state with compact returns are different candidate
boundaries and require their own complete measurements.
[Measured comparison](S3-COMPARISON.md),
[Offload worksheet and remaining evidence](RECOMMENDATIONS-DRAFT.md).

No SDR latency, BER, bandwidth or duty-cycle value in this research note is
entered as a new owned-hardware measurement. Project-reported observations,
source inspection and calculations are labeled separately throughout.
