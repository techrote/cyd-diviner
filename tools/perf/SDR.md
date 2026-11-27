# Finite greenfield SDR experiment host

This backend uploads a finite packed symbol stream to the original classic-ESP32
TX probe, starts the separately identified S3 continuous-IQ receiver first, and
records both serial links. It does not flash either board or enable RF on startup.
Run capture only after the owner has approved the exact images and RF experiment;
the command requires the operator's explicit `--rf-authorized` flag.

```powershell
python tools/perf/sdr_capture.py plan --output evidence/plan-sdr-calibration.json
python tools/perf/sdr_capture.py capture --rf-authorized `
  --identities C:/private/sdr-identities.json --plan evidence/plan-sdr-calibration.json `
  --output evidence/physical/sdr-calibration-01
```

Identity schema `cyd-sdr-identities/1` has `tx` and `rx` entries. Each needs the
owner-confirmed `port`, `baud`, twelve-hex-digit `mac`, `firmware_file` and exact
`firmware_sha256`. TX additionally needs `expected_info` with its exact `profile`
(`classic-sdr-tx-probe`), `source_sha`, `tx_source_digest` and `mac`. RX needs exact
`expected_version` fields (`revision`, `profile`, and optionally the recorded
build date/timestamp). Images are checked before ports open. The upstream RX
protocol has no runtime MAC or binary-digest attestation; preserve its owner's
preflash identity and verified flash receipts alongside the exact runtime VERSION.

The default plan contains eight alternating0101 calibration cases: 1/5/10/20 kbaud
using the API and experimental RAM keying paths, with requested backoff96 quarter
dB and half-second symbol streams. This is a requested SDK backoff, not calibrated
radiated power. Receiver defaults request 16 MS/s input, decimation64, native
eight-bit IQ and mode2, tuned to2433 MHz for the TX's requested2437 MHz carrier.
All tuning, gain, shift, decimation and mode commands remain explicit in the plan.
The upstream IQS start field is the input ring rate; output rate is input divided
by decimation. Fixed manual gain index40 is requested for every default case and
all `GAIN?` mode/index/limit/register-flag fields are retained. A gain index is not
calibrated dB. A different setting requires an independently labelled plan after
inspecting physical clipping/separation and receiver limits.

For framed PDM experiments, replace a calibration workload with `bits_file` and
an explicit `bit_count`, or `symbols_file` containing one byte0/1 per symbol.
The latter is packed MSB-first before upload. All source/upload hashes and exact
uploaded bytes are retained. The TX `load` command ends in LF only, followed by
exactly the packed bytes; the host does not issue per-bit timing commands. TX
parameters remain within the firmware's finite two-second software-requested
bound. A fresh one-use permit is private, redacted, never persisted in control
logs, and consumed by each TX command. There is no automatic retransmission.

RX begins and delivers valid IQ before the TX command. Both serial streams are
drained during the finite run. IQS1 frames retain CRC32, sequence, output sample
index, flags, gain metadata and shift. IQSEND's twelve fields establish the end
boundary. A missing IQSEND fails acquisition and closes the receiver without
sending ordinary commands into an uncertain binary stream. Reconnection/recovery
is an owner-controlled action.

Outputs contain raw serial RX/event logs, the original IQ stream, indexed native
`I,-Q` signed-eight-bit IQ, frame CSV, integrity JSON, TX call timing/trace records,
temperature indications when provided, and input/output/source hashes. Known
between-frame IQ gaps receive zero-valued host placeholders only in the indexed
file, with exact separate erasure records. Those zeros are not measured RF
silence. Initial/final unknown samples are not invented. The original stream is
always retained for inspection. Decoder integrity can also be reproduced offline:

```powershell
python tools/perf/sdr_capture.py decode --raw evidence/physical/sdr-calibration-01/s001_1/iq-stream.bin `
  --output evidence/sdr-decode-check --i8
```

`ACQUIRED` means that finite TX commands and continuous-IQ acquisition completed.
It does not assert that the requested RF levels radiated, a proprietary frame
decoded, or audio reconstructed successfully. TX edge timestamps are SDK call
completion cycles, not independent RF edges. Deadline/late aborts, partial bit
processing and nonzero Wi-Fi stop/deinit results fail acquisition. Receiver IQ
observations and the separate host demodulator must establish any RF/frame claim.
Independent board clocks and USB timestamps do not establish end-to-end latency.
This initial receiver is host/batch demodulation, not an onboard real-time DAC.

Generate the short framed plan and reproducible source artifacts before deciding
which calibrated switching modes/rates can execute it:

```powershell
python tools/perf/sdr_analyze.py emit-plan --wsl --core .pio/sdr-model-test `
  --output evidence/sdr-framed-stimuli --frames 16 --plan-name plan-sdr-framed.json
```

This calls the portable `--emit` CLI. The generated stimuli are labelled
`GENERATED_STIMULUS`, with reference/boxcar PCM/WAV and packed symbols. The plan
contains the requested2 kHz/two-channel/OSR4 stream at23,000 baud, plus1 kHz mono
at5,750 baud,1 kHz stereo at11,500 baud and500 Hz/four-channel at11,500 baud.
Both API and RAM variants are prepared; independent calibration determines which
ones may proceed. None of these generated artifacts is RF measurement evidence.

After a closed physical capture, batch demodulation preserves acquisition status,
observed proprietary headers, source-known payload comparison, and raw IQ gaps as
separate fields:

```powershell
python tools/perf/sdr_analyze.py analyze evidence/physical/sdr-framed-01/results.json `
  --wsl --core .pio/sdr-model-test --stimulus-root evidence --output evidence/sdr-framed-analysis
```

The wrapper checks immutable capture artifact hashes and refuses offline models
or an open capture as physical evidence. It writes decode JSON/CSV, original and
erasure-annotated frame JSONL, received-frame PCM/WAV, and known-source digital
quality where reference hashes match. Payload errors remain accepted and visible;
frames intersecting host IQ erasures are identified, with separate comparison
counts outside erasures. The waveform concatenates received frames; it does not
silently synthesize missing audio or assert continuous real-time playback. Empty
demodulation is `NO FRAMES`, not a diagnosis of RF failure. Header CRC rejection
counts are search candidates, not over-air packet/FCS error rates.

Alternating calibration has no proprietary headers. Analyze its actual envelope
instead of treating an expected frame-decoder `NO FRAMES` result as RF failure:

```powershell
python -m pip install -r tools/perf/analysis-requirements.txt
python tools/perf/sdr_signal_analyze.py evidence/physical/sdr-calibration-01/results.json `
  --output evidence/sdr-signals-calibration-01
```

The signal analyzer reads CRC-valid IQS frames directly and never uses host
erasure placeholders. It reports receiver-code baseline/peak contrast, clipping,
valid sample exposure, and power-envelope sinusoid fits at alternating baud/2.
Exact output sample indices retain timing across gaps. Maximum-window fits and
the narrow frequency scan are descriptive selections, not formal detection
significance or packet/audio decode proof. Zero quantized baseline leaves its
power ratio unavailable. Narrower decimated IQ filters can remove an off-center
carrier; a weak filtered cohort cannot alone establish a TX failure.

The S3 RX implementation is an external GPL-3.0 backend. Preserve its pinned
revision, preparation patch, SDK/build receipt, license and corresponding source
separately from the original framing/TX/demodulation experiment files.
