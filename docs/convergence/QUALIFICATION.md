# Convergence qualification — Draft/HOLD

This is an implementation candidate for owner evaluation. **No merge or physical
hardware flash has occurred.** Every final artifact identifies its exact clean
source SHA, tree and content snapshot. [PR #8](https://github.com/techrote/cyd-diviner/pull/8)
links the final combined-source CI run and artifact matrix. A passing component
run is not silently relabeled as a final application or physical hardware pass.

## Recorded independent qualification and corrections

The first complete matrix tested source
`8d74b4b68883c1691ad0e8d75d923385695af78e`, tree
`a0aa3b2c9b8d72e98764e3664ba2253022363c51`, content snapshot
`2a05aaae65158c343c82ce5374afa16cf7d7e01978639331175a772d5739d56e`.

[Run 46112001450](https://github.com/techrote/cyd-diviner/actions/runs/46112001450)
completed **nine PASS, one FAIL**. The failure was investigated rather than
waived. Subsequent focused correction runs are identified separately below.

| Gate at first exact source | Classification | Evidence or remaining distinction |
| --- | --- | --- |
| `cyd`, `cyd-diagnostics`, `cyd-pcm` | PASS | Real firmware builds and complete clean-source packages |
| `cardputer` | FAIL | Current S3 toolchain rejected the unconditional all-address lock-free assertion |
| `native`, `native-cyd` CI application tours | PASS | Both applications built and executed |
| Independent local `native` tour | PASS | All five specimens plus twenty rites entered/left three times |
| Independent local `native-cyd` tour on slower host | FAIL | Delayed scripted taps collapsed; GLYPHSONG retained keyboard instead of singing |
| Portable normal qualification | PASS | Actual instrument kernels, DSP, radio, callback lifetime, legacy PCM, packaging, VLC protocol |
| ASan + UBSan + LeakSanitizer | PASS | CI enabled `detect_leaks=1`; local ptrace restriction was an earlier BLOCKED gate |
| ThreadSanitizer | PASS | New actual instrument renderers, coherent exchanges, radio controller and callback lifetime |
| Windows VLC DLL build | PASS | Actual plugin compiled and packaged |
| Physical boards, audio continuity, touch feel, radio and USB/VLC end-to-end | NOT RUN | Requires owner evaluation of the final exact source |

The Cardputer probe used the actual compiler and installed SDK archives:
[run 46113050508](https://github.com/techrote/cyd-diviner/actions/runs/46113050508).
Xtensa GCC `esp-14.2.0_20260121` reported only sometimes-lock-free 32-bit atomics.
The configured S3 PSRAM workaround dispatches external-memory atomics through
a critical section. `CONFIG_SPIRAM_USE_MALLOC=1` with a 4,096-byte internal
preference means ordinary 12–21 KB state allocations cannot be assumed internal.
The actual internal compare/exchange helper has one S32C1I instruction; the
exchange helper has a retry loop. The correction therefore requires explicit
internal state storage and bounded strong-CAS ownership transfer, not deletion
of the safety assertion. See [DSP contract](DSP-ENGINEERING.md).

The scripted-touch correction preserves declared inter-event intervals after
actual dispatch and settles a known released contact before the next down edge.
A deterministic 600 ms dispatch-stall fixture reproduces the former failure and
tests the real controller's Hide/Mode behavior plus timer rollover. Existing
rendered-state assertions remain unchanged.
[Corrected run 46113077668](https://github.com/techrote/cyd-diviner/actions/runs/46113077668)
at `dcb793572cbcec2d3395980cf6fbcac46d696392` **PASS**: native-CYD build, normal
and ASan/UBSan controller tests and all eight actual application processes.
[Rendered specimens and identity](previews/README.md) were reviewed independently.

[Independent firmware audit run 46113058607](https://github.com/techrote/cyd-diviner/actions/runs/46113058607)
**PASS** for all three actual first-source CYD bundles, deliberate byte-corruption
rejection, five dependency-metadata fixtures and 28 packaging fixtures. The
auditor shares no packager/provenance implementation. Final firmware jobs repeat
that independent audit on their own bytes, including Cardputer.

## Final combined-source gates

Before accepting the candidate, require terminal PASS for four firmware profiles,
two actual simulator profiles, normal/ASan+UBSan+LSan/TSan portable suites and the
Windows plugin. The linked PR checks and attached reports provide the exact final
revision and results; this page records the evidence and corrections preceding
that freeze. Temporary isolated investigation workflows are not integrated.

The final runner also rejects absent or malformed required reports, stale
receipts and child failures mislabeled PASS. Seven classification fixtures
exercise these guards using the real runner and independent fake children.
The bounded SPSC scheduling model explores 544 and 1,880 states, including 132
first-CAS collisions in the larger case, without a second-CAS failure or lost
final publication. This is abstract ownership evidence, not a C++ weak-memory
proof; actual target code-generation and sanitizer checks remain separate.

Firmware manifests record actual partition capacities, resolved dependencies,
app/full checksums and source identity. Independent `audit.json` reports
image-header/segment checks, partition MD5 and bounds, full-image component
placement, embedded source identity, source reconstruction and static ELF DRAM.
Simulator artifacts retain source-identified binaries, screenshots, WAVs and
asserted state reports. Raw logs upload on failure as well as success: artifact
existence alone does not imply PASS.

## DSP, concurrency and regression evidence

The executed portable workloads include 250,000 coherent snapshot publications,
250,000 ordered events, rapid same-length Reader updates, 25,000 concurrent
UI/render iterations across the five instruments, 600 lifecycle repetitions,
five state-allocation failures and radio-start failure. Actual callback-lifetime
tests include 200,000 pending-writer admission checks and 20,000 allocate,
publish and detach cycles per qualification mode. All first-source normal and
sanitized runs passed. Zero C++ callback allocations were observed in these
workloads; this does not prove arbitrary C-library paths allocation-free.

The legacy comparator checked **1,692,128 PCM samples in 71 cases** against the
prior DSP for SEQ, BEAT, ORACLE and Visions. Every tested sample was identical.
Seventeen rendered mono PCM16 WAV specimens total 95 seconds / 2,280,000 samples
at 24 kHz. All were non-silent and none hit the configured ±30,000 output boundary
or int16 rail. Recordings are reproducible computational specimens, not subjective
listening approval or a recording of physical speakers.

Numerical tests at 24 and 48 kHz cover oscillator interpolation/phase modulation,
SVF and formant stability, pitch, envelopes, delay wrap/quantization, deterministic
vox/score output, clipping/non-finite handling and 10,000 fuzzed scores per rate.
At 24 kHz, sine interpolation maximum error was 4.73e-6; a 440 Hz source measured
439.999999478 Hz; one-second envelope-decay error was 1.88e-5. Delay16 maximum
error was 6.10e-5, below 1/16384. A nominal 3,050 Hz formant peaked near 3,260 Hz:
the SVF envelope is a documented timbral approximation, not an exact phonetic
model. Tolerances and complete 24/48 kHz observations remain in test logs.

## Host performance and memory evidence

First-source CI host measurements used GCC 13.3 on x86-64 and 128 samples at
24 kHz, a **5,333.333 µs** block budget. These are observed computational timings,
not ESP32 measurements or asserted future thresholds.

| Actual renderer workload | Median µs | p99 µs | Worst observed µs |
| --- | ---: | ---: | ---: |
| Dense CANON | 9.644 | 16.294 | 28.212 |
| Six DRONE styles | 6.479–6.570 | 6.650–9.144 | 24.687 |
| Four GLYPHSONG modes | 1.452–1.973 | 2.473–3.835 | 14.381 |
| Four SPECTRA scenarios | 2.254–5.688 | 5.608–8.122 | 49.583 |
| Two COVEN scenarios | 2.904–2.914 | 2.945–5.438 | 22.423 |

The largest tabulated p99 is 0.306% of that host block budget; the largest observed
sample is 0.930%. Physical display/radio/I/O scheduling is excluded. These figures
establish **no embedded real-time headroom**. Final exact-source raw logs retain
their own median/p95/p99/worst observations; do not substitute these older values.

First-source audited app sizes were 2,045,696 B (CYD), 2,049,152 B (diagnostics),
and 2,046,880 B (PCM), each within a 3,145,728 B app partition. Corresponding ELF
static DRAM was 77,088 / 77,448 / 77,168 B. These are historical qualification
receipts; **final image sizes and hashes are the final artifact manifests**.
They exclude dynamic heap, stacks, radio SDK allocations and fragmentation.

Native application callback histograms and frame-cycle records are separate from
kernel microbenchmarks. Histogram percentiles are power-of-two upper bounds;
maxima are exact observations. Native frame cycles include intentional 33 ms
pacing. CYD diagnostics measure its actual 128-sample callback and lock wait;
Cardputer/native use 256 samples and a 10,666.667 µs deadline. Callback over-budget
counts and API write errors are not acoustic underrun measurements.

## Distributed timing evidence

The deterministic CV1 model uses the production parser/session and performs
24,992 assertions. Clean two-/three-node scenarios had zero modeled steady
phase error. Eleven nodes with 20% loss, ±300 ppm clocks, 1–80 ms delays and
reordered duplicates gave median 44 ms, p95 57 ms, maximum 66 ms. A six-node
partition recovered in 1,080 ms; leader removal recovered in 14,050 ms under the
15-second lease. Heavy unrelated traffic produced 3,615 deliberately injected
queue drops with high-water occupancy 15 and bounded storage.

These are musical pulse simulations, not sample synchronization or physical
radio results. CV1 remains unauthenticated, one-hop and subject to documented
replay/hidden-node limitations. [Full method and constraints](RADIO.md).

## Remaining owner gates

Execute the [physical procedure](OWNER-EVALUATION.md) with the exact final
manifests: all five instruments and original CYD visual/palette features, 100
transitions with heap/largest-block recovery, demanding audio plus graphics,
active Wi-Fi/BLE observation and mode switches, PCM CRC/sequence/continuity and
VLC playback, and CYD/CYD plus CYD/Cardputer musical circles. Auxiliary S3/C3
firmware is not supplied.

The newer-instrument TSan workload does not certify older rites or third-party
SDK/SDL code race-free. Listen to the corrected vibrato/LFO cadence, evaluate
resistive-touch targets, inspect internal-memory headroom and verify partition
fit before separately authorized installation. Maintain **Draft/HOLD** until
physical receipts and explicit owner merge authorization exist.
