# Existing instrument DSP regression

The shared DSP changes preserve the existing SEQ, BEAT, ORACLE and Visions audio
paths for the tested valid controls. `tests/dsp_legacy_regression.cpp` compiles
the actual four instrument source files twice in one process: once against the
current `src/dsp.h`, and once against the CYD DSP implementation at
`7b1b37eec6ac126f334a0b008d91135071ea60fa`. The attributed reference in
`tests/dsp_legacy_reference.h` changes only its include path and namespace.
This avoids interpreting changed instrument logic as a DSP regression.

## Executed comparisons

| Existing instrument | Scenario | Compared 24 kHz samples |
| --- | --- | ---: |
| SEQ | Default groove and dense, accented, sliding pattern at 240 BPM | 384,000 |
| BEAT | All nine original programs at 8, 11.025, 16 and 22.05 kHz bytebeat rates | 864,000 |
| ORACLE | All 26 rune identities across three overlapping chime voices | 156,000 |
| Visions | Six original tonal settings and sound disabled | 288,128 |
| **Total** | **Exact PCM comparison, zero differing samples** | **1,692,128** |

**PASS:** optimized native comparison and AddressSanitizer plus
UndefinedBehaviorSanitizer comparison. All non-muted scenarios produced
non-silent audio. All tested internal filter states and delay samples remained
finite. No specimen reached the 30,000 PCM clipping limit. The test records
each scenario's RMS, peak and PCM FNV-1a fingerprint for comparison within the
same compiler/platform; cross-platform floating-point bit identity is not
claimed. The current and historical scope contents also match after rendering.

The sine lookup table and representative oscillator increments compare exactly.
All four instruments use positive frequencies below the sample rate, valid
filter resonance and bounded delay taps in these scenarios, so the new invalid
input guards do not change their synthesis. Visions already updates its slow
oscillator on every sample; it correctly retains ordinary `sine()` calls.
The application now calls `dsp::prepare()` before `hal::begin()` so initial
lookup-table construction occurs before callback publication.

## Resource and qualification limits

The atomic scope stores 256 32-bit samples instead of 256 16-bit samples.
On the tested native compiler `sizeof(dsp::Scope)` is 1,028 bytes compared with
516 bytes for the historical scope: **512 additional bytes per scope**. SEQ and
BEAT each retain one scope. SEQ's CYD state is static; BEAT's state remains an
allocation retained between visits. Their rolling scope display is race-free
at the scalar level but does not promise one coherent captured waveform.

This test changes controls between callback invocations and does not establish
that legacy SEQ/BEAT/ORACLE/Visions UI parameter transfers are free of data races.
Their existing `volatile` fields and program handoff remain outside the new
instrument ownership changes. Graphics are replaced by a no-op seam, so the
comparison does not qualify display output, physical I/O or ESP32 deadlines.
Local sanitizer execution disables LeakSanitizer because this container's
ptrace restriction prevents its operation; ASan and UBSan still execute.

## Reproduction

The comparison is part of the normal portable gate:

```sh
python3 tests/dsp_run.py --output-dir /tmp/diviner-dsp-regression
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 python3 tests/dsp_run.py --sanitizer address --output-dir /tmp/diviner-dsp-regression-asan
```

Each compile and executable has a default 180-second timeout, adjustable with
`--timeout`. An overrun is recorded as **FAIL**, so a stalled concurrency test
cannot leave the qualification process waiting indefinitely. On a host where
LeakSanitizer works, leave `ASAN_OPTIONS` unset to retain its default check.
