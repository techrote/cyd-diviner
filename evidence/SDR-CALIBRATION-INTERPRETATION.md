# Independent SDR calibration interpretation

This read-only review finds a repeated OOK-compatible received envelope in the
adequate-passband RAM captures, including the centered 23,000-symbol/s case.
The earlier uncentered 125 kS/s low-amplitude result is **passband-limited and
inconclusive**, not evidence that the fast transmitter API failed to radiate.
This supports proceeding to a known framed stimulus; alternating calibration
alone does not establish payload delivery, audio quality or latency.

The interpretation below is separate from immutable capture receipts. Their
`rf_signal_verified` fields are not modified. This is offline numerical
analysis of actual received IQ, not a generated channel model. Raw source and
artifact hashes, fixed windows, sample counts and computed results are retained
in [SDR-CALIBRATION-INTERPRETATION.json](SDR-CALIBRATION-INTERPRETATION.json).

## Immutable raw archive and subsequent framed proof

The [raw SDR archive](PHYSICAL-CAPTURES-SDR.zip) retains all ten closed SDR
capture directories, including failed/partial/zero-run attempts, with original
`physical/...` paths. It is **4,207,015 bytes**, SHA256
`12a32067cfa50f957e6d691ed7fa62b704c706e8839b19ba205f0ff8e79aeb0c`.
The [archive receipt](sdr-archive-receipt.json) records **259 verified artifact
manifest entries and 269 verified archived files**, exact ZIP membership,
passing ZIP CRC checks, decompressed-file SHA256 checks and unchanged raw files
after archiving. Extract its `physical/` prefix into `evidence/` before replay.
Analysis outputs outside `physical/` are separate from that raw snapshot.

The later [finite framed SDR proof](SDR-PROOF.md) records actual proprietary
headers, sequence coverage, compared payload errors and digital reconstruction
quality. It does not change this calibration's raw statuses or turn its symbol
periods into one-way latency measurements. The framed proof explicitly preserves
missing sequence 0, mono startup payload errors and the incomplete acquisition.

## Method and sample continuity

The independent [review script](../tools/perf/sdr_calibration_review.py) reads
the retained native signed8 indexed I/Q, accepted-frame CSV and erasure JSON.
It hashes each input against its closed capture's artifact manifest, requires
the frame-table validity mask and erasure mask to agree, checks the retained
valid-sample count, and excludes every host-inserted placeholder. Real received
samples whose quantized I and Q are both zero remain valid samples.

All comparison rows use the same declared **0.30–0.60 s** received-IQ interval
and **1.0–1.5 s** baseline, in receiver sample coordinates. These windows are
chosen from the finite 500 ms calibration bursts; they are not synchronized
transmitter timestamps. Power is `I²+Q²`, in arbitrary signed8 code². The
expected alternating-envelope fundamental is `baud/2`; ordinary least squares
fits an intercept plus sine/cosine at that frequency. R² measures explanatory
power in that fixed window, not RF packet success or bit-error rate. Widths are
medians of thresholded complete high runs whose boundaries are both measured;
they exclude runs censored by a missing IQ interval. Quantization/filtering can
shorten these apparent widths.

The original 250 kS/s API cohort is closed **FAILED** because the 10 ksymbol/s
API call aborted after 10 bits. Its earlier 1 and 5 ksymbol/s acquisitions are
retained. Both original and later 250 kS/s cohorts still lose roughly half the
IQ output sample positions through target queue delivery drops. Their valid
samples can establish coherent received cadence, but their missing positions
must never be reported as measured RF silence or isolated RF loss. The 125 kS/s
cohorts, including centered RAM, have zero retained sample gaps, zero target
queue drops and zero IQ frame CRC errors. No clipping was observed in the
valid I/Q pairs in these six cohorts.

| Closed capture / run | TX backend / symbols/s | IQ kS/s | Valid / excluded IQ samples | Steady / baseline mean code² | R² at baud/2 | Complete high median µs |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| sdr-calibration-01 / s001_1 | api / 1,000 | 250 | 255,009 / 245,248 | 1232.7541 / 0.11155 | 0.74544 | 764.0 |
| sdr-calibration-01 / s002_1 | api / 5,000 | 250 | 254,495 / 245,760 | 53.6597 / 0.08152 | 0.04191 | 16.0 |
| sdr-calibration-01 / s003_1 | api / 10,000 | 250 | 254,496 / 245,760 | 0.0702 / 0.10528 | 0.00001 | 8.0 |
| sdr-ram-125k-01 / s001_1 | ram / 1,000 | 125 | 250,128 / 0 | 0.3986 / 0.04405 | 0.00266 | 16.0 |
| sdr-ram-125k-01 / s002_1 | ram / 5,000 | 125 | 250,128 / 0 | 1.3747 / 0.05168 | 0.00217 | 16.0 |
| sdr-ram-125k-01 / s003_1 | ram / 10,000 | 125 | 250,125 / 0 | 2.6910 / 0.05310 | 0.00463 | 16.0 |
| sdr-ram-125k-01 / s004_1 | ram / 20,000 | 125 | 250,126 / 0 | 5.2959 / 0.22091 | 0.00881 | 8.0 |
| sdr-ram-250k-v2-01 / s001_1 | ram / 1,000 | 250 | 254,976 / 245,248 | 66.7136 / 0.06625 | 0.80035 | 996.0 |
| sdr-ram-250k-v2-01 / s002_1 | ram / 5,000 | 250 | 255,008 / 245,248 | 71.5525 / 0.09448 | 0.80086 | 200.0 |
| sdr-ram-250k-v2-01 / s003_1 | ram / 10,000 | 250 | 254,497 / 245,760 | 69.0638 / 0.04876 | 0.81608 | 100.0 |
| sdr-ram-250k-v2-01 / s004_1 | ram / 20,000 | 250 | 254,497 / 245,760 | 90.9775 / 0.11173 | 0.82733 | 48.0 |
| sdr-offset-plus80-01 / s001_1 | api / 1,000 | 125 | 250,126 / 0 | 2.1666 / 0.09310 | 0.01572 | 8.0 |
| sdr-offset-minus80-01 / s001_1 | api / 1,000 | 125 | 250,127 / 0 | 1119.1231 / 0.07518 | 0.74557 | 760.0 |
| sdr-ram-centered-01 / s001_1 | ram / 1,000 | 125 | 250,127 / 0 | 71.8398 / 0.04642 | 0.80855 | 1000.0 |
| sdr-ram-centered-01 / s002_1 | ram / 5,000 | 125 | 250,127 / 0 | 62.8905 / 0.01648 | 0.80388 | 192.0 |
| sdr-ram-centered-01 / s003_1 | ram / 10,000 | 125 | 250,128 / 0 | 74.3423 / 0.02270 | 0.82556 | 96.0 |
| sdr-ram-centered-01 / s004_1 | ram / 20,000 | 125 | 250,126 / 0 | 73.0430 / 0.01984 | 0.83810 | 48.0 |
| sdr-ram-centered-01 / s005_1 | ram / 23,000 | 125 | 250,127 / 0 | 62.2094 / 0.05490 | 0.83436 | 40.0 |

These 300 ms fixed-window R² values intentionally differ from the primary
signal analyzer's strongest 50 ms-window values. Neither method substitutes
placeholder zeros for received data. The independent script also retains the
`baud` and `2*baud` power-harmonic fits, so short edge transients are inspectable
rather than automatically interpreted as sustained high symbols.

## Why the LO-offset control changed the interpretation

The initial API 1 ksymbol/s capture has 14,259 adjacent valid high-power pairs
in the steady window with both samples above 500 code². Their weighted complex
phase increment gives **+82,630.29 Hz** in the wire-I/Q convention, with
**0.99680** circular coherence. This component lies outside the ±62.5 kHz
Nyquist interval of 125 kS/s, and the upstream decimator's passband further
limits what reaches that output. Its absence after reducing the output rate
cannot diagnose whether the transmitter was switching. A phase estimate depends
on the power selection and on pulse transients; the JSON retains both this
fixed threshold and a separately declared percentile threshold. No offset is
presented as an absolute calibrated RF-frequency measurement.

The receiver wire convention is I,−Q. Rather than infer the sign of an RF
retune from that convention, the owner ran matched LO controls: **FOFS −80**
restored the API 1 ksymbol/s alternating envelope (R² **0.74557** in the fixed
window), whereas **FOFS +80** remained weak/distorted (R² **0.01572**). These are
empirical receiver-command effects with the same approved images and nominal
settings; the exact internal LO, oscillator error and RF offset are not
calibrated by this experiment.

With the wider 250 kS/s output, RAM's requested 1/5/10/20 ksymbol/s alternation
has strong expected half-baud components in actual valid IQ (fixed-window
R² **0.80035/0.80086/0.81608/0.82733**). With the selected −80 offset and gap-free
125 kS/s output, RAM's 1/5/10/20/**23** ksymbol/s cases give
**0.80855/0.80388/0.82556/0.83810/0.83436**. The consistency across requested
cadences and the controlled passband change support a received OOK waveform
from the tested transmitter. Call execution times alone would not establish
this.

At 23 ksymbol/s, the requested symbol interval is **43.478 µs** and the
alternating fundamental is **11.5 kHz**. The complete measured high-run median
is **40 µs**, quantized by 8 µs output-sample spacing and affected by filtering
and the declared threshold. This is consistent with that cadence. It does not
measure every RF edge, prove a bounded symbol-jitter distribution, establish a
continuous 23 kbit/s payload service or measure end-to-end audio latency.

## Limits and next evidence

The calibration pattern deliberately has no framed sync word or header, so a
framed decoder reporting `no_frames` is expected and is not a failed OOK
calibration. A framed run must independently establish the known stream ID,
CRC-valid headers, sequence progression, exact payload comparisons and capture
continuity. A header CRC does not protect payload bits in this experimental
protocol. Received payload errors are intentionally accepted and need measured
error and audio-quality results.

The old host decoder required eight nominal IQ samples per symbol. The revised
host-only decoder accepts at least four using fractional window integration
and quarter-sample phase refinement. Its exact **125,000/23,000 = 5.43478** case
is covered by a separate 43-case synthetic cohort and the original regression
suite; see [protocol manifest](../experiments/sdr/protocol-manifest-low-sps.json).
This is an algorithm validation, separate from the physical calibration above.
The retained original executable and earlier model evidence remain unchanged.

The finite transmitter timings, receiver sample indices and independent board
clocks do not establish synchronized one-way latency. No analog audio output,
RF spectrum-mask compliance, calibrated sensitivity or continuous SDR
transceiver service has been measured by these calibration runs.

Reproduce this review without opening a serial port:

```text
python tools/perf/sdr_calibration_review.py --captures evidence/physical/sdr-calibration-01 evidence/physical/sdr-ram-125k-01 evidence/physical/sdr-ram-250k-v2-01 evidence/physical/sdr-offset-plus80-01 evidence/physical/sdr-offset-minus80-01 evidence/physical/sdr-ram-centered-01 --output evidence/SDR-CALIBRATION-INTERPRETATION.json
```
