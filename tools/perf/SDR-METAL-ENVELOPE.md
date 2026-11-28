# Native SDR snapshot envelope analysis

`sdr_metal_envelope.py` is an offline companion to the finite SDR-metal capture
and native-IQ analyzer. It analyzes actual closed per-receiver capture results,
including the same per-RX format produced by a dual-receiver harness. Run it
separately for each receiver. Separate boards, bursts, clock settings, gain
settings and capture/bench modes are not pooled into a fictional continuous
stream or calibrated board comparison. The tool opens no serial port.

```text
python tools/perf/sdr_metal_envelope.py evidence/physical/CLOSED-METAL-CAPTURE/results.json --output evidence/metal-envelope-01 --write-spectra --write-edges
```

Use a new output directory outside the immutable capture directory. NumPy is
required, as for the existing native-IQ analyzer. The tool records its version.
It imports only the existing pure `decode_words` function from
`sdr_metal_analyze.py`, without changing that decoder or the capture harness.

## Input integrity and retained scope

The input must have schema `cyd-sdr-metal-capture/1`, evidence class
`physical_snapshot_acquisition` and a closed COMPLETE/FAILED/INTERRUPTED status.
Active or unknown states are rejected. The tool verifies the results file and
every artifact listed in `artifact-hashes.json`, then independently checks each
ACQUIRED snapshot's raw SHA256, receipt SHA256, CRC32, byte count, native format,
source ID/repetition, requested count/rate, hardware completion status, canary
and register-restoration proof. Paths cannot escape the capture directory.
Validation occurs before creating analysis output. Failed runs remain listed
as incomplete; their partial payloads are not silently promoted to measurements.

Each snapshot has a fresh local sample index beginning at zero. A no-export
bench can report twenty completed captures but retain only the twentieth raw
buffer. The analyzer analyzes that one buffer, not twenty invented waveforms.
Its results preserve the capture's original status and exact image/source
identities. This does not change a raw receipt's RF-verification field.

## Native power and modulation spectrum

No 8-bit requantization occurs. Each little-endian word is decoded into native
signed 10-bit fields, with upper 12-bit metadata preserved separately. The
pinned upstream convention calls the two fields I and −Q. Envelope power is
`I² + Q²`, so its value is independent of that Q sign. Means, quantiles, variance
and field-limit fractions use those native codes. Code² is not dBm, calibrated
RF amplitude, sensitivity or an analog noise-floor measurement.

The modulation spectrum is the **AC spectrum of power**, not the complex
carrier spectrum: subtract the snapshot's mean power, apply a symmetric Hann
window, compute the one-sided real FFT, and exclude its DC bin from peak
selection. Output includes peak-bin frequency, bin spacing `nominal_rate/N`,
Hann equivalent noise bandwidth, peak fraction of non-DC windowed AC power and
PSD in code⁴ per nominal Hz. Bin spacing and window bandwidth are not calibrated
frequency accuracy. No sub-bin interpolation adds unsupported precision.

A DC-valued carrier with constant power has no modulation peak. A rotating
constant-amplitude carrier can have small quantization ripple, which does not
pass the default power-contrast gate. Carrier plus IQ DC offset or multiple
interfering carriers can create real envelope beating without a keyed source.
The fixture checks one such sinusoidal beat; its lack of two-level separation
prevents qualified OOK threshold edges. This is not a universal classifier.
Strong periodic power alone never proves the intended transmitter's origin.

The tool deliberately does not subtract the mean I/Q vector before computing
power. For a near-DC, fixed-phase OOK carrier, that would turn symmetric low/high
levels into nearly equal bipolar magnitudes and could erase the modulation.
Any separate matched transmitter-off DC correction would need its own declared
receiver/gain/time scope; it is not silently fitted from the keyed snapshot.

## Explicit contrast, occupancy and threshold-edge gates

Power q10/q90, their normalized contrast `(q90-q10)/(q90+q10)` and mean power are
retained. The ratio divides by `max(q10,1 code²)` so zero-valued codes do not
create JSON infinity. That one-code arithmetic floor is not an RF-noise claim.
Low/high groups split at the q10/q90 midpoint. Their median separation is
divided by the larger scaled median absolute deviation (`1.4826*MAD`), floored
at one code². This is a robust heuristic in power-code space, **not RF SNR**.

Default qualification requires all of:

* At least 32 samples and three cycles of the selected AC peak in the snapshot.
* q90/q10-with-floor ratio ≥4 and robust level separation ≥4.
* AC spectral-peak concentration ≥0.1 of non-DC windowed AC power.
* Both hysteretic states occupying at least 5% of the snapshot.
* At least three candidate crossings of each polarity.

Hysteresis thresholds are q10 + 0.35 and 0.65 of the q10→q90 span. The initial
mid-band state is unclassified until one threshold is reached. The tool exports
descriptive midpoint/high-state occupancy even when qualification fails, with
that scope explicit. It exports qualified duty/edge-interval distributions only
after the gates pass, preserving every rejection reason. Duty is snapshot
threshold occupancy, not transmitter RF duty or RF on-time.

Qualified high/low widths use only pairs of observed threshold crossings, so
the unknown first and last censored widths are excluded. Rising-to-rising and
falling-to-falling periods are separate distributions. Results include exact
sample intervals plus nominal-clock microseconds and count/min/median/p95/max.
These are threshold crossings with one-sample granularity, not calibrated RF
edges or a radio latency distribution. No despiking or invented gap samples
alter the input. Ringing or threshold chatter remain visible in the intervals.

Threshold controls are explicit CLI options:

```text
--minimum-power-ratio 4
--minimum-level-separation 4
--minimum-ac-peak-fraction 0.1
--minimum-cycles 3
--minimum-state-fraction 0.05
--hysteresis-low-fraction 0.35
--hysteresis-high-fraction 0.65
```

Changing them creates a different analysis context recorded in the result.
Their defaults deliberately target separated OOK-like levels; a weak ASK pair
or very low duty waveform may not qualify. Rejection is not proof of absent RF.

## Alternating-pattern references and aliases

Only a declared TX-enabled alternating pattern receives frequency references:
positive requested baud/2 and software writes/2 per second derived from the TX
write count, reported CPU clock and elapsed cycle count. Unpaced mode has no
requested-baud reference. Each reference is folded into the nominal RX Nyquist
interval, with alias-to-DC and expected cycles within the finite snapshot shown.
The nearest FFT bin and distance from the independently selected AC peak are
reported; the reference does not choose that peak.

The software average spans the whole TX loop and is not synchronized to a local
RX snapshot. Skipped source slots break uniform alternation; interrupt-mask
segments/gaps can make the local rate differ from the whole-loop average. An
odd repeated pattern length adds a repeated level at its wrap. All receive
explicit warnings. Even a matching aliased frequency is not unique evidence
of the original unaliased rate or source identity.

At maximum length, the RX observation lasts only 1,023.75/409.5/204.75 nominal
microseconds at 16/40/80 MS/s. A 1 ksymbol/s alternating source has a 500 Hz
fundamental, so less than one full cycle fits even at 16 MS/s. Such a snapshot
cannot establish its steady periodicity. This is a hardware observation-horizon
limit, independent of deferred USB throughput.

## Outputs and validation

`results.json` contains source provenance, settings, complete statistics,
qualification reasons and reference caveats. `envelope-summary.csv` is one row
per actual retained snapshot. Optional `--write-spectra` exports the entire
mean-removed power spectrum; `--write-edges` exports only qualified threshold
crossings, with an empty table when unqualified. `manifest.json` pins input,
artifact-manifest, analyzer, imported native decoder and output hashes.

Run the independent deterministic fixtures:

```text
python -m unittest discover -s tools/perf -p test_sdr_metal_envelope.py -v
```

Fixtures cover signed fields/metadata, constant DC power, rotating carrier,
carrier/DC beating, known OOK frequency/duty/widths across fractional source
phases, noise, a single step, alias-to-DC/unpaced/skipped-slot contexts, invalid
thresholds and hash/CRC/count/canary/source failures. CLI fixtures use temporary
synthetic directories explicitly named `FIXTURE_NOT_RF`; they do not publish
physical evidence. Existing native decoder/capture files remain unchanged.

This analyzer does not decode payloads, measure BER from a calibration pattern,
reconstruct audio or measure synchronized one-way latency. Future framed
demodulation can consume the retained native `decode_words` I10/Q10 arrays
directly, keeping any Q-sign conversion explicit rather than downconverting to
the earlier IQ8 batch interface.
