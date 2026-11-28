# Offline demodulation of native snapshots

`sdr_metal_demod.py` consumes closed per-receiver metal captures, including the
single-RX child outputs of the dual harness. It never opens a serial port or
changes the firmware, protocol or raw capture. NumPy is an optional analysis
dependency; install `analysis-requirements.txt` when using this tool.

```powershell
python tools/perf/sdr_metal_demod.py `
  evidence/physical/CLOSED-DUAL-CAPTURE/rx/results.json `
  --stimulus-manifest evidence/sdr-metal-stimuli/manifest.json `
  --core .pio/sdr-model-test-metal --wsl `
  --output evidence/NEW-NATIVE-DEMOD-OUTPUT
```

The default declared core SHA256 is
`09c500232d4046ac6021dfcb8e83c998ff96846296abcc583ef62ace176b8f28`,
the separately built v3 host ELF. Its source and sanitizer/regression receipts
are pinned by `experiments/sdr/protocol-manifest-metal.json`. A separately
rebuilt executable requires an explicit `--core-sha256` matching its own receipt;
changing this argument does not assert algorithm equivalence. All outputs are
written into a new directory outside both immutable input directories.

Before creating output, the wrapper reuses the native envelope analyzer's
closed-capture and snapshot checks: original hashes, exact binary length,
CRC/LF, source ID/repetition, raw hardware count/clock/completion, canary and
owner/lane restoration. Each uploaded packed file must match the capture
manifest and declared upload SHA. That SHA selects a unique entry in the
generated-stimulus manifest; packed/reference/ideal-boxcar hashes, frame-ring
sequences and lengths are verified. Generated references remain source inputs,
never RF measurements.

Each native LE32 word is decoded as signed I10 and native −Q10. An arithmetic
right shift by two produces interleaved signed I8/−Q8, with −512 mapping to
−128 and511 mapping to127. There is no Q-sign flip, fitted DC subtraction,
resampling, concatenation across snapshots or placeholder gap insertion.
Native and converted hashes plus code/metadata ranges remain in a conversion
receipt. The core's existing `--demod-i8` interface receives that exact file and
the configured native sample clock; it does not independently calibrate it.

For paced captures, one trial uses requested baud. For unpaced captures, the
default predeclared centres are the validated whole-loop software write mean
times **1,1.004,1.008**. The multiplier list is explicitly configurable with
`--unpaced-multipliers 1 1.004 1.008`; values must be positive and finite.
Every centre, CLI command, stdout/stderr, exit status, frame file and PCM file
is retained. Unsupported sample density is labelled NOT RUN. The frozen core
searches its existing ±4000ppm neighborhood about each centre. There is no
additional adaptive clock search based on known payload values.

Selection uses the largest count of CRC-valid headers whose source rate,
channel count, oversample, payload size and stream ID match the declared
stimulus. Ties use lowest mean training-fit error, then earliest declared trial.
Payload bits, BER, confidence and decoded audio quality never enter selection.
If no matching headers exist, the selected trial is null and the result remains
NO MATCHING HEADERS. Every trial stays visible, including zero-frame trials and
valid headers that do not match the intended source configuration.

Only after selection, the existing `expected_payloads`/`compare_frames` oracles
compare accepted payloads to the uploaded ring sequence. Per-frame bit counts
and errors remain inspectable. All-known-frame totals retain repeated frames;
unique-sequence totals and quality use the first occurrence of each sequence.
Unknown sequences and configuration mismatches do not receive invented
references. Source and ideal-boxcar PCM comparisons reuse `moments`, including
per-channel error energy/RMSE, correlation, SNR availability and exact-match
flags. They cover only received known frames, with no loss concealment or
post-hoc correlation alignment. A zero error produces null SNR with an explicit
unbounded-SNR reason.

Every matching frame retains its recovered samples/symbol and derives
effective audio rate as
`(nominal_RX_rate / recovered_SPS) * payload_bits / ((112 + payload_bits) * channels * OSR)`.
That recovered rate is distinct from the header's nominal source rate. In
particular, the unpaced waveform ring can replay much faster than the reference
generation rate. No WAV is emitted; raw PCM is a digital concatenation of
received frames, without a continuous-time or live DAC claim.

Finite native apertures can crop the first and last frames. NO MATCHING HEADERS
does not identify why decoding failed or prove no RF; a short observation can
contain no complete frame at all. Search-candidate header rejects are not
independent RF/FCS errors. The core's raw sequence-gap counter assumes16-bit
monotonic sequence numbers; the uploaded0..31 ring can wrap and those raw
counters must not be treated as RF packet loss. Separate board clocks, offline
processing and recovered symbol fitting do not measure one-way latency.

Outputs include `results.json`, `decoded-summary.csv`, `trials.csv`, `frames.csv`,
`quality.csv`, each conversion, all CLI logs/PCM/frame files, selected annotated
frames and a recursive hash manifest pinning inputs, core and imported tools.
Original capture statuses remain unchanged. A failed analysis preserves its
partial outputs and reports FAILED; it does not turn raw failures into complete
captures. Hardware-free fixtures check conversion, declared selection policy,
known-source errors/duplicates, immutability and orchestration:

```powershell
python -m unittest discover -s tools/perf -p test_sdr_metal_demod.py
```
