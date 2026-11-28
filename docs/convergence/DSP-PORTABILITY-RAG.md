# Downstream DSP implementation findings

**Evidence scope:** portable numerical tests and actual Diviner instrument
kernels at component commit `25329fe60d9d993d4f7f9c6098c46b3257edc189`;
[executed method and receipts](DSP-ENGINEERING.md). No related repository was
modified. These are integration notes, not evidence of a completed FM-1,
ESP32-S3 USB Audio, MHAC or VST port.

## Reusable boundaries

| Source | What can be reused | Dependencies and storage |
| --- | --- | --- |
| `src/dsp.h` | Phase oscillators, FM phase modulation, decay, noise, SVF, float/int16 delay, scope | C++17, `<cmath>`-style math and lock-free 32-bit atomics. Define `DIVINER_DSP_RATE` to build without Diviner HAL. Shared sine table: 1,025 floats (4,100 bytes) plus guard; prewarm on a non-audio thread. |
| `src/vox.h` | Animalese letter articulation, held vowel/formant voice, onset/coda, diphthong and vibrato; bounded text reader | `dsp.h`, `audio_exchange.h`, C strings. Native sizes: Voice 188 bytes; Reader 592 bytes including three text publication slots. No heap allocation in their normal operation. |
| `src/dsp_score.h` | Deterministic 40-character text-to-syllable score, note suffixes and holds | `vox.h`; output array supplied by caller, at most 48 nine-byte notes. This is a small musical heuristic, not natural-language phonemization or UTAU support. |
| `src/audio_exchange.h`, `src/audio_storage.h` | Complete latest-value snapshots, bounded ordered event queues, internal-RAM state allocation | C++17 atomic uint32; strict non-reentrant single producer and single consumer. Latest holds three copies of T plus ownership metadata. Publish: at most two strong CAS attempts; consume: one. Reader's 132-byte text message exchange is 408 bytes on the measured host. On the admitted S3 IDF 5.5 workaround, control words must be internal RAM and construction checks this. Derive heap State owners from `InternalState` to enforce placement; native allocation remains unchanged. |
| `src/apps/canon.cpp` | Four phasing sequencers and contrasting two-voice FM timbres | Instrument currently includes theme/HAL. Port the musical state and callback, retaining the control snapshot seam; do not import touch/display code into an audio plugin. |
| `src/apps/drone.cpp` | Six FM-percussion/pad styles and generative movement | Retains five pad voices, three bell voices and 4,096-float delay. The delay object is 16,388 bytes native; a same-length Delay16 is 8,196 bytes but is not an automatic sound-equivalent replacement. |

Flash contribution per component is **NOT MEASURED**. Header inlining, LTO,
shared sine-table deduplication, math libraries and platform support affect
link results; use the final firmware map rather than adding source sizes.

## Ownership contract to preserve

1. The UI/host parameter thread authors complete parameter snapshots.
2. The audio callback adopts at most one latest revision at a block boundary.
3. Only the audio callback writes oscillators, envelopes, filters, delays,
   current-note counters and voice configuration.
4. Drawing/host telemetry consumes a separate audio-to-UI snapshot.
5. Detach and quiesce the callback before destroying instrument state.

`Reader::post/stop` are producer operations. Reader `start`, `step`, `hz`,
`sylSec`, `loop`, `pitchFn`, `ctx`, `len`, `idx` and all Voice fields are
audio-owned or accessed only before callback publication. A second producer
(e.g. a network callback and UI both calling `post`) needs serialization on
the control side. `volatile` and an atomic payload length do not establish
ownership of the payload bytes.

Latest-value exchange coalesces intermediate updates. Use `SpscQueue` for
ordered note/call events and account for its false/full return; enqueue
success is not a receipt or guarantee of later musical execution. Bounded
queues make overload observable without blocking audio.

## Rate, signal and CPU findings

Diviner's application is 24 kHz mono int16 PCM. The primitive suite also passed
at 48 kHz, including stability, phase precision and score determinism. That
**does not establish sample-rate-equivalent timbre**: vox attack durations and
oscillator increments use the chosen rate, but fixed per-sample smoothing,
noise decay and control divisors still change their wall-clock behavior with
sample rate. Normalize these coefficients deliberately for 44.1/48/96 kHz
VST or USB Audio reuse, then rerun spectral/envelope and listening checks.

Use `Osc::sineStrided(divisor)` for an oscillator sampled at a reduced control
rate. Merely calling `hz(5.2)` and `sine()` every sixteenth sample produces
0.325 Hz, not 5.2 Hz. The convergence corrects that concrete vox vibrato defect.

The SVF is finite and stable across the executed control sweep, but its
band-pass resonance differs from nominal formant labels: the 24 kHz nominal
3,050 Hz peak measured 3,260 Hz. Its pitch source and spectral envelope are
separate properties. Letter articulation is intentionally stylized; reliable
natural speech, speaker identity and perceptual intelligibility were not
qualified. Delay16 adds approximately one 1/16384 quantization step and
saturates at ±1.99; compare decay tails and feedback behavior before replacing
a float delay.

Offline GLYPHSONG choir rendered with a desktop median 1.813 µs and p99
3.765 µs per 128-sample block; the same specimen peaked at 27,676 PCM units.
This demonstrates functional two-voice feasibility on the tested host,
**not ESP32 headroom**. The actual embedded callback deadline is block samples
÷ sample rate, and full I/O/radio/display scheduling must be measured with
instrumented firmware. Three-state memory reuse and real-time no-allocation
checks are more transferable than these desktop timings.

## Practical downstream choices

For FM-1 or CYD VIBEWARE, copy the small synthesis headers with their notices
and keep the receiving instrument's existing audio and control architecture.
For USB Audio ESP32-S3, retain host-buffer ownership and sample-rate conversion
explicitly; a USB callback is not the place to compile text, allocate scores
or wait on a radio peer. For MHAC/VST, separate score generation from render,
use host sample rate and block size, and carry control snapshots into the
plugin's audio thread. A distributed node can receive bounded syllable/score
or note events and synthesize locally; that musical coordination does not
create a sample-synchronous distributed DSP graph.

Preserve the repository's MIT license, original upstream attribution and
`NOTICE.md`. These components remain inside Diviner to keep upstream changes
traceable; no external-library release or downstream API-stability guarantee
is implied. Physical CPU cost, heap fragmentation, end-to-end latency and
listening quality remain owner-side evidence requirements.
