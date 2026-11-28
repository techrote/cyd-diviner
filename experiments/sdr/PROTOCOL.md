# Greenfield raw-radio protocol prototype

This independent implementation is an experimental transmitter stimulus and
**host/batch IQ demodulator**, not evidence of radiated data, on-device
real-time decoding, continuous analog audio output or measured RF latency.
No implementation was copied from the reviewed no-license ESP32SDR project.
The PHY/tone driver and physical receiver build remain separate components.

## Wire definition

Frames contain 32 alternating NRZ training symbols (0 first), sync word
`0xD391C5A7` sent MSB first, six header bytes sent MSB first, then 128 or 256
raw payload bits. Payload bit zero is the LSB of its byte. Header layout:

| Bytes | Meaning |
| --- | --- |
| 0–1 | Sequence, unsigned little-endian 16-bit, modulo 65,536 |
| 2 | Stream ID, unsigned byte; an experiment filter, not authentication |
| 3–4 | Configuration, little-endian 16-bit |
| 5 | CRC8 of header bytes 0–4; polynomial `0x07`, init 0, no final xor |

Configuration bits 0–1 select 1/2/4 channels (codes 0/1/2); bits 2–3 select
OSR 4/8/16/32; bits 4–7 select source rates 250, 500, 1,000, 2,000, 4,000,
8,000, 16,000, 24,000 or 48,000 Hz (codes 0–8); bit 8 selects 256 rather
than 128 payload bits. Bits 9–15 must be zero. Invalid configurations and
bad header CRCs are rejected. Payload CRC/FEC and ARQ are deliberately absent:
decoded payload bit errors are accepted and available for reconstruction.

The PDM source has continuous independent first-order sigma-delta error state
per channel. Its bit order is source sample frame, oversampling tick, then
channel. The source uses deterministic integer-phase triangles, peaks of
24,000 PCM LSB and channel frequencies `30 + 17*channel` Hz. Frame boundaries
do not reset encoder state. Reconstruction counts ones over each OSR-sized
subchannel interval, maps the bipolar mean to PCM16 and clamps +32,768 to
+32,767. Low OSR deliberately produces coarse, distorted output.

## Actual capacity and timing interpretation

Total length is **240 symbols with 128 payload bits** or **368 symbols with
256 payload bits**. At 256 bits, framing adds 43.75% to the payload symbol
count. Sustaining a source sample rate requires:

```text
NRZ baud = source_rate * channels * OSR * frame_symbols / payload_bits
effective audio rate = actual baud * payload_bits / (frame_symbols*channels*OSR)
```

For example, 2 kHz/two-channel/OSR4 and 4 kHz/mono/OSR4 each need 23,000
symbols/s with 256-bit payloads. The revised host receiver accepts at least four
IQ samples per symbol; 125 kS/s therefore permits at most 31,250 symbols/s
under this input guard. The exact 125,000/23,000 = 5.43478 samples/symbol
case has a separate synthetic validation cohort. This guard is not a claim
that arbitrary RF filtering, timing jitter or SNR will decode at that rate. Advertising a high source-rate code does not make
the radio or USB carry it: `--emit` reports both configured source rate and
effective audio rate at the selected baud.

Headers interrupt payload transmission. A continuous audio output would need
its own sample clock, buffering and a policy for missing frames; those are
not supplied by the batch demodulator. Its output is concatenated received
frames, with sequence gaps recorded separately. None of these arithmetic
rates or source timestamps is a sample-to-analog latency measurement.

## APIs and bounded host receiver

The firmware-facing API in `protocol.h` uses no heap: `PdmSource::payload`
fills up to 32 bytes and optionally up to 64 interleaved reference samples;
`buildFrame` fills up to 368 individual byte-valued 0/1 symbols. Compile
`protocol.cpp` with `SDR_PROTOCOL_TRANSMIT_ONLY=1` to exclude the host receiver.

The host receiver computes OOK envelope power or a two-FSK phase-difference
score. It searches sample phase and a bounded grid of symbol periods, learns
threshold/contrast from the alternating training sequence, permits at most
one sync-bit error, then checks header CRC/configuration and stream ID. It
refines timing using training/sync/header fit, never known payload or payload
CRC. All payload bits are then emitted, with confidence diagnostics.

The current search is per-frame batch clock recovery, not a streaming PLL.
Input is capped at ten million IQ pairs, period-search grids at 129 points,
and nominal samples/symbol at 4–1,024. Fractional sample-window integration
and quarter-sample phase refinement avoid the whole-sample timing quantization
that produced payload errors when the old guard alone was relaxed. CRC-reject counts are **overlapping search
candidates**, not independent received packets or an RF error rate. Sequence
gaps and duplicate decoded sequences are separate counters. Unknown headers,
lost bit synchronization and false sync must remain inspectable.

The synthetic channel applies complex AWGN, carrier offset, transmitter clock
drift and a contiguous payload-symbol inversion. Its passband is ideal: OOK
carrier-offset success tests envelope invariance, not hardware RF selectivity
or PLL-lock tolerance. IQ is quantized to signed eight-bit I/Q before decoding.
The tests also corrupt a header and verify later resynchronization, accept
payload errors, and check that deterministic noise-only input yields no frame.

## Build and commands

Host build, from the repository:

```text
g++ -std=c++17 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer experiments/sdr/protocol.cpp experiments/sdr/test_protocol.cpp -o .pio/sdr-model-test
```

With no arguments the executable runs correctness/model tests and emits JSON
classified `OFFLINE_MODEL`. On Windows this ELF can be invoked through
`wsl.exe -e /mnt/c/cyd-diviner/perf-characterisation/.pio/sdr-model-test`.

Generate a finite transmitter stimulus:

```text
sdr-model-test --emit PREFIX SOURCE_RATE CH OSR PAYLOAD_BITS BAUD FRAME_COUNT SEED STREAM_ID
sdr-model-test --emit /tmp/tx 2000 2 4 256 23000 64 23063 1
```

This writes `PREFIX-symbols.bits`, `PREFIX-reference.pcm` and
`PREFIX-boxcar.pcm`. **The `.bits` file packs NRZ symbols MSB-first in each
byte**, not one byte per symbol. Extract symbol `k` as
`(bytes[k/8] >> (7-k%8)) & 1`. Frames have byte-aligned lengths, so no padding
symbols are inserted. JSON class `GENERATED_STIMULUS` records bit/byte counts,
source configuration, required/effective rates and frame count. The reference
and boxcar files are interleaved signed PCM16 little endian.

The emitter accepts positive integer baud 1–240,000,000, matching the experimental
probe CPU-clock metadata bound. This replaces the earlier 100 kbaud host CLI
cap. Requested baud changes rate metadata, not packed symbols, reference PCM or
boxcar PCM. The declared source rate and the effective audio rate at a requested
transport baud remain separate; neither acceptance nor generated bytes proves
that a radio or receiver can sustain that rate. The separate
`test_emit_cli.py --binary PATH --output NEWDIR` regression checks bounds,
required/effective rate arithmetic and byte independence, including 1.104 Mbaud.

Generate an impaired IQ model:

```text
sdr-model-test --model PREFIX SNR_DB CFO_HZ CLOCK_PPM BURST_BITS ook|fsk
sdr-model-test --model /tmp/model 20 5000 2000 16 ook
```

This writes `PREFIX-iq.i8`, `PREFIX-reference.pcm` and `PREFIX-decoded.pcm`
for eight source frames. Model quality compares the full known source;
unreceived frames are explicitly zero-filled for this comparison. Raw payload
BER is conditional on received, sequence-matched frames; missing frames are
reported separately. These are computed model results, not RF measurements.

Demodulate an IQ file:

```text
sdr-model-test --demod-i8 IQ_PATH SAMPLE_RATE BAUD OUT_PCM FRAME_JSONL STREAM_ID
sdr-model-test --demod-i8 /tmp/model-iq.i8 250000 23000 /tmp/decoded.pcm /tmp/frames.jsonl 1
```

Input is interleaved signed eight-bit I/Q, two bytes per pair. Output JSON
schema is `greenfield_sdr_demod/1`, class `HOST_DEMODULATION`, with
`rf_origin_verified=false`. The caller must associate actual capture origin,
USB CRC/sample gaps and RF setup. Per-frame JSONL schema
`greenfield_sdr_frame/1` includes sequence/configuration, payload hex, recovered
period, IQ sample offset and confidence. Output PCM concatenates received
frames and does not conceal missing sequence numbers. No frames or a changed
configuration returns exit status 3 while preserving diagnostic JSON/artifacts;
invalid arguments/I/O return 2. The receiving host can create WAV files using
the declared rate/channels, while preserving any mismatch with effective
transport rate and any known capture gaps.

`protocol-manifest.json` and `host-validation.json` retain the original
eight-sample receiver evidence. `protocol-manifest-low-sps.json`,
`host-validation-low-sps.json` and `host-regression-low-sps.json` identify the
revised host receiver and its separate validation cohort. The original
`.pio/sdr-model-test` executable is retained; build the revised receiver as
`.pio/sdr-model-test-low-sps` and run `--low-sps-tests` for its additional cohort.

The new 43-case cohort includes sixteen independent fractional start phases at
125 kS/s and 23 ksymbol/s, AWGN at 20 dB configured carrier SNR with +3,500 and
−3,500 ppm transmitter clock offsets across eight phases each, accepted payload
inversions, a rejected header followed by resynchronization, combined
impairments and eight exact four-sample phase cases. Noise-only input and the
below-four-sample guard are checked. These are ideal-passband, quantized-IQ
model tests; they do not establish RF latency, filter tolerance or a guaranteed
physical bit error rate. A byte-for-byte comparison of eight emitted frames,
reference PCM and boxcar PCM against the retained original executable verifies
that this receiver change does not alter the example transmitter stimulus.
