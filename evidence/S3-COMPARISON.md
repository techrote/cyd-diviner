# Physical S3 and classic CYD kernel comparison

CYD capture: **CAPTURE COMPLETE**, 66 valid cases. S3 capture: **CAPTURE COMPLETE**, 132 valid cases. Only completed, metadata-matched workload cases enter timing ratios.

* **CYD** `cyd-profile`: source `a4549f87cef05d19d9acf96d59ae95a2435ebb67`, firmware `fb35d75725b15ecd8b6aceb368492cf76ebaee85bbb4aeb50bd61311e229f15c`, raw input SHA256 `cb4d53d91c8d625acab8faed07bbf91aa58a726ccc7e65d6a0510b09ed0dc41d`; CPU 240 MHz, flash 40000000 Hz, initialized PSRAM 0 bytes.
* **S3** `kernel-s3-n4r2-peer`: source `174a79670d4b0da656ebd615f0cd1620aa0ddbb1`, firmware `7ac5bf7f79bb2ed6048a40367fe1d3966a1d5e53a999b2f09f28f310456bb681`, raw input SHA256 `d89c28832226ddbe53db4efa2a1faa6876d1eb531ef3fb7d1f5c7017405a312d`; CPU 240 MHz, flash 80000000 Hz, initialized PSRAM 2095087 bytes.

Both CPUs were configured at 240 MHz, but flash is 40 MHz on the CYD and 80 MHz on this S3 profile. The CYD is the Diviner profiling image with its background DAC/audio task and observer arrays; its frame loop pauses during each kernel command. The S3 is standalone, without the ordinary audio/display workers, with the idle peer worker/USB buffer present. These are observed configured-system comparisons, not a controlled isolation of CPU/FPU or architectural advantage. Internal and PSRAM results remain separate, with exact capability versus inferred-zero-PSRAM receipts in JSON.

Each completed repetition has three warmups, reinitialization from seed, and 256 exact timed steps. The table pools exact timings across matching repetitions. Initialization, allocation, yields and output inspection are excluded; RTOS preemption inside a step is included. A ratio above 1 means the S3's observed median is lower in these contexts, not a promised application speedup.

| Kernel / size | S3 region | reps CYD/S3 | samples CYD/S3 | median CYD/S3 µs | p99 CYD/S3 µs | observed median ratio | fingerprints match |
| --- | --- | --- | --- | --- | --- | --- | --- |
| osc-f32/realistic | internal | 3/3 | 768/768 | 80/67 | 81/67 | 1.194 | True |
| osc-f32/large | internal | 3/3 | 768/768 | 318/265 | 319/265 | 1.200 | True |
| osc-q15/realistic | internal | 3/3 | 768/768 | 28/25 | 28/25 | 1.120 | True |
| osc-q15/large | internal | 3/3 | 768/768 | 109/97 | 110/97 | 1.124 | True |
| filter-f32/realistic | internal | 3/3 | 768/768 | 75/62 | 75/62 | 1.210 | True |
| filter-f32/large | internal | 3/3 | 768/768 | 297/248 | 306/248 | 1.198 | True |
| filter-q15/realistic | internal | 3/3 | 768/768 | 50/44 | 50/44 | 1.136 | True |
| filter-q15/large | internal | 3/3 | 768/768 | 192/169 | 192/169 | 1.136 | True |
| modal-f32/realistic | internal | 3/3 | 768/768 | 196/175 | 197/175 | 1.120 | True |
| modal-f32/large | internal | 3/3 | 768/768 | 1916/1741 | 1924/1741 | 1.101 | True |
| modal-q15/realistic | internal | 3/3 | 768/768 | 304/275 | 305/276 | 1.105 | True |
| modal-q15/large | internal | 3/3 | 768/768 | 3713/3346 | 3715/3347 | 1.110 | True |
| fft-f32/realistic | internal | 3/3 | 768/768 | 297/282 | 306/282 | 1.053 | True |
| fft-f32/large | internal | 3/3 | 768/768 | 1405/1350 | 1414/1350 | 1.041 | True |
| cellular/realistic | internal | 3/3 | 768/768 | 1247/1026 | 1256/1028 | 1.215 | True |
| cellular/large | internal | 3/3 | 768/768 | 4977/4065 | 4979/4067 | 1.224 | True |
| particles/realistic | internal | 3/3 | 768/768 | 23/19 | 24/19 | 1.211 | True |
| particles/large | internal | 3/3 | 768/768 | 90/73 | 91/74 | 1.233 | True |
| feedback/realistic | internal | 3/3 | 768/768 | 1201/1069 | 1217/1083 | 1.123 | True |
| feedback/large | internal | 3/3 | 768/768 | 4489/3974 | 4528/3974 | 1.130 | True |
| stripe-rgb565/realistic | internal | 3/3 | 768/768 | 81/76 | 82/76 | 1.066 | True |
| stripe-rgb565/large | internal | 3/3 | 768/768 | 322/301 | 331/301 | 1.070 | True |
| osc-f32/realistic | psram | 3/3 | 768/768 | 80/67 | 81/67 | 1.194 | True |
| osc-f32/large | psram | 3/3 | 768/768 | 318/265 | 319/265 | 1.200 | True |
| osc-q15/realistic | psram | 3/3 | 768/768 | 28/25 | 28/25 | 1.120 | True |
| osc-q15/large | psram | 3/3 | 768/768 | 109/97 | 110/97 | 1.124 | True |
| filter-f32/realistic | psram | 3/3 | 768/768 | 75/62 | 75/62 | 1.210 | True |
| filter-f32/large | psram | 3/3 | 768/768 | 297/248 | 306/248 | 1.198 | True |
| filter-q15/realistic | psram | 3/3 | 768/768 | 50/44 | 50/44 | 1.136 | True |
| filter-q15/large | psram | 3/3 | 768/768 | 192/169 | 192/169 | 1.136 | True |
| modal-f32/realistic | psram | 3/3 | 768/768 | 196/175 | 197/175 | 1.120 | True |
| modal-f32/large | psram | 3/3 | 768/768 | 1916/1741 | 1924/1741 | 1.101 | True |
| modal-q15/realistic | psram | 3/3 | 768/768 | 304/275 | 305/276 | 1.105 | True |
| modal-q15/large | psram | 3/3 | 768/768 | 3713/3346 | 3715/3347 | 1.110 | True |
| fft-f32/realistic | psram | 3/3 | 768/768 | 297/282 | 306/282 | 1.053 | True |
| fft-f32/large | psram | 3/3 | 768/768 | 1405/1350 | 1414/1350 | 1.041 | True |
| cellular/realistic | psram | 3/3 | 768/768 | 1247/1026 | 1256/1028 | 1.215 | True |
| cellular/large | psram | 3/3 | 768/768 | 4977/4065 | 4979/4067 | 1.224 | True |
| particles/realistic | psram | 3/3 | 768/768 | 23/19 | 24/19 | 1.211 | True |
| particles/large | psram | 3/3 | 768/768 | 90/73 | 91/74 | 1.233 | True |
| feedback/realistic | psram | 3/3 | 768/768 | 1201/1069 | 1217/1083 | 1.123 | True |
| feedback/large | psram | 3/3 | 768/768 | 4489/4021 | 4528/4047 | 1.116 | True |
| stripe-rgb565/realistic | psram | 3/3 | 768/768 | 81/76 | 82/76 | 1.066 | True |
| stripe-rgb565/large | psram | 3/3 | 768/768 | 322/301 | 331/301 | 1.070 | True |

## Independent output checks

The comparison independently checks algorithm/precision/seed, size, work units, warmup and sample count before producing a ratio. It also compares every repetition's final fingerprint and output min/max/sum, reporting absolute and relative numeric-summary differences in JSON/CSV. A 32-bit fingerprint is not a cryptographic proof of complete numerical equivalence. Matching summaries do not bound every sample's error. Cross-chip float differences require the separate host double-precision/DFT correctness evidence; no tolerance is invented here.

## USB output recovery and observer scope

The completed S3 capture records **1** bounded post-record progress queries and **0** parse errors. Exact host tool digests: harness `31f38de0dc0af9abbfbb8efa90da255f52544bbb50a4135faff6927fdbca08e4`, shared capture module `a63355c5cbe7bea2f9df2a685aa8765e13ed2d234a0c7913d62e3f9d7eb81c4e`.

* `telemetry_write_failures`: first 1, last 1, observed delta 0 across 265 state records.
* `telemetry_short_writes`: first 0, last 0, observed delta 0 across 265 state records.
* Progress query while awaiting `kernel`: RX offset 212992, partial record 1403 bytes, no-RX interval 0.809 s; subsequent RX 1 bytes after 0.001 host seconds.

The optional query is sent only after the expected record's type tag has appeared. Kernel timing steps have finished before that record is emitted. No query is sent while a kernel is still computing with an empty pending buffer. Raw partial bytes are retained; malformed JSON fails rather than being discarded. This successful recovery is evidence for the bounded workaround in this capture, not proof of the underlying native-USB fault or a general firmware fix. Lifetime counters may already be nonzero at the first snapshot; their observed delta must not be substituted for an all-time zero-error claim.

## Earlier captures and memory identity

* `evidence/physical/s3-kernels-link-01/results.json`: **FAILED**, 0 valid cases; recorded failure `ValueError('Target psram_bytes differs from positively identified board metadata')`. Original JSON/hash and status remain unchanged.
* `evidence/physical/s3-kernels-link-02/results.json`: **FAILED**, 80 valid cases; recorded failure `TimeoutError("No target 'kernel' record for None after 180s")`. Original JSON/hash and status remain unchanged.
* `evidence/physical/s3-kernels-01/results.json`: **FAILED**, 5 valid cases; recorded failure `TimeoutError("No target 'state' record for None after 20s")`. Original JSON/hash and status remain unchanged.

The first peer-image attempt used a prior profile's initialized-PSRAM heap total (2,095,103 bytes). The new peer profile reports 2,095,087 bytes. The profile-specific receipt records that API total while retaining the physically identified 2 MiB nominal PSRAM package. That 16-byte heap-accounting difference is not evidence of a changed physical memory capacity. The rejected identity-only attempt supplies no performance result. The earlier USB-partial run remains a separate failed snapshot.

## Limits for VIBEWARE

Use the complete row's measured tails and actual memory placement when selecting local kernels. Do not transfer these step timings directly to fullscreen rendering or a 128-sample DAC deadline. No wireless speedup is established here: input/state transfer, actual link RTT/loss/goodput and remote scheduling must also fit the intended cadence. Temperature indications use different chip APIs and are not calibrated cross-chip thermometry. Physical C3 execution remains unmeasured.

Artifacts: [structured comparison](S3-COMPARISON.json), [flat numeric table](S3-COMPARISON.csv). Exact source/image/profile, timing samples, allocation capability receipts and failed streams remain in the separately archived physical captures.
