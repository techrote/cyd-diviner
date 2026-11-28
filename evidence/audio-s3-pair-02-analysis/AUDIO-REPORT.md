# Finite audio packet-stream evidence

Capture status **FAILED**; 48/48 attempted repetitions. Statuses `{'FAILED': 26, 'MEASURED': 22}`; analysis validation `{'FAILED': 26, 'MEASURED': 22}`.

[Raw structured source](../physical/audio-s3-pair-02/results.json), SHA256 `a42e9881c8b3197170309d5095f4a9dce48969b7ace2494313d3acabbe5f127d`. Per-board redacted RX/target/event logs remain separate raw evidence.

- **cyd**: `kernel-s3-n4r2-audio`; source `ad9cbfd670e456de5e08fca19810fb93f7f1db47`; tree `49fcfc731717626ed1b102f9d4d4ef5f135b4dbb91f1e323f6853eaa2c357ea6`; image `db7a8db3115f061ee01b82850d679e8487467a05f0f220bab2d2957b87eb22b8`.
- **peer**: `kernel-s3-n4r2-audio`; source `ad9cbfd670e456de5e08fca19810fb93f7f1db47`; tree `49fcfc731717626ed1b102f9d4d4ef5f135b4dbb91f1e323f6853eaa2c357ea6`; image `db7a8db3115f061ee01b82850d679e8487467a05f0f220bab2d2957b87eb22b8`.

These are synthetic-source digital reconstruction measurements over ESP-NOW broadcast packets. Packetized PDM, when selected, is an optional comparison and does not implement the requested proprietary SDR TX/RX. There is no I2S/analog-output or synchronized one-way latency claim. PHY values are requests accepted by the firmware API, not over-air rate readback.

| Run | Direction / codec / Hz / channels / frames / PHY request | capture / analysis | source rendered / nonconcealed % | PLC of rendered % | accepted TX / admitted RX | nonconcealed PCM B/s |
| --- | --- | --- | --- | --- | --- | --- |
| a001_1 | peer→cyd pcm16 24000 2ch 8f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a002_2 | peer→cyd pcm16 24000 2ch 8f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a003_3 | peer→cyd pcm16 24000 2ch 8f 1M | MEASURED / MEASURED | 100.00 / 22.32 | 77.68 | 2108 / 2009 | 21429.3 |
| a004_1 | peer→cyd pcm16 24000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a005_2 | peer→cyd pcm16 24000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a006_3 | peer→cyd pcm16 24000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a007_1 | peer→cyd pcm16 24000 2ch 32f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a008_2 | peer→cyd pcm16 24000 2ch 32f 1M | MEASURED / MEASURED | 100.00 / 50.76 | 49.24 | 1221 / 1142 | 48725.3 |
| a009_3 | peer→cyd pcm16 24000 2ch 32f 1M | MEASURED / MEASURED | 100.00 / 50.40 | 49.60 | 1211 / 1134 | 48384.0 |
| a010_1 | peer→cyd pcm16 48000 2ch 16f 1M | MEASURED / MEASURED | 100.00 / 16.87 | 83.13 | 1655 / 1518 | 32384.0 |
| a011_2 | peer→cyd pcm16 48000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a012_3 | peer→cyd pcm16 48000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a013_1 | peer→cyd pcm16 24000 2ch 8f 6M | MEASURED / MEASURED | 100.00 / 58.43 | 41.57 | 5368 / 5259 | 56096.0 |
| a014_2 | peer→cyd pcm16 24000 2ch 8f 6M | MEASURED / MEASURED | 100.00 / 54.81 | 45.19 | 5094 / 4933 | 52618.7 |
| a015_3 | peer→cyd pcm16 24000 2ch 8f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a016_1 | peer→cyd pcm16 24000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a017_2 | peer→cyd pcm16 24000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a018_3 | peer→cyd pcm16 24000 2ch 16f 6M | MEASURED / MEASURED | 100.00 / 0.69 | 99.31 | 4016 / 31 | 661.3 |
| a019_1 | peer→cyd pcm16 24000 2ch 32f 6M | MEASURED / MEASURED | 100.00 / 88.84 | 11.16 | 2076 / 1999 | 85290.7 |
| a020_2 | peer→cyd pcm16 24000 2ch 32f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a021_3 | peer→cyd pcm16 24000 2ch 32f 6M | MEASURED / MEASURED | 100.00 / 87.87 | 12.13 | 2033 / 1977 | 84352.0 |
| a022_1 | peer→cyd pcm16 48000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a023_2 | peer→cyd pcm16 48000 2ch 16f 6M | MEASURED / MEASURED | 100.00 / 49.04 | 50.96 | 4572 / 4414 | 94165.3 |
| a024_3 | peer→cyd pcm16 48000 2ch 16f 6M | MEASURED / MEASURED | 100.00 / 46.62 | 53.38 | 4369 / 4196 | 89514.7 |
| a025_1 | cyd→peer pcm16 24000 2ch 8f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a026_2 | cyd→peer pcm16 24000 2ch 8f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a027_3 | cyd→peer pcm16 24000 2ch 8f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a028_1 | cyd→peer pcm16 24000 2ch 16f 1M | MEASURED / MEASURED | 100.00 / 32.67 | 67.33 | 1607 / 1470 | 31360.0 |
| a029_2 | cyd→peer pcm16 24000 2ch 16f 1M | MEASURED / MEASURED | 100.00 / 32.38 | 67.62 | 1596 / 1457 | 31082.7 |
| a030_3 | cyd→peer pcm16 24000 2ch 16f 1M | MEASURED / MEASURED | 100.00 / 33.51 | 66.49 | 1631 / 1508 | 32170.7 |
| a031_1 | cyd→peer pcm16 24000 2ch 32f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a032_2 | cyd→peer pcm16 24000 2ch 32f 1M | MEASURED / MEASURED | 100.00 / 49.64 | 50.36 | 1206 / 1117 | 47658.7 |
| a033_3 | cyd→peer pcm16 24000 2ch 32f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a034_1 | cyd→peer pcm16 48000 2ch 16f 1M | MEASURED / MEASURED | 100.00 / 16.70 | 83.30 | 1634 / 1503 | 32064.0 |
| a035_2 | cyd→peer pcm16 48000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a036_3 | cyd→peer pcm16 48000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a037_1 | cyd→peer pcm16 24000 2ch 8f 6M | MEASURED / MEASURED | 100.00 / 52.59 | 47.41 | 4886 / 4733 | 50485.3 |
| a038_2 | cyd→peer pcm16 24000 2ch 8f 6M | MEASURED / MEASURED | 99.97 / 54.16 | 45.83 | 5022 / 4874 | 51989.3 |
| a039_3 | cyd→peer pcm16 24000 2ch 8f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a040_1 | cyd→peer pcm16 24000 2ch 16f 6M | MEASURED / MEASURED | 100.00 / 82.96 | 17.04 | 3795 / 3733 | 79637.3 |
| a041_2 | cyd→peer pcm16 24000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a042_3 | cyd→peer pcm16 24000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a043_1 | cyd→peer pcm16 24000 2ch 32f 6M | MEASURED / MEASURED | 100.00 / 88.00 | 12.00 | 2035 / 1980 | 84480.0 |
| a044_2 | cyd→peer pcm16 24000 2ch 32f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a045_3 | cyd→peer pcm16 24000 2ch 32f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a046_1 | cyd→peer pcm16 48000 2ch 16f 6M | MEASURED / MEASURED | 100.00 / 47.78 | 52.22 | 4450 / 4300 | 91733.3 |
| a047_2 | cyd→peer pcm16 48000 2ch 16f 6M | MEASURED / MEASURED | 100.00 / 46.43 | 53.57 | 4346 / 4179 | 89152.0 |
| a048_3 | cyd→peer pcm16 48000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |

Coverage uses the complete scheduled source block count. Quality sums cover rendered blocks, including PLC; source blocks skipped before the receiver anchor or during obsolete-work discard remain coverage loss, not silent zero-error samples. TX API acceptance minus admitted RX fragments includes radio, callback queue, age/deadline and stale/future-window effects; it is not isolated RF loss.

| Run | source skipped / expired prep | RX queue drops / expired callback | stale / future / CRC | PLC blocks / skipped playout | max callback age / slot dwell / render lateness µs |
| --- | --- | --- | --- | --- | --- |
| a001_1 | 0 / 6960 | 0 / 0 | 111 / 0 / 0 | 7076 / 0 | 70 / 1337 / 170 |
| a002_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a003_3 | 0 / 6891 | 0 / 0 | 98 / 0 / 0 | 6991 / 0 | 57 / 1323 / 166 |
| a004_1 | 0 / 2740 | 0 / 0 | 92 / 0 / 0 | 2833 / 0 | 80 / 1846 / 194 |
| a005_2 | 0 / 2786 | 0 / 0 | 111 / 0 / 0 | 2898 / 0 | 82 / 1906 / 184 |
| a006_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a007_1 | 0 / 1034 | 0 / 0 | 82 / 0 / 0 | 1116 / 0 | 117 / 3191 / 228 |
| a008_2 | 0 / 1028 | 0 / 0 | 78 / 0 / 0 | 1108 / 0 | 120 / 3113 / 223 |
| a009_3 | 0 / 1039 | 0 / 0 | 77 / 0 / 0 | 1116 / 0 | 117 / 3182 / 220 |
| a010_1 | 0 / 7344 | 0 / 0 | 136 / 0 / 0 | 7482 / 0 | 71 / 1174 / 202 |
| a011_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a012_3 | 0 / 7298 | 0 / 0 | 86 / 0 / 0 | 7386 / 0 | 69 / 2385 / 211 |
| a013_1 | 0 / 3632 | 0 / 0 | 106 / 0 / 0 | 3741 / 0 | 69 / 1725 / 167 |
| a014_2 | 0 / 3905 | 0 / 0 | 158 / 0 / 0 | 4067 / 0 | 78 / 1231 / 175 |
| a015_3 | 0 / 3930 | 0 / 0 | 146 / 0 / 0 | 4083 / 0 | 74 / 1245 / 165 |
| a016_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a017_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a018_3 | 0 / 484 | 0 / 0 | 0 / 3891 / 0 | 4469 / 0 | 93 / 5253 / 209 |
| a019_1 | 0 / 174 | 0 / 0 | 25 / 0 / 0 | 251 / 0 | 109 / 3747 / 230 |
| a020_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a021_3 | 0 / 217 | 0 / 0 | 41 / 0 / 0 | 273 / 0 | 110 / 3292 / 221 |
| a022_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a023_2 | 0 / 4428 | 0 / 0 | 155 / 0 / 0 | 4586 / 0 | 83 / 1285 / 213 |
| a024_3 | 0 / 4631 | 0 / 0 | 169 / 0 / 0 | 4804 / 0 | 83 / 1306 / 191 |
| a025_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a026_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a027_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a028_1 | 0 / 2892 | 0 / 0 | 136 / 0 / 0 | 3030 / 0 | 83 / 1907 / 183 |
| a029_2 | 0 / 2903 | 0 / 0 | 137 / 0 / 0 | 3043 / 0 | 83 / 1905 / 192 |
| a030_3 | 0 / 2869 | 0 / 0 | 123 / 0 / 0 | 2992 / 0 | 95 / 1907 / 187 |
| a031_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a032_2 | 0 / 1044 | 0 / 0 | 89 / 0 / 0 | 1133 / 0 | 121 / 3181 / 235 |
| a033_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a034_1 | 0 / 7366 | 0 / 0 | 131 / 0 / 0 | 7497 / 0 | 84 / 1174 / 186 |
| a035_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a036_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a037_1 | 0 / 4114 | 0 / 0 | 147 / 0 / 0 | 4267 / 0 | 67 / 1287 / 168 |
| a038_2 | 0 / 3978 | 0 / 0 | 137 / 0 / 0 | 4123 / 0 | 76 / 1797 / 164 |
| a039_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a040_1 | 0 / 704 | 0 / 0 | 46 / 0 / 0 | 767 / 0 | 98 / 3579 / 182 |
| a041_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a042_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a043_1 | 0 / 215 | 0 / 0 | 46 / 0 / 0 | 270 / 0 | 120 / 3323 / 225 |
| a044_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a045_3 | 0 / 186 | 0 / 0 | 41 / 0 / 0 | 238 / 0 | 113 / 3338 / 223 |
| a046_1 | 0 / 4549 | 0 / 0 | 144 / 0 / 0 | 4700 / 0 | 86 / 1289 / 198 |
| a047_2 | 0 / 4653 | 0 / 0 | 154 / 0 / 0 | 4821 / 0 | 87 / 1312 / 184 |
| a048_3 | — / — | — / — | — / — / — | — / — | — / — / — |

The age/dwell/lateness values above use receiver-local scheduling. Elapsed encode/receive/render/send and quality-observer costs include preemption; their maxima are observed samples, not worst-case execution-time guarantees. Exact call counts, sums, callback outcomes, in-flight state and active-window heap minima remain in JSON/CSV.

| Run / channel | observed samples | unaligned digital error RMS / max (PCM16 LSB) | SNR dB | correlation |
| --- | --- | --- | --- | --- |
| a003_3 / 0 | 72000 | 10978.634 / 28784 | 2.022 | — |
| a003_3 / 1 | 72000 | 11838.548 / 30362 | 1.367 | — |
| a008_2 / 0 | 72000 | 9839.917 / 33951 | 2.973 | — |
| a008_2 / 1 | 72000 | 10575.161 / 35783 | 2.347 | — |
| a009_3 / 0 | 72000 | 9854.124 / 33952 | 2.960 | — |
| a009_3 / 1 | 72000 | 10544.067 / 35896 | 2.373 | — |
| a010_1 / 0 | 144000 | 10965.780 / 24000 | 2.032 | — |
| a010_1 / 1 | 144000 | 11443.789 / 24250 | 1.661 | — |
| a013_1 / 0 | 72000 | 5907.237 / 33951 | 7.405 | — |
| a013_1 / 1 | 72000 | 6279.855 / 35767 | 6.874 | — |
| a014_2 / 0 | 72000 | 6858.326 / 33775 | 6.108 | — |
| a014_2 / 1 | 72000 | 7181.105 / 35593 | 5.709 | — |
| a018_3 / 0 | 72000 | 13835.514 / 32703 | 0.013 | — |
| a018_3 / 1 | 72000 | 13867.930 / 34224 | -0.007 | — |
| a019_1 / 0 | 72000 | 4760.619 / 33952 | 9.279 | — |
| a019_1 / 1 | 72000 | 4975.324 / 35383 | 8.896 | — |
| a021_3 / 0 | 72000 | 5021.281 / 33952 | 8.816 | — |
| a021_3 / 1 | 72000 | 5127.194 / 35767 | 8.635 | — |
| a023_2 / 0 | 144000 | 6403.917 / 24000 | 6.704 | — |
| a023_2 / 1 | 144000 | 6691.918 / 24070 | 6.322 | — |
| a024_3 / 0 | 144000 | 6855.889 / 24000 | 6.111 | — |
| a024_3 / 1 | 144000 | 7066.931 / 23998 | 5.848 | — |
| a028_1 / 0 | 72000 | 10517.385 / 33775 | 2.395 | — |
| a028_1 / 1 | 72000 | 11295.342 / 35906 | 1.775 | — |
| a029_2 / 0 | 72000 | 10557.562 / 33952 | 2.361 | — |
| a029_2 / 1 | 72000 | 11272.308 / 35908 | 1.792 | — |
| a030_3 / 0 | 72000 | 10479.794 / 33952 | 2.426 | — |
| a030_3 / 1 | 72000 | 11110.141 / 35907 | 1.918 | — |
| a032_2 / 0 | 72000 | 9918.714 / 33951 | 2.904 | — |
| a032_2 / 1 | 72000 | 10751.761 / 35906 | 2.203 | — |
| a034_1 / 0 | 144000 | 11016.980 / 24000 | 1.991 | — |
| a034_1 / 1 | 144000 | 11498.795 / 24249 | 1.620 | — |
| a037_1 / 0 | 72000 | 7208.960 / 33775 | 5.675 | — |
| a037_1 / 1 | 72000 | 7596.496 / 35768 | 5.220 | — |
| a038_2 / 0 | 71976 | 6917.494 / 33952 | 6.034 | — |
| a038_2 / 1 | 71976 | 7293.397 / 35908 | 5.575 | — |
| a040_1 / 0 | 72000 | 6013.203 / 33952 | 7.251 | — |
| a040_1 / 1 | 72000 | 6165.196 / 35908 | 7.034 | — |
| a043_1 / 0 | 72000 | 4924.041 / 33951 | 8.986 | — |
| a043_1 / 1 | 72000 | 5171.294 / 35639 | 8.561 | — |
| a046_1 / 0 | 144000 | 6649.333 / 24000 | 6.377 | — |
| a046_1 / 1 | 144000 | 6842.284 / 24250 | 6.129 | — |
| a047_2 / 0 | 144000 | 6802.383 / 24000 | 6.180 | — |
| a047_2 / 1 | 144000 | 7102.385 / 24000 | 5.805 | — |

Zero observed error has unbounded mathematical SNR and is left unavailable rather than replaced by a finite number. Correlation is unavailable because the target exports energy/error sums without cross-moments. Reference comparison is unshifted and sequence-aligned; no fitting removes packet/PLC errors.

Only successful, validated repetitions with matching requested settings, actual codec/queue/PHY configuration and both source/image identities enter grouped min/median/max summaries. Failed sessions and unattempted repetitions retain their status. The offline continuous-PDM model has its own evidence class and artifacts.

- `a001_1`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a002_2`: Owned receiver did not report active channel6/PS-off/PHY-configured readiness
- `a004_1`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a005_2`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a006_3`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a007_1`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a011_2`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a012_3`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a015_3`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a016_1`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a017_2`: Owned receiver did not report active channel6/PS-off/PHY-configured readiness
- `a020_2`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a022_1`: Owned receiver did not report active channel6/PS-off/PHY-configured readiness
- `a025_1`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a026_2`: Owned receiver did not report active channel6/PS-off/PHY-configured readiness
- `a027_3`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a031_1`: Owned receiver did not report active channel6/PS-off/PHY-configured readiness
- `a033_3`: Owned receiver did not report active channel6/PS-off/PHY-configured readiness
- `a035_2`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a036_3`: Owned receiver did not report active channel6/PS-off/PHY-configured readiness
- `a039_3`: Owned receiver did not report active channel6/PS-off/PHY-configured readiness
- `a041_2`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a042_3`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a044_2`: Owned receiver did not report active channel6/PS-off/PHY-configured readiness
- `a045_3`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a048_3`: Owned receiver did not report active channel6/PS-off/PHY-configured readiness
