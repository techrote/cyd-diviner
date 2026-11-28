# Future upstream convergence

Retain the `upstream` remote pointing to
`https://github.com/numbpill3d/cardputer-adv-diviner.git`. The last integrated
upstream revision is `c1b1175bcf15072bc2538ccdde6806d5901b16e9`.

1. Fetch both remotes and record immutable main SHAs, merge-base, changed paths,
   active PRs and current physical evidence. Check whether another integration
   or performance worker owns overlapping files before editing.
2. Branch from current owner main. Compare upstream from the last integrated
   revision and merge with history preserved. Resolve platform contracts before
   adapting instruments; do not overwrite CYD controls/themes with upstream's
   older equivalents.
3. Keep changes grouped by shared DSP/score, radio protocol, platform HAL,
   touch/input, and qualification. Preserve MIT/OFL notices and original authors.
4. Run all supported firmware profiles, native and native-cyd; the portable
   DSP/radio/touch/lifecycle suites; ASan/UBSan and scoped TSan; actual application
   tours; partition verification and artifact hashing. Review rendered pixels
   and WAVs. Do not infer hardware deadline margin from desktop benchmarks.
5. Publish one Draft/HOLD candidate with exact source identity, direct CI and
   artifact links, explicit failed/blocked/unexecuted gates, and owner checks.
   Promote only after owner physical evaluation and explicit merge authorization.

## Candidate upstream contributions

| Change | Upstream value | Boundary |
| --- | --- | --- |
| `audio_exchange.h`, `vox::Reader` ownership, immutable GLYPHSONG scores | Fixes same-length publication races and overwrite races | Portable; submit tests with the change |
| Callback/overlay lifetime and atomic UI sound queue | Prevents use-after-free and removes `volatile` handoff assumptions | Portable contract plus each HAL implementation |
| Control-rate oscillator stride | Restores intended vibrato/LFO frequency | Audible correction; disclose before acceptance |
| SVF coefficient bound and finite PCM conversion | Bounds unstable/invalid numeric inputs | Portable numeric tests at 24/48 kHz |
| CV1 elected-leader-only synchronization and parser/rate limits | Deterministic membership, packet loss/reorder recovery | CV1 wire remains compatible; no authentication claim |
| Wi-Fi scan freshness/cache bounds and ESP-NOW IDF4/5 seam | Maintains old/new ESP32 compatibility | Device-specific adapters plus portable tests |
| Optional logical pointer callback and native-cyd simulator | Allows touchscreen ports without changing keyboard operation | Small additive Rite contract |
| Clean artifact metadata, partition checks and native capture tests | Reproducible releases and reviewable evidence | CI/tooling |

No external PR or maintainer contact is part of this mission. Prepare focused
patches from these boundaries when the owner requests an upstream contribution.
