# Finite audio packet-stream evidence

Capture status **FAILED**; 48/48 attempted repetitions. Statuses `{'MEASURED': 25, 'FAILED': 23}`; analysis validation `{'MEASURED': 25, 'FAILED': 23}`.

[Raw structured source](../physical/audio-s3-pair-deferred-01/results.json), SHA256 `2f9b69490f7e008b50c52a464d94c10bd9159290ee0a2cdd1eca847c4be29f35`. Per-board redacted RX/target/event logs remain separate raw evidence.

This is the explicit deferred-metadata cohort. Matching successful start ACKs are required, with only the documented post-ACK formatter failure permitted. Both radios stop before export. Active channel/power-save readback is **NOT MEASURED**; checked SDK setup is inferred from the ACK/source and later PHY API status. Post-stop info describes released radios, and last_error/operation describe post-stop command status. Packet/PLC/error counters retain the finite workload observations.

- **cyd**: `kernel-s3-n4r2-audio`; source `ad9cbfd670e456de5e08fca19810fb93f7f1db47`; tree `49fcfc731717626ed1b102f9d4d4ef5f135b4dbb91f1e323f6853eaa2c357ea6`; image `db7a8db3115f061ee01b82850d679e8487467a05f0f220bab2d2957b87eb22b8`.
- **peer**: `kernel-s3-n4r2-audio`; source `ad9cbfd670e456de5e08fca19810fb93f7f1db47`; tree `49fcfc731717626ed1b102f9d4d4ef5f135b4dbb91f1e323f6853eaa2c357ea6`; image `db7a8db3115f061ee01b82850d679e8487467a05f0f220bab2d2957b87eb22b8`.

These are synthetic-source digital reconstruction measurements over ESP-NOW broadcast packets. Packetized PDM, when selected, is an optional comparison and does not implement the requested proprietary SDR TX/RX. There is no I2S/analog-output or synchronized one-way latency claim. PHY values are requests accepted by the firmware API, not over-air rate readback.

| Run | Direction / codec / Hz / channels / frames / PHY request | capture / analysis | source rendered / nonconcealed % | PLC of rendered % | accepted TX / admitted RX | nonconcealed PCM B/s |
| --- | --- | --- | --- | --- | --- | --- |
| a001_1 | peer→cyd pcm16 24000 2ch 8f 1M | MEASURED / MEASURED | 100.00 / 16.22 | 83.78 | 1654 / 1460 | 15573.3 |
| a002_2 | peer→cyd pcm16 24000 2ch 8f 1M | MEASURED / MEASURED | 100.00 / 18.36 | 81.64 | 1829 / 1652 | 17621.3 |
| a003_3 | peer→cyd pcm16 24000 2ch 8f 1M | MEASURED / MEASURED | 100.00 / 19.82 | 80.18 | 1929 / 1784 | 19029.3 |
| a004_1 | peer→cyd pcm16 24000 2ch 16f 1M | MEASURED / MEASURED | 100.00 / 31.98 | 68.02 | 1573 / 1439 | 30698.7 |
| a005_2 | peer→cyd pcm16 24000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a006_3 | peer→cyd pcm16 24000 2ch 16f 1M | MEASURED / MEASURED | 100.00 / 32.36 | 67.64 | 1589 / 1456 | 31061.3 |
| a007_1 | peer→cyd pcm16 24000 2ch 32f 1M | MEASURED / MEASURED | 100.00 / 50.58 | 49.42 | 1197 / 1138 | 48554.7 |
| a008_2 | peer→cyd pcm16 24000 2ch 32f 1M | MEASURED / MEASURED | 100.00 / 49.24 | 50.76 | 1195 / 1108 | 47274.7 |
| a009_3 | peer→cyd pcm16 24000 2ch 32f 1M | MEASURED / MEASURED | 100.00 / 48.62 | 51.38 | 1183 / 1094 | 46677.3 |
| a010_1 | peer→cyd pcm16 48000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a011_2 | peer→cyd pcm16 48000 2ch 16f 1M | MEASURED / MEASURED | 100.00 / 16.13 | 83.87 | 1591 / 1452 | 30976.0 |
| a012_3 | peer→cyd pcm16 48000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a013_1 | peer→cyd pcm16 24000 2ch 8f 6M | MEASURED / MEASURED | 100.00 / 51.78 | 48.22 | 4844 / 4660 | 49706.7 |
| a014_2 | peer→cyd pcm16 24000 2ch 8f 6M | MEASURED / MEASURED | 100.00 / 51.54 | 48.46 | 4821 / 4639 | 49482.7 |
| a015_3 | peer→cyd pcm16 24000 2ch 8f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a016_1 | peer→cyd pcm16 24000 2ch 16f 6M | MEASURED / MEASURED | 100.00 / 81.51 | 18.49 | 3811 / 3668 | 78250.7 |
| a017_2 | peer→cyd pcm16 24000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a018_3 | peer→cyd pcm16 24000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a019_1 | peer→cyd pcm16 24000 2ch 32f 6M | MEASURED / MEASURED | 100.00 / 86.62 | 13.38 | 2009 / 1949 | 83157.3 |
| a020_2 | peer→cyd pcm16 24000 2ch 32f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a021_3 | peer→cyd pcm16 24000 2ch 32f 6M | MEASURED / MEASURED | 100.00 / 87.56 | 12.44 | 2023 / 1970 | 84053.3 |
| a022_1 | peer→cyd pcm16 48000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a023_2 | peer→cyd pcm16 48000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a024_3 | peer→cyd pcm16 48000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a025_1 | cyd→peer pcm16 24000 2ch 8f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a026_2 | cyd→peer pcm16 24000 2ch 8f 1M | MEASURED / MEASURED | 100.00 / 20.18 | 79.82 | 1958 / 1816 | 19370.7 |
| a027_3 | cyd→peer pcm16 24000 2ch 8f 1M | MEASURED / MEASURED | 100.00 / 20.41 | 79.59 | 1957 / 1837 | 19594.7 |
| a028_1 | cyd→peer pcm16 24000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a029_2 | cyd→peer pcm16 24000 2ch 16f 1M | MEASURED / MEASURED | 100.00 / 34.18 | 65.82 | 1658 / 1538 | 32810.7 |
| a030_3 | cyd→peer pcm16 24000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a031_1 | cyd→peer pcm16 24000 2ch 32f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a032_2 | cyd→peer pcm16 24000 2ch 32f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a033_3 | cyd→peer pcm16 24000 2ch 32f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a034_1 | cyd→peer pcm16 48000 2ch 16f 1M | MEASURED / MEASURED | 100.00 / 16.51 | 83.49 | 1627 / 1486 | 31701.3 |
| a035_2 | cyd→peer pcm16 48000 2ch 16f 1M | MEASURED / MEASURED | 100.00 / 14.20 | 85.80 | 1661 / 1278 | 27264.0 |
| a036_3 | cyd→peer pcm16 48000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a037_1 | cyd→peer pcm16 24000 2ch 8f 6M | MEASURED / MEASURED | 100.00 / 55.88 | 44.12 | 5159 / 5029 | 53642.7 |
| a038_2 | cyd→peer pcm16 24000 2ch 8f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a039_3 | cyd→peer pcm16 24000 2ch 8f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a040_1 | cyd→peer pcm16 24000 2ch 16f 6M | MEASURED / MEASURED | 100.00 / 80.31 | 19.69 | 3801 / 3614 | 77098.7 |
| a041_2 | cyd→peer pcm16 24000 2ch 16f 6M | MEASURED / MEASURED | 100.00 / 82.53 | 17.47 | 3869 / 3714 | 79232.0 |
| a042_3 | cyd→peer pcm16 24000 2ch 16f 6M | MEASURED / MEASURED | 100.00 / 80.36 | 19.64 | 3792 / 3616 | 77141.3 |
| a043_1 | cyd→peer pcm16 24000 2ch 32f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a044_2 | cyd→peer pcm16 24000 2ch 32f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a045_3 | cyd→peer pcm16 24000 2ch 32f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a046_1 | cyd→peer pcm16 48000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a047_2 | cyd→peer pcm16 48000 2ch 16f 6M | MEASURED / MEASURED | 100.00 / 46.58 | 53.42 | 4373 / 4192 | 89429.3 |
| a048_3 | cyd→peer pcm16 48000 2ch 16f 6M | MEASURED / MEASURED | 100.00 / 47.40 | 52.60 | 4448 / 4266 | 91008.0 |

Coverage uses the complete scheduled source block count. Quality sums cover rendered blocks, including PLC; source blocks skipped before the receiver anchor or during obsolete-work discard remain coverage loss, not silent zero-error samples. TX API acceptance minus admitted RX fragments includes radio, callback queue, age/deadline and stale/future-window effects; it is not isolated RF loss.

| Run | source skipped / expired prep | RX queue drops / expired callback | stale / future / CRC | PLC blocks / skipped playout | max callback age / slot dwell / render lateness µs |
| --- | --- | --- | --- | --- | --- |
| a001_1 | 0 / 7345 | 0 / 0 | 192 / 0 / 0 | 7540 / 0 | 56 / 1312 / 167 |
| a002_2 | 0 / 7170 | 0 / 0 | 176 / 0 / 0 | 7348 / 0 | 66 / 1324 / 167 |
| a003_3 | 0 / 7071 | 0 / 0 | 144 / 0 / 0 | 7216 / 0 | 57 / 1334 / 168 |
| a004_1 | 0 / 2927 | 0 / 0 | 134 / 0 / 0 | 3061 / 0 | 98 / 1843 / 214 |
| a005_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a006_3 | 0 / 2910 | 0 / 0 | 133 / 0 / 0 | 3044 / 0 | 85 / 1840 / 186 |
| a007_1 | 0 / 1052 | 0 / 0 | 56 / 0 / 0 | 1112 / 0 | 116 / 3718 / 225 |
| a008_2 | 0 / 1055 | 0 / 0 | 87 / 0 / 0 | 1142 / 0 | 125 / 3214 / 228 |
| a009_3 | 0 / 1066 | 0 / 0 | 87 / 0 / 0 | 1156 / 0 | 121 / 3216 / 223 |
| a010_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a011_2 | 0 / 7408 | 0 / 0 | 138 / 0 / 0 | 7548 / 0 | 74 / 1351 / 186 |
| a012_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a013_1 | 0 / 4156 | 0 / 0 | 183 / 0 / 0 | 4340 / 0 | 75 / 1290 / 178 |
| a014_2 | 0 / 4178 | 0 / 0 | 179 / 0 / 0 | 4361 / 0 | 71 / 1258 / 165 |
| a015_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a016_1 | 0 / 688 | 0 / 0 | 142 / 0 / 0 | 832 / 0 | 144 / 2050 / 184 |
| a017_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a018_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a019_1 | 0 / 241 | 0 / 0 | 60 / 0 / 0 | 301 / 0 | 123 / 3348 / 227 |
| a020_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a021_3 | 0 / 227 | 0 / 0 | 53 / 0 / 0 | 280 / 0 | 123 / 3342 / 215 |
| a022_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a023_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a024_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a025_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a026_2 | 0 / 7041 | 0 / 0 | 141 / 0 / 0 | 7184 / 0 | 66 / 1337 / 171 |
| a027_3 | 0 / 7042 | 0 / 0 | 120 / 0 / 0 | 7163 / 0 | 66 / 1338 / 186 |
| a028_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a029_2 | 0 / 2842 | 0 / 0 | 120 / 0 / 0 | 2962 / 0 | 90 / 1838 / 195 |
| a030_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a031_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a032_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a033_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a034_1 | 0 / 7372 | 0 / 0 | 141 / 0 / 0 | 7514 / 0 | 72 / 1178 / 190 |
| a035_2 | 0 / 7338 | 0 / 0 | 91 / 292 / 0 | 7722 / 0 | 72 / 2522 / 187 |
| a036_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a037_1 | 0 / 3841 | 0 / 0 | 128 / 0 / 0 | 3971 / 0 | 73 / 1306 / 191 |
| a038_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a039_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a040_1 | 0 / 699 | 0 / 0 | 160 / 0 / 0 | 886 / 0 | 92 / 1981 / 187 |
| a041_2 | 0 / 631 | 0 / 0 | 142 / 0 / 0 | 786 / 0 | 84 / 1987 / 183 |
| a042_3 | 0 / 708 | 0 / 0 | 159 / 0 / 0 | 884 / 0 | 83 / 2045 / 186 |
| a043_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a044_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a045_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a046_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a047_2 | 0 / 4627 | 0 / 0 | 170 / 0 / 0 | 4808 / 0 | 86 / 1304 / 214 |
| a048_3 | 0 / 4551 | 0 / 0 | 162 / 0 / 0 | 4734 / 0 | 93 / 1314 / 193 |

The age/dwell/lateness values above use receiver-local scheduling. Elapsed encode/receive/render/send and quality-observer costs include preemption; their maxima are observed samples, not worst-case execution-time guarantees. Exact call counts, sums, callback outcomes, in-flight state and active-window heap minima remain in JSON/CSV.

| Run / channel | observed samples | unaligned digital error RMS / max (PCM16 LSB) | SNR dB | correlation |
| --- | --- | --- | --- | --- |
| a001_1 / 0 | 72000 | 12004.720 / 30880 | 1.246 | — |
| a001_1 / 1 | 72000 | 12536.560 / 31040 | 0.869 | — |
| a002_2 / 0 | 72000 | 11624.839 / 28960 | 1.525 | — |
| a002_2 / 1 | 72000 | 12265.134 / 30756 | 1.059 | — |
| a003_3 / 0 | 72000 | 11308.229 / 29647 | 1.765 | — |
| a003_3 / 1 | 72000 | 12139.059 / 30362 | 1.149 | — |
| a004_1 / 0 | 72000 | 10511.489 / 33326 | 2.399 | — |
| a004_1 / 1 | 72000 | 11330.156 / 35907 | 1.748 | — |
| a006_3 / 0 | 72000 | 10536.327 / 33951 | 2.379 | — |
| a006_3 / 1 | 72000 | 11301.696 / 35895 | 1.770 | — |
| a007_1 / 0 | 72000 | 9861.261 / 33951 | 2.954 | — |
| a007_1 / 1 | 72000 | 10427.998 / 35906 | 2.469 | — |
| a008_2 / 0 | 72000 | 10016.040 / 33952 | 2.819 | — |
| a008_2 / 1 | 72000 | 10714.211 / 35768 | 2.234 | — |
| a009_3 / 0 | 72000 | 9985.007 / 33775 | 2.846 | — |
| a009_3 / 1 | 72000 | 10636.597 / 35768 | 2.297 | — |
| a011_2 / 0 | 144000 | 11090.652 / 24000 | 1.934 | — |
| a011_2 / 1 | 144000 | 11569.906 / 24202 | 1.566 | — |
| a013_1 / 0 | 72000 | 7434.633 / 33951 | 5.408 | — |
| a013_1 / 1 | 72000 | 7920.741 / 35767 | 4.857 | — |
| a014_2 / 0 | 72000 | 7531.661 / 33775 | 5.295 | — |
| a014_2 / 1 | 72000 | 7851.929 / 35064 | 4.933 | — |
| a016_1 / 0 | 72000 | 6382.421 / 33952 | 6.733 | — |
| a016_1 / 1 | 72000 | 6486.780 / 35767 | 6.592 | — |
| a019_1 / 0 | 72000 | 5255.655 / 33952 | 8.420 | — |
| a019_1 / 1 | 72000 | 5443.715 / 35767 | 8.115 | — |
| a021_3 / 0 | 72000 | 5019.032 / 33326 | 8.820 | — |
| a021_3 / 1 | 72000 | 5267.073 / 35767 | 8.401 | — |
| a026_2 / 0 | 72000 | 11267.368 / 28783 | 1.796 | — |
| a026_2 / 1 | 72000 | 12146.516 / 31135 | 1.144 | — |
| a027_3 / 0 | 72000 | 11268.792 / 30142 | 1.795 | — |
| a027_3 / 1 | 72000 | 12082.732 / 30270 | 1.189 | — |
| a029_2 / 0 | 72000 | 10370.625 / 33327 | 2.517 | — |
| a029_2 / 1 | 72000 | 11107.861 / 35767 | 1.920 | — |
| a034_1 / 0 | 144000 | 10991.751 / 24000 | 2.011 | — |
| a034_1 / 1 | 144000 | 11518.005 / 23999 | 1.605 | — |
| a035_2 / 0 | 144000 | 11558.032 / 24000 | 1.575 | — |
| a035_2 / 1 | 144000 | 11895.161 / 24186 | 1.325 | — |
| a037_1 / 0 | 72000 | 6545.960 / 33951 | 6.513 | — |
| a037_1 / 1 | 72000 | 6890.204 / 35894 | 6.068 | — |
| a040_1 / 0 | 72000 | 6537.788 / 33952 | 6.524 | — |
| a040_1 / 1 | 72000 | 6742.814 / 35907 | 6.256 | — |
| a041_2 / 0 | 72000 | 6037.396 / 33951 | 7.216 | — |
| a041_2 / 1 | 72000 | 6298.474 / 35767 | 6.848 | — |
| a042_3 / 0 | 72000 | 6487.799 / 33951 | 6.591 | — |
| a042_3 / 1 | 72000 | 6708.034 / 35908 | 6.301 | — |
| a047_2 / 0 | 144000 | 6975.377 / 24000 | 5.961 | — |
| a047_2 / 1 | 144000 | 7191.148 / 24249 | 5.697 | — |
| a048_3 / 0 | 144000 | 6675.781 / 24000 | 6.343 | — |
| a048_3 / 1 | 144000 | 6960.526 / 24249 | 5.980 | — |

Zero observed error has unbounded mathematical SNR and is left unavailable rather than replaced by a finite number. Correlation is unavailable because the target exports energy/error sums without cross-moments. Reference comparison is unshifted and sequence-aligned; no fitting removes packet/PLC errors.

Only successful, validated repetitions with matching requested settings, actual codec/queue/PHY configuration and both source/image identities enter grouped min/median/max summaries. Failed sessions and unattempted repetitions retain their status. The offline continuous-PDM model has its own evidence class and artifacts.

- `a005_2`: Missing successful matching audio ACK; deferred mode aborted
- `a010_1`: Missing successful matching audio ACK; deferred mode aborted
- `a012_3`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a015_3`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a017_2`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a018_3`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a020_2`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a022_1`: Missing successful matching audio ACK; deferred mode aborted
- `a023_2`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a024_3`: Missing successful matching audio ACK; deferred mode aborted
- `a025_1`: Missing successful matching audio ACK; deferred mode aborted
- `a028_1`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a030_3`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a031_1`: Missing successful matching audio ACK; deferred mode aborted
- `a032_2`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a033_3`: Missing successful matching audio ACK; deferred mode aborted
- `a036_3`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a038_2`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a039_3`: Missing successful matching audio ACK; deferred mode aborted
- `a043_1`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a044_2`: Missing successful matching audio ACK; deferred mode aborted
- `a045_3`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a046_1`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
