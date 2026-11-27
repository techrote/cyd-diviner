# Audio MVP evidence tools

The matching [firmware method](AUDIO_MVP.md) defines buffering, deadlines,
reconstruction and measurement limits; this document covers host execution
and evidence handling.

These tools keep three experiments separate: finite PCM16 packet streaming over
ESP-NOW broadcast, finite packetized PDM streaming over ESP-NOW broadcast, and an
offline continuous raw-PDM impairment model. Neither PDM experiment implements
the separately requested greenfield proprietary SDR transmitter/receiver.
Packetized PDM still has application
framing/CRC and packet loss. The offline model deliberately bypasses packet CRC;
it is not a measurement of a continuous RF bitstream.

The two physical endpoints must be identified and flashed with owner approval
outside this harness. Use the existing `cyd-perf-link-identities/1` manifest with
`cyd` and `peer` entries, exact source commit/tree digests, profiles, confirmed COM
ports, firmware paths and SHA256 digests. The harness checks both local binaries
before opening either port, then checks target source/profile identity. Preserve
separate verified flash receipts; the target does not attest the binary hash.
The harness never resets, flashes, scans, or selects an unowned peer.

```powershell
python tools/perf/audio_mvp_capture.py plan --output evidence/plan-audio-mvp.json
python tools/perf/audio_mvp_capture.py capture `
  --identities C:/private/audio-identities.json `
  --plan evidence/plan-audio-mvp.json --output evidence/physical/audio-mvp-01
python tools/perf/audio_mvp_analyze.py evidence/physical/audio-mvp-01/results.json
```

The default matrix has three repetitions of eight PCM16 configurations, with a
three-second finite source. The default sender is `peer` (S3), receiver `cyd`.
PCM16 covers 24 kHz stereo at 8/16/32 frames per block and a short 48 kHz stereo
case. `--include-pdm` adds optional packetized-PDM comparisons covering OSR16/32
and one/two channels. Both request 1 and
6 Mbps ESP-NOW PHY settings. Add `--reverse` when generating a plan to append
the opposite direction. Rate requests and actual firmware configuration results
remain inspectable; a requested PHY rate is not measured RF capacity.

For every repetition the receiver is configured and activated before the sender.
Only UART-identified peer MACs and a fresh private session authorization are
allowed. Authorization TX and transaction RX are omitted from logs; generated
auth values are also registered for streaming redaction of public serial data.
No hotspot credential is needed. Public run IDs correlate both summaries.

The finite streaming interval contains no host queries. Native USB progress
recovery can issue at most three spaced idempotent `perf audio info` queries,
only after a matching public metadata/export record has begun and stopped making
progress. It never pokes an empty pending buffer, a private transaction, or a
streaming interval. Explicit identity `usb_progress_poll: true` permits this for
the initial identity record; native-USB target metadata enables later recovery.

Outputs retain source/image identity, receiver-first setup receipts, both raw
redacted RX streams, target JSONL, serial event logs, finite completion evidence,
queue/deadline/loss/PLC counters, and available digital reconstruction metrics.
Failed or incomplete sessions remain failed and preserve partial evidence.
Digital reconstruction statistics concern the synthetic source and receiver
algorithm, not microphone, speaker, DAC or analog fidelity. The two board clocks
are not synchronized; no one-way RF latency is inferred from them or UART timing.

The continuous-PDM model calls the portable C++ core executable directly:

```powershell
python tools/perf/audio_mvp_simulate.py --wsl --core .pio/perf-audio-core-test `
  --output evidence/audio-model-osr16 --rate 24000 --channels 2 `
  --block-frames 16 --osr 16 --frames 48000 --seed 23063 `
  --cases clean:0 flip:1000 burst:128 stuck:128 erase:128
```

Build the ELF with `g++ -std=c++17 -O1 -g -Wall -Wextra -Werror
-DGRIM_PERF_AUDIO_CORE_STANDALONE=1 -I src src/perf_audio_core.cpp
tools/perf/test_audio_core.cpp -o .pio/perf-audio-core-test` inside WSL.
The core CLI is `--simulate RATE CH BLOCK_FRAMES OSR TOTAL_PCM_FRAMES SEED
IMPAIRMENT STRENGTH REF_PATH OUT_PATH`. `flip` strength is seeded parts per
million; `burst`, `stuck` and `erase` strength is segment length in bits. The
core reports actual positions and changed/lost bit counts. Erasure represents
known missing positions with the held preceding bit per channel; framing and
bit clock positions remain known. It does not model insertion/deletion or
synchronization loss.

Model results are labelled `OFFLINE_MODEL` and include raw reference/reconstructed
PCM16LE, separate WAVs, a comparison WAV, core receipts, digital quality JSON/CSV
and input/tool/output hashes. Comparison WAV channels are reference channels
first, then reconstructed channels. Unaligned error/correlation remain visible
beside a bounded offline filter-alignment comparison. Positive alignment lag
means the decoded samples follow the reference. That alignment is not transport
latency, and a deterministic specimen does not characterize RF loss statistics.
