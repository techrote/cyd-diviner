# Matched native-frame observations before and after receiver movement

Both complete cohorts contain 30/30 acquired windows per receiver, covering 10 source/clock settings with three repetitions each. **All 30 before/after pairs match exactly per board** for workload/repetition settings and uploaded packed-bit SHA. Runtime board/MAC/profile/source/image identities, receiver init/tune/gain, transmitter allocation/observer settings, disabled temperature policy, frozen decoder ELF and declared trial policy are unchanged. Only the owner-reported physical receiver exchange is the intended intervention; time, cables and antenna angles are not controlled laboratory variables.

The owner described the initial arrangement as hanging boards about 40 cm apart, free air between them and nearby cables. COM22 and COM23 were then exchanged physically without unplugging, while TX stayed fixed; preserving angles was requested but not independently measured. The public owner metadata files are [initial arrangement](metal-placement-owner-note.json) and [swap confirmation](metal-placement-owner-swap.json). This supports placement-dependent received behavior and removes any basis for attributing the earlier difference solely to board brand. It does not isolate calibrated antenna gain, multipath, coupling or every changed physical factor.

| Receiver / stage | Acquired windows | Windows with matching headers | Selected known frames | Conditional errors / known bits | Native code² power median across 30 windows |
| --- | ---: | ---: | ---: | ---: | ---: |
| rx / before | 30/30 | 18/30 | 38 | 1838 / 8192 | 7,182.8 |
| rx2 / before | 30/30 | 5/30 | 9 | 44 / 2176 | 220.9 |
| rx / after | 30/30 | 13/30 | 22 | 722 / 4352 | 2,128.8 |
| rx2 / after | 30/30 | 16/30 | 27 | 1461 / 6016 | 15,343.7 |

COM22/MAC `1c2904bec3a0` changes from 18 to 13 matching-header windows; COM23/MAC `1c2904bec3d8` changes from 5 to 16. The native power-code median decreases on COM22 and increases markedly on COM23. This is consistent with the separate position-swap calibration observations reported by the parent campaign, while each cohort retains its own scope. Code² is a digital receiver indication, not calibrated RF power; the medians summarize the same 30 matched window settings, not an RF sensitivity test.

**Payload denominators are conditional received domains.** A window with no matching complete header has unknown payload BER, not zero errors. The stronger post-swap receiver also accepts more unpaced frames with substantial payload errors. The pooled error/bit counts above therefore must not be used to rank receiver quality or interpret a placement change as worsened RF fidelity: the accepted source-frame populations and clock modes differ. Exact per-setting bit/error counts and every window remain in CSV/JSON.

| Matched source / clock setting | COM22 matching windows before→after (each /3) | COM23 matching windows before→after (each /3) |
| --- | ---: | ---: |
| 24000-2ch-osr4-p128 / 360000 | 1→1 | 1→2 |
| 48000-2ch-osr4 / 552000 | 3→1 | 1→2 |
| 48000-2ch-osr8 / 1104000 | 3→3 | 3→3 |
| 48000-4ch-osr8 / 2208000 | 0→0 | 0→0 |
| 24000-2ch-osr32 / 2208000 | 0→0 | 0→0 |
| 24000-2ch-osr4-p128 / 360000  /  unpaced | 3→3 | 0→3 |
| 48000-2ch-osr4 / 552000  /  unpaced | 3→3 | 0→3 |
| 48000-2ch-osr8 / 1104000  /  unpaced | 3→1 | 0→1 |
| 48000-4ch-osr8 / 2208000  /  unpaced | 2→1 | 0→2 |
| 24000-2ch-osr32 / 2208000  /  unpaced | 0→0 | 0→0 |

The analysis uses the same predeclared unpaced clock centres (software-loop mean ×1, 1.004, 1.008) and one requested-baud centre for paced cases. All 60 trials per receiver per cohort remain available. Matching header/config count, then training-fit error, determines selection; known payload BER, confidence and PCM quality do not choose a candidate. An independent read-only audit verified the original cohort conversions, all 120 original trial scores/selections, header CRC/config and 47 selected frame BER/PCM mappings.

The finite native horizons are 204.75–1023.75 µs at configured clocks. Arbitrary source phase, first/last cropping, skipped source slots and current channel conditions can alter which complete headers fit. The receiver clocks are independent, so before/after windows are matched configurations, not aligned IQ samples or paired identical source phases. Replayed 0..31 source-ring sequence counters are not RF packet-loss counters. Effective unpaced replay rate comes from recovered symbol timing rather than the source header; received-domain PCM remains digital concatenation without a live DAC, analog-output, continuous throughput or synchronized one-way latency claim.

**Excluded from the main comparison:** `metal-dual-framed-wifi-swapped-01` remains FAILED after two strict complete exposures and a third partial RX USB transfer. `metal-readback-recovery-01` records recovery of the retained aperture with the exact saved CRC through USB only, without a new RF acquisition. Those artifacts remain separate; neither their successes nor their recovery are merged into these 30-window cohorts or used to promote the failed attempt.

Original cohort: [RX demod](metal-framed-wifi-rx-demod/results.json), [RX2 demod](metal-framed-wifi-rx2-demod/results.json). Complete post-swap cohort: [RX demod](metal-framed-wifi-swapped-rx-demod/results.json), [RX2 demod](metal-framed-wifi-swapped-rx2-demod/results.json). Comparison artifacts: [JSON with exact context assertions](METAL-FRAMED-POSITION-COMPARISON.json), [per-setting CSV](METAL-FRAMED-POSITION-COMPARISON.csv), [all 120 windows CSV](METAL-FRAMED-POSITION-COMPARISON-windows.csv). The comparison manifest pins original/raw/derived and owner-metadata hashes.
