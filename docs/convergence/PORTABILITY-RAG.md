# Downstream implementation RAG — Diviner convergence

**Status: implementation evidence inside Diviner; downstream ports NOT RUN.**
This document is a retrieval entry point for subsequent work in
[CYD SYNTH VIBEWARE](https://github.com/techrote/cyd-SYNTH-VIBEWARE),
[FM-1 VIBEWARE](https://github.com/techrote/FM-1-VIBEWARE),
[ESP32-S3 USB Audio](https://github.com/techrote/USBaudio-ESP32S3),
[FM-1 MHAC](https://github.com/techrote/FM-1-MHAC) and
[MachineHumanAudioCybersemiotics](https://github.com/techrote/MachineHumanAudioCybersemiotics).
Those repositories were not modified. Recommendations below identify source
seams to evaluate, not completed compatibility claims or stable external APIs.

## Reusable implementation boundaries

| Capability / retrieval terms | Source and dependency boundary | Evidence and constraint |
| --- | --- | --- |
| Formants, animalese, machine vocalization | `vox.h` + `dsp.h` + `audio_exchange.h`; C++17, math, lock-free-width atomics | Host Voice 188 B, Reader 592 B; finite/deterministic at 24/48 kHz. Nominal formant and actual SVF peak differ. Stylized speech-like synthesis, not natural language or UTAU. |
| Syllables, annotated singing | `dsp_score.h`; bounded input/output, caller-owned score | 40 input characters, at most 48 notes; explicit degrees/holds, deterministic composition, no render-thread score allocation. |
| Generative phase counterpoint | CANON's control snapshot and audio state in `apps/canon.cpp` | Four phase clocks, distinct FM voices, phrase/rest editing, scales, hold/alignment. Separate UI from callback before transplant. Host state 17,320 B. |
| Six-style generative drone | `apps/drone.cpp`, `dsp.h` | Five pads, three FM bells, float delay; host state 18,240 B. Control-rate LFO frequency bug corrected. |
| Coherent audio parameters | `audio_exchange.h` | Three-slot ownership transfer; one producer/consumer. Latest coalesces revisions, queue preserves ordered admitted events. Never add a second producer without control-side serialization. |
| Environmental sonification | `radio_sonification.h` + SPECTRA audio/telemetry seam | Stable identity-to-pitch, observation freshness, RSSI dynamics, bounded population/fades. Hash identity is not a privacy guarantee; do not persist identifiers. Host state 12,128 B excludes radio SDK/cache. |
| Distributed musical pulse | `coven_protocol.h` + `radio_queue.h` | Actual shared parser/session used in firmware and simulation. Session 376 B on host; ten remote peers; queue 15 packets. CV1 wire retained, one-hop lowest-MAC election and 15 s lease. |
| Distributed local vocalization | COVEN + bounded DYAD parser + vox | Host COVEN state 19,408 B; no shared DSP graph or sample clock. Untrusted text bounded/sanitized/rate-limited, transient only. |
| Safe callback lifetime | `audio_lifecycle.h`, HAL contract | One audio consumer/one control producer; detach waits only on control thread, audio admission never waits. Includes overlay lifetime. CYD retains its existing mutex implementation. |

Sizes are measured native ABI values, not ESP32 heap/flash measurements. Final
firmware maps and manifests describe whole-image static memory and flash. Radio
SDK heap, vector capacities, display buffers, fragmentation and runtime peaks
must be measured on the receiving hardware. The shared sine table is 4,100 B;
an atomic Scope adds 512 B versus the earlier 16-bit plain storage on the tested
ABI. Float Delay<4096> is 16,388 B; Delay16<4096> is 8,196 B and changes quantization.

## Integration choices that should survive a port

Generate phrases/scores and perform text/network parsing outside the audio
callback. Publish an immutable complete revision at block boundaries, retain
DSP ownership on the render thread, and transfer telemetry through a separate
exchange. On shutdown detach and wait for in-flight render before destroying
state. Check queue-full returns and publish drop counters. Do not replace this
with a `volatile` flag, shared text buffer, or payload-length compare/exchange.

The instrument application is 24 kHz mono PCM. Numerical tests at 48 kHz prove
those tested numerical properties only. Per-sample smoothing, noise decay and
control divisors still require deliberate time normalization for 44.1/48/96 kHz
USB Audio or VST work. A reduced-rate oscillator must advance by its stride:
the old 5.2 Hz vox vibrato advanced every sixteenth sample and ran at 0.325 Hz.

Retain the receiving project's buffer ownership, interrupt and host transport
contracts. A USB callback must not allocate/compile scores or wait for radio.
For a synthesizer plugin, map host parameters into snapshots and pass actual
sample rate/block size; for auxiliary radio nodes, distribute musical events
and synthesize locally. CV1 cannot provide sample-accurate distributed audio.
An incompatible protocol extension should carry a new version/boot nonce and
document coexistence with CV1; no such extension is required here.

Wi-Fi/BLE observation is non-associating but active and may transmit requests.
RSSI is an unstable signal measurement, not calibrated distance or position.
Avoid collecting/publishing device names or addresses in installations. CV1 is
unauthenticated; bounded processing does not make it resistant to hostile
flooding or replay. See [radio evidence and limits](RADIO.md).

## Evidence to retrieve before implementation

- [DSP engine and ownership](DSP-ENGINEERING.md), [detailed DSP portability](DSP-PORTABILITY-RAG.md), and [exact legacy PCM regression](DSP-LEGACY-REGRESSION.md).
- [CV1 scenarios, timing bounds, radio lifecycle and physical limits](RADIO.md).
- [Reproducible builds/tests](BUILD-AND-TEST.md), [final qualification](QUALIFICATION.md) and the exact CI artifact manifests/logs.
- [Owner hardware procedure](OWNER-EVALUATION.md) for callback deadlines, heap recovery, touch usability, serial PCM and multi-board captures.

Keep the MIT license, original notices, upstream commit provenance and
`NOTICE.md` with copied source. Component flash cost, embedded per-voice CPU,
heap fragmentation, listening quality and downstream host compatibility remain
unmeasured until the receiving implementation is independently qualified.
