# Instrument audio ownership and qualification

**Disposition: component qualification PASS; physical audio performance NOT RUN.**
The component receipts in [DSP-evidence](DSP-evidence/) describe source
`25329fe60d9d993d4f7f9c6098c46b3257edc189`, before final application integration.
The final application report and CI artifacts supersede these receipts for
firmware identity. Source provenance is the preserved upstream merge of
[`c1b1175bcf15072bc2538ccdde6806d5901b16e9`](https://github.com/numbpill3d/cardputer-adv-diviner/commit/c1b1175bcf15072bc2538ccdde6806d5901b16e9)
onto CYD main `7b1b37eec6ac126f334a0b008d91135071ea60fa`.

## Changes that affect sound or correctness

CANON, DRONE and GLYPHSONG now separate UI-authoring state from audio-owned
oscillators, envelopes, filters, delays and sequencer cursors. A bounded
`audio_exchange::Latest<T>` publication transfers a complete control revision
at the beginning of each callback. A separate exchange transfers coherent
telemetry back at the end. Drawing never traverses the audio-owned voices.
Callbacks allocate no memory and contain no synchronization wait loops.

The exchange has three slots: one producer-owned, one consumer-owned and one
atomic middle slot. Publication and acquisition exchange ownership with
acquire/release ordering. A producer cannot overwrite the consumer's currently
read slot. This fixes the previous `vox::Reader` and GLYPHSONG score handoff:
retracting a length or comparing its unchanged value did not stop a producer
writing the exact buffer the consumer was copying. Repeated equal-length
messages are specifically stress-tested.

`Reader::post()` and `stop()` are UI-producer operations. All other Reader
configuration and runtime access belongs to the audio thread, or to a period
when its callback is detached. GLYPHSONG submits text, score, mode, pitch and
pace together; a newer text revision cannot become paired with an older score.
Intermediate UI revisions are intentionally coalesced. Musical events that
must all occur should instead use the bounded `SpscQueue<T,N>` and explicitly
handle its drop-new overflow result.

Upstream's sound families remain: four differently phased FM voices in CANON;
Temple, Tubular, Bowl, Kalimba, Gong and Ice in DRONE; and speak, chant, choir
and syllabic sing in GLYPHSONG. Two control-rate errors were corrected:

- Vox vibrato set to 5.2 Hz had advanced its oscillator once per 16 audio
  samples, producing approximately 0.325 Hz. `sineStrided(16)` restores Hz units.
- DRONE's 0.045/0.013 Hz filter LFOs had similarly run 64 times too slowly.
  Their phases now advance by the elapsed sample count. This is an intentional
  audible departure from the faulty cadence, not a bit-identical optimization.

DRONE musical timers now compare signed differences across the 32-bit sample
counter rollover. `dsp::Scope` uses lock-free-width atomic samples for legacy
viewers; its display remains a rolling observation, not a coherent block capture.
The new instruments' other telemetry is coherent. The sine table has safe
single initialization and the instrument entry paths prewarm it before audio
publication. All three leave paths detach audio before freeing state.

The portable numerical primitives reject non-finite frequency/phase inputs,
clamp invalid SVF controls, handle arbitrary delay tap wrap, silence NaN PCM,
and protect vowel indexes. The score composer bounds input to 40 characters,
output to 48 notes, and annotated degrees to the supported two-octave range.
The engine remains a small speech-like musical synthesizer; it is not a
natural-language speech engine or UTAU-compatible implementation.

## Direct manipulation

| Instrument | Touch action in the original 240 × 135 logical canvas |
| --- | --- |
| CANON | Tap an outlined phrase cell and drag vertically to alter pitch. Notes/Rest edit the selected cell; Erase removes it. Hold a cell for 500 ms to make a rest. Touch the reels to realign and deselect. Tempo and spread meters accept continuous horizontal gestures. |
| DRONE | Drag the artwork horizontally for drift and vertically for brightness. The dock provides style, scale, register, wind, bells, hold and reseed. |
| GLYPHSONG | Compose via the common keyboard, including Ctrl+left/right caret movement, insertion and backspace. Tap the text field to reveal it. The right panel controls pitch vertically; the pace meter controls speed horizontally. Enter performs the line; mode changes publish a complete score revision. |

Keyboard operation remains available on Cardputer and desktop. Touch is a UI
producer of the same musical parameters; it never mutates oscillator/filter
state directly. Audio telemetry carries a playback revision so the piano roll
cannot highlight a stale score after a new line is submitted.

## Executed qualification

Reproduce with a C++17 compiler and Python 3; no graphics package or board is
needed for this component harness:

```sh
python3 tests/dsp_run.py --output-dir /tmp/diviner-dsp --audio
python3 tests/dsp_run.py --sanitizer address --output-dir /tmp/diviner-asan
python3 tests/dsp_run.py --sanitizer thread --output-dir /tmp/diviner-tsan
```

The instrument test compiles the actual application source files. Its graphics
operations and callback-lifetime seam are substituted, so it does not qualify
the real platform HAL, display, audio driver, scheduling or physical output.
The application/native/firmware matrix must cover those separately.

| Qualification | Result and evidence |
| --- | --- |
| 24 kHz and 48 kHz primitive suites | **PASS**. Sine interpolation, phase/pitch precision, envelope timing, SVF stability, measured formant envelope, float/16-bit delay wrap and quantization, finite output, deterministic vox, annotation handling and 10,000 score fuzz strings at each rate. |
| Bounded communication | **PASS**. 250,000 latest-value messages checked for monotonic coherent receipt; 250,000 ordered queue events; rapid same-length actual Reader publication while synthesizing; explicit full-queue and stop checks. |
| Actual instrument code | **PASS**. Eleven non-silent WAV specimens; CANON phase/hold/alignment/phrase assertions; all six DRONE styles; all four GLYPHSONG modes; caret editing; three allocation failures; 15,000 concurrent UI/draw iterations against audio; 300 lifecycle repetitions. |
| Audio allocation observation | **PASS**. Zero C++ allocations observed on the render callback thread during the executed workloads. This is not a proof for arbitrary future paths or C-library allocation. |
| ASan + UBSan | **PASS**, with leak detection disabled in this local ptrace environment. The original LeakSanitizer run was **BLOCKED** by its runtime's inability to inspect threads under ptrace. CI defaults to leak checking; see its final result separately. |
| ThreadSanitizer | **PASS** for all four component test cases, including the real three-instrument concurrent test. This does not cover untouched legacy rites or SDL/library internals. |
| Physical speaker/USB PCM/listening | **NOT RUN** in this component work. |

Observed 24 kHz sine maximum interpolation error was `4.72685e-6`; computed
440 Hz phase-increment frequency was `439.999999478` Hz. One-second exponential
decay differed from `exp(-1)` by `1.88053e-5` at 24 kHz and `0.000502318` at
48 kHz, within the documented `0.001` float-coefficient tolerance. Delay16's
maximum quantization error was `6.09756e-5`, below one `1/16384` quantization
step. The deterministic vowel sweep's maximum raw float magnitude was `0.828375`.

The existing Chamberlin band-pass has a damped discrete response whose peak
is slightly above its nominal coefficient frequency. The impulse-response
measurement agrees within the 10 Hz search resolution with
`cos(w) = 1 - f² / (2 sqrt(1 - fq))`. At 24 kHz, nominal 330/850/1900/3050 Hz
produced measured peaks 330/860/1980/3260 Hz. Consequently these formant labels
are approximate spectral targets; exact formant placement is a useful future
refinement. The test preserves and quantifies this character instead of claiming
human-vowel accuracy from the nominal constants alone.

## Native computational observations

The uninstrumented x86-64 Linux/GCC 14.2 harness rendered 128-sample blocks.
There were 1,500 CANON blocks and 938 blocks for each other specimen. Timing
includes the small test HAL mutex/call seam; graphics and real audio I/O are
excluded. Shared-host scheduling and concurrent qualification affect tails.
These figures are not ESP32 utilization measurements or performance gates.

| Workload | Median µs | p99 µs | Worst observed µs |
| --- | ---: | ---: | ---: |
| CANON, maximum tempo/spread | 9.674 | 21.982 | 38.447 |
| DRONE, six styles | 6.480–6.520 | 6.680–18.388 | 21.632–100.288 |
| GLYPHSONG speak | 0.991 | 2.263 | 29.353 |
| GLYPHSONG chant | 1.462 | 2.644 | 74.320 |
| GLYPHSONG choir | 1.813 | 3.765 | 15.443 |
| GLYPHSONG continuous sing | 2.043 | 2.454 | 17.285 |

At 24 kHz the actual CYD 128-sample deadline is **5,333.333 µs**; Cardputer's
256-sample deadline is **10,666.667 µs**. Utilization is callback elapsed time
÷ its actual block deadline. The table's CANON p99 is 0.412% of the 128-sample
budget on this desktop only. Native SDL normally requests 512 samples; the WAV
worker renders 256. Measure each platform using its actual callback length.

Native object sizes were CANON 17,320 bytes, DRONE 18,240 bytes and GLYPHSONG
20,880 bytes. These are `sizeof` values, not complete heap peaks: platform
pointer width, allocator overhead, graphics, radio, audio queues and static
storage remain additional. See [DSP-PORTABILITY-RAG](DSP-PORTABILITY-RAG.md)
for component boundaries and downstream constraints.

## Remaining owner checks

Use final diagnostics firmware to measure callback median/p95/p99/max,
deadline overruns, audio cadence and heap/largest-free-block under choir,
maximum-density CANON, each DRONE style and concurrent graphical activity.
Repeat entry/exit after Wi-Fi and BLE rites. Audition the corrected vibrato and
filter-LFO motion, touch note/rest editing, text caret and mode changes. Verify
USB PCM continuity independently; finite offline PCM is not evidence of a
continuous physical stream. Leave the integration Draft/HOLD until these
hardware checks and the owner's musical evaluation are complete.
