# DIVINER — UPSTREAM-CONVERGENCE-001

Disposition: **Draft/HOLD**. No merge, hardware flash, upstream pull request or
changes to related repositories are authorized by this candidate.

## Source provenance

| Source | Exact revision | Treatment |
| --- | --- | --- |
| CYD `main` | `7b1b37eec6ac126f334a0b008d91135071ea60fa` | First parent; preserve CYD platform and visual work |
| Original upstream `main` | `c1b1175bcf15072bc2538ccdde6806d5901b16e9` | Second parent; retain all ten upstream-specific commits and their attribution |
| Common ancestor | `8e6155e30d03f9b7577335d9a29cc66c8c745e87` | Independently checked with `git merge-base` |
| Separate performance PR #7 | `771064d419c4391f8252a31107606f4a801e8186` | Read-only evidence; not an integration dependency |

The initial discovery found the commissioned main revisions unchanged: 12
CYD-specific commits, ten upstream-specific commits, and 35 upstream changed
files. PR #7 remains a separate Draft/HOLD campaign with 44 commits and 1,186
changed files. Its SDR prototypes, specialized hardware profiles and retained
archives are not silently promoted into ordinary Diviner.

Sources: [CYD main](https://github.com/techrote/cyd-diviner/commit/7b1b37eec6ac126f334a0b008d91135071ea60fa),
[upstream](https://github.com/numbpill3d/cardputer-adv-diviner/commit/c1b1175bcf15072bc2538ccdde6806d5901b16e9),
[performance PR](https://github.com/techrote/cyd-diviner/pull/7).

## Integration decisions

The original HAL and rite boundaries remain. New instruments share DSP,
formants, score construction, bounded audio exchanges, CV1 coordination and
presentation helpers. An optional logical pointer callback extends `Rite`;
keyboard handling stays intact. `native-cyd` runs the actual CYD controls and
palette renderer using the SDL backend, with explicitly simulated radio.

The merge reconciled conflicts in DRONE, Sentinel, the radio HAL, the shell,
theme declarations, README and historical flashing documentation. CYD's audio
mutex is retained; Cardputer and native gain a non-waiting audio admission gate
covering both main and overlay callback lifetime. Control threads wait for an
in-flight callback before destroying detached state.

DRONE adopts upstream's dynamically released state and six FM styles. This
deliberately replaces CYD's earlier permanently resident core state while
preserving settings separately and retaining low-memory safe-mode behavior.
The implementation is tested for failed state allocation and repeated leave.

CYD palette persistence, twelve themes, compact carousel, rotated touch,
animated backdrops, layered Visions, feedback trails, fullscreen rendering,
USB serial PCM and VLC plugin paths remain. Cardputer IR and Probe remain
excluded on CYD as before. The removed upstream TILT experiment stays removed;
its historical IMU/gesture vocabulary is a future hardware-specific research
input, not an advertised feature of this build.

## Qualification boundaries

Build success, numeric correctness, thread-sanitized component tests, native
application runs and physical hardware behavior are separate gates. New
instrument TSan checks do not assert that all older rites or third-party SDL
internals are race-free. ASan/UBSan are not race detectors. Local LeakSanitizer
was blocked by ptrace restrictions; the first complete CI run subsequently passed
with leak checking enabled. Final-source receipts must identify their own run.

Native radio fixtures use deterministic invented identities; actual Wi-Fi,
BLE, ESP-NOW coexistence, radio-induced audio disturbance and multi-board
musical timing require the [owner procedure](OWNER-EVALUATION.md).

See [qualification](QUALIFICATION.md), [interaction](INTERACTION.md),
[upstream synchronization](UPSTREAM-SYNC.md) and [portability RAG](PORTABILITY-RAG.md).

## Reviewed unmerged work and historical motion instrument

The final repository inventory still found no open upstream pull requests.
Upstream issue #5 requests further SEQ/MIDI/track-editing features; those are
outside this convergence and have not been silently added. The upstream
branch `codex/ir-tv-support-retire-network` at
`46222df5b086668470e936e1b0fe33811d7694f8` diverges from current main by four
branch-only and ten main-only commits. Its IR expansion, network-rite retirement
and alternative DRONE work are not the selected upstream main baseline;
retiring the owner's existing network rites would conflict with preservation.
The upstream `drone-bell-styles` and older CI branches were also inventoried.
Owner PR #7 remains open Draft at the exact revision above.

TILT was introduced in `eeb19eb7972b41913e48ef3930867347a2c54d5c` and removed in
`b51f06b8f8535fdba54b3396ad7f024b0e670dde`. Its four-voice chord followed a
simulated marble: horizontal movement chose scale degrees, vertical position
controlled filter brightness, speed controlled shimmer, rim impacts chimed,
and a detected shake scattered bells. It provided level calibration, axis
flips, recenter/lock and keyboard nudges when no IMU was present.

Useful future lessons are a capability-reporting accelerometer HAL, explicit
calibration/orientation and separation of gesture events from synthesis. A
future port should use measured elapsed time instead of the old fixed 33 ms
physics step, and replace its volatile strike ring/shared targets with the
qualified bounded exchange mechanisms. This is historical research, not an
integrated or physically qualified motion instrument. TILT remains removed.
