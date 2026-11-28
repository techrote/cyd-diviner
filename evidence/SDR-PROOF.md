# Physical greenfield SDR proof: finite multichannel PDM delivery

**Actual carrier keying on the owned classic ESP32 CYD and IQ reception on the
owned ESP32-S3 recovered this original experimental NRZ/TDM-PDM protocol.** At
23,000 symbols/s, a 2 kHz stereo, OSR4 source recovered **15 of 16 frames in each
of three runs**, with **zero errors in 3,840 compared payload bits per run**.
Sequence 0 was not recovered in those runs. This is a finite physical proof
with host/batch decoding, not continuous on-board audio playback or a measured
low-latency audio link.

This protocol is original/proprietary experimental framing carried by direct
OOK carrier keying and raw IQ reception. **It is not PDM over ESP-NOW.** Its
independent source, waveform generation and decoder are in
[experiments/sdr](../experiments/sdr/PROTOCOL.md). The S3 receiver backend is the
separately built, GPL-3.0 ESPARGOS/esp-sdr implementation with the retained
adapter; this is not a claim that the complete receiver stack was written from
scratch. No implementation from the reviewed no-license ESP32SDR project was
copied.

## Measured runs and incomplete attempts

The [JSON overview](SDR-PROOF.json) and [CSV overview](SDR-PROOF.csv) consolidate
13 ACQUIRED runs and one incomplete acquisition from three **closed** analyses.
All three use decoder SHA256
`d99ab63acddc7f578db97a263b32f15fc0141fd6ab10963104be6ab325e80312`.
The underlying capture and analysis files remain unchanged.

| Closed analysis / scope | Source rate / channels / OSR | Symbols/s | Acquired runs | Unique frames / generated | Compared payload bits | Payload errors / conditional BER |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| [Low-rate v2](sdr-framed-low-analysis-v2/results.json), mono | 1,000 Hz / 1 / 4 | 5,750 | 3 | 47 / 48 | 12,032 | 202 / 1.67886% |
| Low-rate v2, stereo | 1,000 Hz / 2 / 4 | 11,500 | 3 | 45 / 48 | 11,520 | 0 / 0 observed |
| Low-rate v2, four-channel | 500 Hz / 4 / 4 | 11,500 | 1 | 15 / 16 | 3,840 | 0 / 0 observed |
| [Four-channel retry](sdr-framed-four-analysis/results.json) | 500 Hz / 4 / 4 | 11,500 | 3 | 45 / 48 | 11,520 | 0 / 0 observed |
| [23 ksymbol/s](sdr-framed-23k-analysis/results.json) | 2,000 Hz / 2 / 4 | 23,000 | 3 | 45 / 48 | 11,520 | 0 / 0 observed |

The low-rate source capture retains status **FAILED**: seven acquisitions
completed, followed by an incomplete second four-channel acquisition (`s008_2`)
with a recorded JSON parse error. The latter was skipped rather than decoded
as a valid transmission. The separately completed four-channel retry supplies
three valid repetitions; its earlier zero-run retry failure is also retained
in the raw archive. The low-rate analysis therefore does not represent a
completed original matrix. The earlier v1 failed analysis is retained outside
`physical/`; v2 is a separate analysis, not an overwrite.

The first two mono runs each decoded all 16 headers but had **101 errors out of
4,096 payload bits (2.46582%)**, all in sequence 0. Their later sequences matched
the generated payload. The third mono run and every other acquired run decoded
sequences **1–15**, with zero compared payload errors and missing sequence 0.
The exact cause of sequence-0 loss/corruption is not assigned by these results.

**Conditional payload BER and frame coverage are different results.** A zero
observed BER applies only to the recovered, source-matched payload; it does not
count a missing frame as correct, establish a zero underlying error probability
or guarantee future traffic. The 23 ksymbol/s source-frame coverage is 93.75%
in each run. The decoder's internal `sequence_gaps=0` counts gaps between
observed frames, so it cannot reveal an absent leading sequence. The overview
explicitly compares against all 16 generated source sequences. Header CRC
reject counts represent overlapping search candidates, not independent RF
packets or a packet-error rate.

All 13 acquired framed runs have zero retained IQ sample gaps, zero IQ frame
CRC errors and zero target IQ queue drops. Known source matching and quality
comparisons therefore do not touch host-inserted erasure samples. This clean
framed subset is distinct from earlier 250 kS/s calibration captures with
substantial IQ delivery gaps; those failures remain in the archive.

## Wire, source and reconstruction

Each finite transmission contains 16 frames of 368 symbols: 32 alternating
training symbols, a 32-bit sync word, a six-byte header with CRC8, and 256 raw
payload bits. Payload CRC, FEC and retransmission are deliberately absent.
Header validation and known-source comparison are separate: a valid header
permits a corrupted payload to be retained and measured. Framing adds 43.75%
to the payload symbol count.

The precomputed source consists of deterministic integer-phase triangle waves
at **30, 47, 64 and 81 Hz** for channels 0–3. Independent first-order sigma-delta
states continue across payload frames. TDM order is source sample frame,
OSR tick, channel. The host reconstructs each subchannel with a simple
OSR-sized boxcar. At OSR4 this has only five bipolar output levels, including
clamped positive full scale; coarse distortion is expected and measured.
These source rates are **500–2,000 Hz per channel**, not 24/48 kHz audio.

Quality is evaluated on the source samples belonging to received sequences;
missing frames are excluded from that domain and reported separately. The
retained WAV files concatenate received frames without concealment, silence
insertion or a real-time playout schedule. Two distinct comparisons are kept:

* **Decoded PCM versus original source PCM** includes intentional PDM/boxcar
  distortion plus additional transport and host-demodulation errors.
* **Decoded PCM versus ideal boxcar reconstruction of the same source bits**
  measures additional transport-plus-host-demodulation reconstruction error.
  It does not isolate RF impairments from decoder behavior.

For the 23 ksymbol/s stereo runs, each channel has 480 received source samples.
Source-comparison RMS errors are **6,426.53 and 6,972.30 PCM16 LSB**, with
calculated source/error SNRs **6.744 and 6.033 dB** and correlations **0.9075 and
0.8955**. Both channels match their ideal boxcar reconstruction exactly on the
received domain: additional RMS error is zero. That leaves substantial codec
distortion; a zero extra error is not lossless PCM or high-fidelity audio.

The four-channel retry likewise matches ideal boxcar output exactly; its
source/error SNRs are **6.423, 8.765, 7.131 and 7.575 dB**. In the two corrupted
mono runs, additional RMS errors versus ideal boxcar are **2,941.21 and 3,114.37
LSB**. All per-channel metrics, domains and sample counts are in the overview
JSON/CSV and original `rf-quality.csv` files. A zero-error comparison records
SNR as null with an explicit zero-error reason rather than a fabricated finite
dB value. These digital source/error ratios are not measured RF SNR.

## Timing and scope

At the nominal 23 ksymbol/s setting, a symbol lasts **43.478 µs**. A complete
368-symbol frame occupies **16 ms** and contains 32 stereo sample frames at
2 kHz. At 11.5 ksymbol/s the nominal symbol and frame periods are **86.957 µs**
and **32 ms**; at 5.75 ksymbol/s they are **173.913 µs** and **64 ms**. These are
arithmetic serialization periods, not one-way latency measurements.

The source was generated offline and transmitted as a finite preloaded bit
buffer. The receiver delivered IQ to a PC, which decoded it in batch and wrote
PCM/WAV. The experiment has no receiver DAC/I2S output, continuous on-device
clock recovery, live audio input, synchronized source/receiver timebase,
measured sample-to-analog latency, range sweep or calibrated RF power/noise
measurement. It demonstrates a working finite physical proprietary signal
path at these tested low rates. It does not yet establish the requested
continuous multichannel low-latency SDR audio system or DSP request/result
turnaround.

## Exact identities and reproducibility

| Component | Source identity | Application SHA256 |
| --- | --- | --- |
| Classic CYD TX, MAC `a4f00f5fff54`, profile `classic-sdr-tx-probe` | `bb34b3c27c4c8388956bb05a8a5280d7a2c687a2`; source-content digest `b9585839a9ee69d79f18799a640a6bd23679a2878966f3379304b0b1030b05ee` | `0623e6cccefd6717da560c5913f344b818ce43adf38c47b9e698785f560d2bc0` |
| S3 N4R2 RX, MAC `1c2904bec3a0`, profile `esp32s3` | `3ab3b65bb62f2d9427ee33c2c3b627427e729294-cyd-iq-fdf42772e506`; build `2026-11-27T21:17:17Z` | `b81bee95f5bd0e0393538c0183a9898856474fc9b6ddb16daaf889d3e3acd949` |

The [authorized stage-1 flash receipt](prototype-stage1-flash-receipt.json)
records both exact app images and verified flashes. The
[image/source bundle receipt](prototype-bundle-receipt.json) identifies the
archive containing the reviewed images and exact adapted GPL receiver source;
its historical `UNAPPROVED-...` filename does not replace that later explicit
hardware authorization. TX's experimental RAM ABI is pinned to ESP-IDF 5.5.5,
commit `b774170ff46c393eeb5e495ea37936038d3f4f4f`. Receiver RF setup for these
framed cohorts is nominal FREQ 2433, FOFS −80, manual gain index 40, 16 MS/s
input with decimation 128, IQS8 and output 125 kS/s. TX requests channel 6 and
backoff 96 quarter-dB units. These are configured settings, not calibrated
carrier power or absolute RF-frequency measurements.

The [low-SPS code/model manifest](../experiments/sdr/protocol-manifest-low-sps.json)
pins host source, decoder binary and **1,846 new plus 52,896 regression
ASAN/UBSAN checks**. The new receiver's fractional-window integration and
quarter-sample phase refinement support the exact 125,000/23,000 sampling ratio
in synthetic tests. Wire framing and PDM source output are unchanged; retained
original and revised executables emit byte-identical comparison stimuli. The
[original manifest](../experiments/sdr/protocol-manifest.json) and model results
remain historical evidence. Synthetic channel tests and physical results
above remain distinct evidence classes.

Raw inputs are preserved in [PHYSICAL-CAPTURES-SDR.zip](PHYSICAL-CAPTURES-SDR.zip)
(**4,207,015 bytes**, SHA256
`12a32067cfa50f957e6d691ed7fa62b704c706e8839b19ba205f0ff8e79aeb0c`).
The [archive receipt](sdr-archive-receipt.json) verifies all ten closed SDR raw
capture directories, including failed/partial/zero-run attempts: 269 files and
259 artifact-manifest entries. The three framed source paths are
`physical/sdr-framed-centered-low-01/`,
`physical/sdr-framed-four-retry-02/` and `physical/sdr-framed-23k-01/`. Extract
while retaining the `physical/` prefix. Analysis folders are separate from
this raw archive. Their manifests pin source capture hashes, decoder/driver
hashes and every derived artifact:

* [Low-rate v2 manifest](sdr-framed-low-analysis-v2/manifest.json).
* [Four-channel manifest](sdr-framed-four-analysis/manifest.json).
* [23 ksymbol/s manifest](sdr-framed-23k-analysis/manifest.json).

This overview was generated only after verifying all three analysis manifests'
input/output hashes and the complete raw archive hash. Its JSON pins the
analysis, archive-receipt and code/model-manifest hashes. No capture, earlier
analysis, protocol source, firmware or tool was modified to produce it.
