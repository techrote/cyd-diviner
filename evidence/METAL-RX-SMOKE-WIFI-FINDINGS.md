# Native receiver smoke observations

Both independently identified receivers completed **18/18 windows** in `metal-dual-rx-smoke-wifi-02`. Each board completed 189 snapshots: three repetitions of 20-snapshot no-export bench windows and single-snapshot export windows at each nominal 16/40/80 MS/s setting. Only 18 snapshots per board were exported and retained; bench exports retain the final snapshot of each 20-snapshot loop. All 36 windows retain their original `ACQUIRED` status. These are receiver-only tests: **no TX command was dispatched**.

Runtime identity, both image hashes, every raw artifact hash, shared TX-log hashes, snapshot CRC/LF and canary/owner/lane restoration were verified. Source is `3b4161fbcbd6832264019aa3a4fb993a85798857`, profile `s3-metal-snapshot`, image SHA256 `468c2187cc447510561038212539fdbeab3a95298024d412c13db2e6b2d85e7e`, source digest `d4405afd6fe2fea726fe40a7180df46758f87ad3b59b5c13d2e29c7ea0c4a72e`. COM22/MAC `1c2904bec3a0` is RX; COM23/MAC `1c2904bec3d8` is RX2. Both explicitly used **WiFi_NULL**, 2433 MHz, offset −80 kHz and manual gain index 40. Driver-based preparation here is separate from the direct-PHY/gain startup failure cohorts.

The table summarizes three actual windows per row. “Trigger/done” is the median across per-window means (20 captures for bench; one for export); brackets show the range of those three means. “Loop/capture” includes the full selected cohort. Configured clock arithmetic and CPU observations are separate.

| Receiver | Cohort | Nominal MS/s | Nominal horizon µs | Trigger/done µs [range] | Loop/capture µs | Words/loop second | Nominal horizon/loop |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| rx | bench | 16 | 1023.75 | 1025.55 [1025.20–1025.55] | 2,211.50 | 7,406,738 | 46.292% |
| rx | bench | 40 | 409.50 | 411.20 [411.20–411.25] | 1,595.25 | 10,267,983 | 25.670% |
| rx | bench | 80 | 204.75 | 206.25 [206.15–206.30] | 1,389.10 | 11,791,808 | 14.740% |
| rx | capture | 16 | 1023.75 | 1025.00 [1025.00–1026.00] | 264,722.00 | 61,876 | 0.387% |
| rx | capture | 40 | 409.50 | 411.00 [411.00–412.00] | 264,640.00 | 61,895 | 0.155% |
| rx | capture | 80 | 204.75 | 207.00 [206.00–207.00] | 263,667.00 | 62,124 | 0.078% |
| rx2 | bench | 16 | 1023.75 | 1025.50 [1025.45–1025.65] | 2,211.70 | 7,406,068 | 46.288% |
| rx2 | bench | 40 | 409.50 | 411.20 [411.20–411.30] | 1,595.25 | 10,267,983 | 25.670% |
| rx2 | bench | 80 | 204.75 | 206.20 [206.05–206.25] | 1,389.10 | 11,791,808 | 14.740% |
| rx2 | capture | 16 | 1023.75 | 1026.00 [1026.00–1026.00] | 83,074.00 | 197,174 | 1.232% |
| rx2 | capture | 40 | 409.50 | 412.00 [411.00–412.00] | 82,873.00 | 197,652 | 0.494% |
| rx2 | capture | 80 | 204.75 | 206.00 [206.00–206.00] | 83,400.00 | 196,403 | 0.246% |

The CPU-observed trigger-to-done interval is approximately 1.25–2.50 µs longer than the 16,380/clock arithmetic horizon. The cycle-counter sums provide the corresponding target-CPU observations. This supports completion timing consistency at the configured native settings; it does not independently calibrate the sample clock or establish RF bandwidth, sensitivity or signal origin.

No-export bench loop time remains approximately 2.212 ms, 1.595 ms and 1.389 ms per capture at nominal 16, 40, 80 MS/s. The corresponding measured whole-loop rates are about 7.41, 10.27 and 11.79 million native words/second. These include real safety/observer work. Every capture writes 16,380 SENTINEL words before the timed trigger bracket and compares 16,380 words afterward; four canaries, gain/preparation checks, locks, register setup and restoration also remain. The non-trigger/done portion is about 1.18 ms per capture. **That aggregate cost is not an isolated sentinel benchmark.** No-export timing excludes per-capture CRC, formatting and USB; the final buffer CRC/readout follows the measured loop.

Export loops take approximately 263.6–265.1 ms on RX/COM22 versus 82.0–84.3 ms on RX2/COM23 for 65,520 native payload bytes plus JSON/LF/flush. RX is about 3.18 times slower in this cohort despite nearly identical no-export timing. This is the observed complete readout path, including checks, CRC, firmware USB, host pumping and Windows transport; no component or board-brand cause has been isolated. Every target transfer-failure delta is zero. The compiled wrapper uses a 5 s per-send deadline and 5 s flush wait; successful transfer completion here is comfortably within those bounds, not a guarantee for another host/rate.

Both receivers report exactly 216,016 B free INTERNAL|8BIT heap and 172,032 B largest block before and after every run, with no observed run-level heap drift. This is a boundary observation, not a sampled in-loop minimum or proof of every future allocation. No application allocation or USB transfer occurs inside the native snapshot body.

**Temperature is NOT MEASURED in this cohort:** the plan explicitly disables queries after the earlier sensor-command stall; the earlier failed capture remains separate. Capture indices reset to zero per burst; the 500 ms arm delay lies outside loop timing, and requested zero inter-snapshot gap does not remove preparation/check costs. Independent board clocks and USB arrival times do not synchronize the two receivers. CRC, hardware-done, canary and restoration demonstrate acquisition integrity, not received RF packets or analog/audio output.

Per-receiver generated analysis: [RX report](metal-smoke-wifi-rx-analysis/METAL-REPORT.md), [RX2 report](metal-smoke-wifi-rx2-analysis/METAL-REPORT.md). Exact repetitions, identities, heap fields and formulas: [comparison JSON](METAL-RX-SMOKE-WIFI-FINDINGS.json), [per-repetition CSV](METAL-RX-SMOKE-WIFI-FINDINGS.csv). Raw input paths/hashes and compiled-source hashes are preserved in that JSON.
