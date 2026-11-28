# Convergence qualification — Draft/HOLD

This is an implementation candidate for owner evaluation. **No merge or
physical hardware flash has occurred.** The exact qualified source is recorded
by each CI artifact's manifest and report. Historical component receipts are
retained as development evidence and are not relabeled as final firmware tests.

## Current evidence classification

| Gate | Development result | Final exact-head CI |
| --- | --- | --- |
| CYD firmware | PASS build; 2,038,749-byte ELF flash accounting before final cleanup | Pending workflow execution; no final PASS claimed here |
| Cardputer firmware | BLOCKED locally by dependency extraction space; no source verdict | Pending workflow execution |
| CYD diagnostics / PCM | NOT RUN final matrix | Pending workflow execution |
| Native / native-CYD | PASS builds and actual application specimens | Pending frozen rerun |
| Numerical DSP at 24/48 kHz | PASS | Pending frozen rerun |
| Actual five instrument kernels and allocation failures | PASS | Pending frozen rerun |
| ASan + UBSan | PASS with local leak detection disabled | Pending independent CI with leaks enabled |
| ThreadSanitizer | PASS for new actual instrument kernels, exchanges and radio controller | Pending frozen rerun |
| LeakSanitizer | BLOCKED by development ptrace runtime | Pending independent CI |
| Existing synthesis output | PASS: 1,692,128 samples exactly equal to prior DSP | Pending frozen rerun |
| Touch/persistence | PASS: 247,600 pixel mappings, actual controller, eight-process rendered tour | Pending frozen rerun |
| Packaging/provenance | PASS: 28 independent fixtures | Pending frozen rerun |
| Actual VLC/CYD1 host protocol | PASS: 2,128 single-bit faults, 549 chunk sizes, CRC/sequence/recovery and host failures | Pending frozen rerun |
| Physical board, radio, acoustic continuity, touch feel, USB/VLC end-to-end | NOT RUN | Owner evaluation required |

The final PR report must link the actual run and its terminal job results.
This page intentionally distinguishes already executed component work from
the pending final-source matrix; it is not a promise that a scheduled test passed.

## Methods and measured development evidence

The actual new renderers run against a minimal graphics/radio test HAL. The
DSP suite exercises 250,000 coherent snapshot publications, 250,000 ordered
events, same-length Reader updates, 15,000 concurrent UI/render iterations,
300 instrument lifecycles and three allocation failures. The radio suite adds
24,992 protocol assertions, 10,000 concurrent UI/render iterations, 300
lifecycles, two allocation failures, startup failure and bounded command
overflow. Both observed zero C++ allocations from the callback thread in their
executed workloads. This does not prove every C-library path allocation-free.

The numerical suite measures sine interpolation error (4.73e-6 in the tested
sweep), 440 Hz source accuracy (439.999999478 Hz measured), finite SVF sweeps,
envelope timing, deterministic output, score bounds and Delay16 quantization
(maximum 6.10e-5, below 1/16384). At 24 kHz a nominal 3,050 Hz formant measured
approximately 3,260 Hz: timbral approximation is documented, not called exact.

Host component timings use 128 samples at 24 kHz, a **5,333.333 µs** budget.
Development high-density CANON p99 was 21.982 µs (0.412% of that host block
budget); GLYPHSONG choir 3.765 µs (0.071%); an eight-voice SPECTRA Wi-Fi specimen
19.138 µs (0.359%); COVEN follower/speech 4.086 µs (0.077%). These are desktop
observations, exclude physical display/radio/I/O scheduling, vary with host
load and establish **no ESP32 real-time headroom**. Final raw host logs retain
median/p95/p99/worst per specimen; no arbitrary hardware threshold is inferred.

The actual native application separately records callback histograms and
frame-cycle timing. Histogram percentiles are power-of-two upper bounds;
maximum values are exact observations. Native frame cycles include intentional
33 ms pacing. CYD diagnostics measure callback and lock wait for its actual
128-sample block; Cardputer/native use 256 samples and a 10,666.667 µs deadline.
API write errors and callback over-budget counts are not acoustic underrun counts.

The deterministic CV1 model uses the same parser/session as firmware. Two and
three clean nodes had zero modeled steady phase error. Eleven nodes with 20%
loss, ±300 ppm clocks, 1–80 ms delay and reordered duplicates produced median
44 ms, p95 57 ms, maximum 66 ms. Six-node partition recovery took 1,080 ms;
leader removal recovery took 14,050 ms, consistent with the 15-second lease.
Unrelated-traffic saturation caused 3,615 deliberately injected queue drops
while preserving bounded storage. These are musical pulse simulations, not
sample synchronization or physical radio results. [Full method](RADIO.md).

## Remaining owner gates

Execute the [physical procedure](OWNER-EVALUATION.md) with the exact manifests:
all five instruments and original CYD visual/palette features, 100 transitions
with heap/largest-block recovery, demanding audio plus graphics, active Wi-Fi/
BLE observation and mode switches, PCM CRC/sequence/continuity and VLC playback,
and CYD/CYD plus CYD/Cardputer musical circles. Auxiliary S3/C3 node firmware is
not supplied. CV1 unauthenticated replay/hidden-node limits remain explicit.

The newer instrument TSan workload does not make older rites or third-party
SDK/SDL code race-free. Listen to the deliberately corrected vibrato/LFO cadence,
evaluate small resistive-touch targets, and verify standalone partition fit
before any owner-approved installation. Maintain Draft/HOLD until those
receipts and explicit merge authorization exist.
