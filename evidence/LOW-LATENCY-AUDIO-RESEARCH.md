# Four research arms and low-latency multichannel audio

Research snapshot: **2026-11-27**. This note combines source inspection,
manufacturer documentation and explicitly calculated budgets. It contains no
new owned-hardware audio-link, analog-delay or packet-latency measurement.
No purchase, board access or firmware modification was performed.

## Four arms, with technologies compared across them

The arms describe uses, not radio technologies. All four remain in scope;
remote video/display has lower priority. Accepting distortion in arm 4 does
not weaken the precision/reliability baseline for arms 1 and 2.

| Arm | Required evidence | Candidate technologies and current boundary |
| --- | --- | --- |
| 1. DSP request/result RPC | One initiating clock from job submission to validated computed result; actual input/output precision, bytes, deadline, failures and simultaneous audio/display impact. | Existing ESP-NOW/Wi-Fi/BLE measurements inform screening; raw 802.11, custom SDR or an external packet radio require separate actual-job tests. Echo RTT is not a compute-offload result. |
| 2. Continuous one-way audio/control stream | Preserve the stated PCM precision and reliability baseline; count accepted, unique, late, missing and concealed frames; measure sustained channel rate and clock drift. | Packet transports can be tested with sequence/sample indices and deadlines. Companding, ADPCM and analog conversion are distinct changed-quality modes. Controls need age/latest-state rules rather than replaying stale values. |
| 3. Remote video/display — lower priority | Actual camera/display latency, frame loss, bandwidth and local rendering/SPI cost. | Digital frames/deltas and external analog FPV are different experiments. ESP32 SDR reception alone supplies no complete video transmitter or display pipeline. |
| 4. FPV-inspired non-video multichannel audio | Accept an explicit noise, distortion or erasure tradeoff for low sample-to-output delay; 2/4/8 independent channels, channel alignment, tails, interference response and recovery. | Small deadline packets with concealment/FEC; external FM/FDM; or a custom raw modem/PDM experiment. None currently promises 1 ms or bit-exact PCM. |

The [SDR review](SDR-RESEARCH.md) separates demonstrated receive extraction,
S3 tone-keyed messages, continuous I/Q export and unsupported arbitrary-I/Q
TX. Those capabilities are cross-cutting research inputs, not extra arms.

## Calculated payload and block-fill budgets

Here a **frame** contains one simultaneous sample from every channel. For
sample rate `fs`, channels `C`, coded bits/sample `b` and `N` frames/block:

```text
sample payload rate = fs * C * b                   [bit/s]
sample payload bytes/block = N * C * b / 8         [bytes]
block period / conservative fill allowance = N/fs [seconds]
blocks per second = fs/N
```

The complete 54-case arithmetic grid is in
[LOW-LATENCY-AUDIO-BUDGETS.csv](LOW-LATENCY-AUDIO-BUDGETS.csv), explicitly
labeled `calculated_not_measured`. The formulas above reproduce it.

These exclude transport headers, framing, predictor state, FEC, preambles,
radio switching, queueing, clock recovery and converters. The oldest sample's
accumulation span is `(N-1)/fs` if capture begins exactly at its sample instant;
`N/fs` is the useful conservative block-period allowance. Neither is measured
end-to-end delay.

| Sample rate | Channels | 16-bit linear PCM Mb/s | 8-bit companded Mb/s | 4-bit ADPCM codewords Mb/s |
| --- | --- | --- | --- | --- |
| 24 kHz | 2 | 0.768 | 0.384 | 0.192 |
| 24 kHz | 4 | 1.536 | 0.768 | 0.384 |
| 24 kHz | 8 | 3.072 | 1.536 | 0.768 |
| 48 kHz | 2 | 1.536 | 0.768 | 0.384 |
| 48 kHz | 4 | 3.072 | 1.536 | 0.768 |
| 48 kHz | 8 | 6.144 | 3.072 | 1.536 |

**8-bit companding and 4-bit ADPCM change precision.** Neither preserves
arbitrary 16-bit linear PCM. RFC 3551 describes eight-bit logarithmically
scaled PCMA/PCMU and four-bit DVI4 ADPCM, including predictor/index state.
Their sample-based rates can be specified separately, but a 24/48 kHz mode
must state its rate and format; it must not inherit a narrowband voice-quality
claim. The ADPCM numbers above count codewords only. Independent recovery
after loss requires predictor-state handling and an explicit multichannel
packing format; RFC 3551 leaves DVI4 multichannel packing unspecified.
[RFC 3551 §§4.5.1, 4.5.14 and Table 1](https://www.rfc-editor.org/rfc/rfc3551.html)

| Rate | 8 frames: fill / packets/s | 16 frames: fill / packets/s | 32 frames: fill / packets/s |
| --- | --- | --- | --- |
| 24 kHz | 333.333 µs / 3,000 | 666.667 µs / 1,500 | 1,333.333 µs / 750 |
| 48 kHz | 166.667 µs / 6,000 | 333.333 µs / 3,000 | 666.667 µs / 1,500 |

The next table is sample payload bytes per block, shown as **8 / 16 / 32
frames**. It applies at either sample rate; rate changes the arrival period.

| Channels | 16-bit PCM bytes | 8-bit companded bytes | 4-bit ADPCM codeword bytes |
| --- | --- | --- | --- |
| 2 | 32 / 64 / 128 | 16 / 32 / 64 | 8 / 16 / 32 |
| 4 | 64 / 128 / 256 | 32 / 64 / 128 | 16 / 32 / 64 |
| 8 | 128 / 256 / 512 | 64 / 128 / 256 | 32 / 64 / 128 |

Small blocks are not automatically efficient. Reusing this mission's
**40-byte application header** at 6,000 packets/s would consume **1.920 Mb/s
in that header alone**, before radio framing. This is a calculation using the
current [link packet layout](../src/perf_link.cpp), not a proposed production
audio format or observed sustained rate. For an illustrative independently
decodable ADPCM layout using four state bytes/channel per block, eight frames
produce four codeword bytes/channel plus four state bytes/channel: the sample
coding's nominal 4:1 reduction becomes 2:1 before other headers. That custom
multichannel layout is not implemented or claimed as interoperable DVI4.

For audio, model the budget from sample capture to analog output:

```text
L = ADC/filter delay + block fill + encode/packetize
  + queue/access/airtime + receive/decode
  + playout reserve + DAC/filter delay
```

A peer DSP insert also adds input transmission, remote work and return
transmission. This can be pipelined for throughput, but a pipeline does not
remove the latency of an individual sample. One-way targets cannot be proved
by dividing RTT by two without clock/asymmetry evidence.

## Existing CYD audio establishes a different baseline

The reviewed source uses mono int16 callbacks at **24 kHz**, in **128-frame
blocks**, with **four 128-frame DMA buffers**, two 16-bit I2S slots and the
same mixed sample in both slots. Only the GPIO26 DAC channel is enabled; the
internal DAC takes the top eight bits. This is not eight independent output
channels. New multichannel audio needs a separately validated input/output
interface and pin/resource budget.
[HAL declaration](../src/hal.h), [CYD audio task and setup](../src/hal_cyd.cpp),
[PCM protocol](../src/cyd_pcm_protocol.h)

Calculated block period is **5.333333 ms**; four buffers represent **21.333333
ms of nominal capacity**. Capacity is not a measurement of queue occupancy or
analog latency. Espressif documents `i2s_write` as writing the DMA TX buffer,
from which transfer proceeds automatically; the function's return is not the
analog output instant. A sub-millisecond objective therefore requires a
different buffering/conversion experiment, not simply a faster radio added
to the unchanged 128-frame path.
[Pinned IDF 4.4.7 I2S documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/peripherals/i2s.html)

## Deadline packets: tolerate erasures without unbounded waiting

A candidate low-delay stream should give each block a sample index and
playout deadline, bound the queue, discard stale blocks and conceal a missing
block at its deadline. Repetition with a short fade, interpolation using
already available neighbors, or a brief fade to silence are candidate policies
to compare for clicks, error energy and CPU cost. None reconstructs unknown
audio exactly. Interpolation that waits for a future block consumes more
latency. Channels should share one timeline and concealment decision to avoid
uncontrolled interchannel skew.

“No retry” must distinguish **no application retransmission** from hidden
MAC retries and contention. UDP alone does not disable 802.11 MAC behavior.
Raw-frame send API acceptance is not airtime, receiver proof or a deadline
guarantee. The pinned SDK documents raw non-QoS frame transmission, default
rate selection and callback constraints; that path still uses the Wi-Fi
modem. It is not a sampled-waveform radio interface.
[Pinned Wi-Fi raw-send/sniffer guide](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-guides/wifi.html#wi-fi-80211-packet-send)

Packet FEC is a separate quality/latency choice. One XOR parity packet for
`k` equal data packets can recover one erased packet if the other packets
and parity arrive; payload overhead is `1/k`, before their headers. If parity
is sent after the group, repair of the first packet waits for the remaining
`k-1` block periods plus parity transmission. For `k=2`, 16-frame blocks at
48 kHz add at least **333.333 µs of group formation** for that repair path,
while costing **50% additional parity payload**. Interleaving can spread burst
loss but extends the repair horizon. Deadline misses beyond the allowed
horizon still need concealment. These are design calculations, not this
campaign's implemented FEC or measured quality.
[IETF generic media FEC and XOR recovery](https://www.rfc-editor.org/rfc/rfc5109.html)

## CRC/FCS: distortion tolerance does not expose corrupted PCM automatically

The current **IDF 4.4.7** Wi-Fi guide explicitly puts CRC-error frames under
the frames a sniffer **will not dump**. Ordinary ESP-NOW/UDP audio must
therefore treat such corruption as missing delivery and concealment, rather
than assuming it can play the damaged sample bytes. This campaign also
rejects invalid application CRCs.
[Pinned sniffer limitations](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-guides/wifi.html#wi-fi-sniffer-mode),
[Current link validation](../src/perf_link.cpp)

There is a version-specific research lead: the **IDF v6.1 public header**
defines `WIFI_PROMIS_FILTER_MASK_FCSFAIL`, with a warning in its comment.
The public tag resolves to commit
`fff9895c82d744c7237be8847347bdd1b07c6643` (committed 2026-08-25); the reviewed
header SHA256 is
`8e398a8d22b18c199ea6d48e42784e8a897c22000a09ee9ac7d22cf0f5110e5c`.
That declaration establishes an advertised filter capability, **not a tested
corrupt-audio channel on these two chips**. It is outside the approved 4.4.7
images. A future investigation must verify target support, callback length/
status, whether PHY-decoded bytes exist, address/frame synchronization and
actual failure behavior. A missing/undecodable header cannot be recovered just
by accepting bad FCS; turning off checks can also turn bounded erasures into
large PCM impulses or persistent ADPCM predictor errors.
[Pinned v6.1 header, filter definitions](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_wifi/include/esp_wifi_types_generic.h#L720)

## External packet radio candidate: SX1280 FLRC

Semtech's manufacturer-authored **SX1280/1 Rev 3.3, September 2023** datasheet
specifies FLRC up to **1.3 Mb/s raw**, coding rates 1, 3/4 and 1/2, effective
rates 1.3/0.975/0.650 Mb/s at that setting, and **6–127-byte payloads**.
Preamble, synchronization, optional CRC and host-interface/switching time
still consume budget. It is an external radio, not a mode enabled on the
owned ESP32 radio. Its packet engine/FEC makes it a concrete alternative to
writing a software modem; none of those specifications guarantees application
latency. Semtech explicitly describes the rate/sensitivity tradeoff.
[Semtech product and official document listing](https://www.semtech.com/products/wireless-rf/lora-connect/sx1280),
[Semtech-authored datasheet, distributor-hosted copy, §§6.3, 7.3, 14.3](https://www.hy-line-group.com/products/hcp/datasheet/semtech/sx1280-1_datasheet.pdf),
[Semtech FLRC guidance](https://www.semtech.com/design-support/faq/p0/P60)

The payload table rules out **48 kHz × eight channels** even at four bits
(1.536 Mb/s exceeds 1.3 Mb/s), and rules out 48 kHz stereo 16-bit PCM before
framing. At 24 kHz × eight channels, four-bit codewords need 0.768 Mb/s:
below uncoded/3/4 ceilings but above the 1/2-coded ceiling, with all state and
framing still omitted. This is capacity screening, not a feasible-link claim
or a purchasing recommendation. Tiny blocks may need fragmentation or a
smaller format; reducing payload precision changes the comparison.

## Continuous analog FM/FDM candidate

RichWave's public manufacturer catalog lists **RTC6701/RTC6711 at 2.4 GHz
FM/FSK** and **RTC6705/RTC6715 at 5.8 GHz FM** as analog wireless audio/video
parts. It establishes that external analog TX/RX building blocks exist; the
catalog supplies no measured sample-to-output delay, eight-channel separation
or fidelity guarantee for our application. No gated/private datasheet was
accessed.
[RichWave analog wireless audio/video catalog](https://www.richwave.com.tw/en/page_prd_new.aspx?Id=8)

Conceptually, continuous baseband FM can avoid packet accumulation and
retransmission. Multiple independent audio channels could use multiple RF
paths or frequency-division subcarriers plus filters/demodulators. That adds
bandwidth, guard bands, crosstalk, filtering and converter delay; a stereo
audio pin label would not establish an eight-channel PCM service. The result
is analog audio with noise/distortion, not preserved PCM words. Interference
can cause hiss, capture, threshold effects or muting rather than a uniformly
graceful degradation. These are architectural considerations to measure, not
latency/fidelity values attributed to those catalog parts.

Such a path needs a stated audio bandwidth per channel, deviation/subcarrier
plan, SNR/THD+crosstalk objective and measured group delay under interference.
No “camera frame” is needed for this fourth arm. Its acceptance criteria can
favor continuous low-delay sound even when the strict PCM baseline would
reject the same result.

## PDM/sigma-delta: a different precision and bandwidth tradeoff

An oversampled one-bit stream can be low-pass filtered/decimated into audio;
it does not mean one radio bit per PCM sample. At an illustrative **64×
oversampling ratio**, 48 kHz stereo needs **6.144 Mb/s**, and eight channels
need **24.576 Mb/s**, before framing. The corresponding 24 kHz rates are
3.072 and 12.288 Mb/s. This exceeds the FLRC candidate's capacity even where
the 16-bit PCM alternative might fit. The OSR is an assumption, not a measured
quality requirement or a selected modem mode.

Manufacturer examples explicitly require decimation/filtering and expose a
delay/bandwidth tradeoff. Microchip documents 32×/64× PDM decimation. TI's
eight-channel PCMD3180 offers different filter modes; at 48 kHz its published
linear-phase group delay is 17.1 samples (calculated **356.25 µs**) versus
7.7 samples (**160.42 µs**) for its low-latency mode, with different response
specifications. These are **part-specific filter specifications**, not the
whole converter/radio/output delay or a claim that the owned boards include
that device.
[Microchip decimation example](https://onlinedocs.microchip.com/oxy/GUID-2ACDA668-0A87-46A1-B7FC-9DC74A5461AD-en-US-3/GUID-F9C56753-87F8-42CF-B2A6-B8A52FFBDFE0.html),
[TI PCMD3180 Rev A, January 2026, Tables 6-23 and 6-31](https://www.ti.com/lit/ds/symlink/pcmd3180.pdf)

A narrowly defined calculation explains the appeal of tolerating bit noise.
Map fixed-rate bits to `-1,+1`; flipping `K` bits in an `M`-bit **boxcar**
window changes its normalized average by at most `2K/M`. Independent flips
with probability `p` scale the expected signal mean by `1-2p`, plus noise.
These statements assume intact bit timing and framing, bounded flips and
that specific averaging model. They are not an audio SNR/THD guarantee.
Insertions/deletions, bursts, correlated errors, different reconstruction
filters and modulator overload invalidate a simple “small random hiss”
interpretation. RF bit errors are not automatically noise-shaped by the
source sigma-delta loop. Any custom raw-modem proposal needs bit recovery,
resynchronization, limiting and measured error spectra.

## Evidence needed before choosing an arm-4 implementation

Retain the strict 16-bit PCM reference, then test explicitly named changed
formats and loss policies. Use common clocks or a measured drift/resampling
policy, bounded queues, channel/sample indices and an analog impulse or
correlation measurement from capture input to output. Report latency tails,
deadline losses and recovery separately from perceived/recorded distortion.
Test silence, impulses, tones and representative synth audio under simultaneous
display/DSP load and controlled interference. Audio clock/sample slips,
intersample peaks and interchannel alignment matter even when ordinary packet
delivery looks good.

No current research source or payload calculation proves 1 ms multichannel
audio. The next decision should use the closed owned-peer measurements when
available, while recognizing that a reliable echo, a continuous PCM stream
and a distortion-tolerant analog/raw stream have different acceptance tests.
