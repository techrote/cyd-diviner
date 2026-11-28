# Finite audio packet-stream evidence

Capture status **FAILED**; 48/48 attempted repetitions. Statuses `{'MEASURED': 2, 'FAILED': 46}`; analysis validation `{'MEASURED': 2, 'FAILED': 46}`.

[Raw structured source](../physical/audio-bidirectional-deferred-01/results.json), SHA256 `9178f60e650c50ebbdfe53cfa285e1810c7161ca19f3e6e7296443a7311781fb`. Per-board redacted RX/target/event logs remain separate raw evidence.

This is the explicit deferred-metadata cohort. Matching successful start ACKs are required, with only the documented post-ACK formatter failure permitted. Both radios stop before export. Active channel/power-save readback is **NOT MEASURED**; checked SDK setup is inferred from the ACK/source and later PHY API status. Post-stop info describes released radios, and last_error/operation describe post-stop command status. Packet/PLC/error counters retain the finite workload observations.

- **cyd**: `cyd-profile-audio`; source `ad9cbfd670e456de5e08fca19810fb93f7f1db47`; tree `49fcfc731717626ed1b102f9d4d4ef5f135b4dbb91f1e323f6853eaa2c357ea6`; image `407eb96f8fe2b74ed1cf45f4a163558eacde0ff287a4b878a9ed1faadfaccc93`.
- **peer**: `kernel-s3-n4r2-audio`; source `ad9cbfd670e456de5e08fca19810fb93f7f1db47`; tree `49fcfc731717626ed1b102f9d4d4ef5f135b4dbb91f1e323f6853eaa2c357ea6`; image `db7a8db3115f061ee01b82850d679e8487467a05f0f220bab2d2957b87eb22b8`.

These are synthetic-source digital reconstruction measurements over ESP-NOW broadcast packets. Packetized PDM, when selected, is an optional comparison and does not implement the requested proprietary SDR TX/RX. There is no I2S/analog-output or synchronized one-way latency claim. PHY values are requests accepted by the firmware API, not over-air rate readback.

| Run | Direction / codec / Hz / channels / frames / PHY request | capture / analysis | source rendered / nonconcealed % | PLC of rendered % | accepted TX / admitted RX | nonconcealed PCM B/s |
| --- | --- | --- | --- | --- | --- | --- |
| a001_1 | peer→cyd pcm16 24000 2ch 8f 1M | MEASURED / MEASURED | 100.00 / 21.96 | 78.04 | 2076 / 1976 | 21077.3 |
| a002_2 | peer→cyd pcm16 24000 2ch 8f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a003_3 | peer→cyd pcm16 24000 2ch 8f 1M | MEASURED / MEASURED | 100.00 / 22.50 | 77.50 | 2110 / 2025 | 21600.0 |
| a004_1 | peer→cyd pcm16 24000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a005_2 | peer→cyd pcm16 24000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a006_3 | peer→cyd pcm16 24000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a007_1 | peer→cyd pcm16 24000 2ch 32f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a008_2 | peer→cyd pcm16 24000 2ch 32f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a009_3 | peer→cyd pcm16 24000 2ch 32f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a010_1 | peer→cyd pcm16 48000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a011_2 | peer→cyd pcm16 48000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a012_3 | peer→cyd pcm16 48000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a013_1 | peer→cyd pcm16 24000 2ch 8f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a014_2 | peer→cyd pcm16 24000 2ch 8f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a015_3 | peer→cyd pcm16 24000 2ch 8f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a016_1 | peer→cyd pcm16 24000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a017_2 | peer→cyd pcm16 24000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a018_3 | peer→cyd pcm16 24000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a019_1 | peer→cyd pcm16 24000 2ch 32f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a020_2 | peer→cyd pcm16 24000 2ch 32f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a021_3 | peer→cyd pcm16 24000 2ch 32f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a022_1 | peer→cyd pcm16 48000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a023_2 | peer→cyd pcm16 48000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a024_3 | peer→cyd pcm16 48000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a025_1 | cyd→peer pcm16 24000 2ch 8f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a026_2 | cyd→peer pcm16 24000 2ch 8f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a027_3 | cyd→peer pcm16 24000 2ch 8f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a028_1 | cyd→peer pcm16 24000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a029_2 | cyd→peer pcm16 24000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a030_3 | cyd→peer pcm16 24000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a031_1 | cyd→peer pcm16 24000 2ch 32f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a032_2 | cyd→peer pcm16 24000 2ch 32f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a033_3 | cyd→peer pcm16 24000 2ch 32f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a034_1 | cyd→peer pcm16 48000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a035_2 | cyd→peer pcm16 48000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a036_3 | cyd→peer pcm16 48000 2ch 16f 1M | FAILED / FAILED | — / — | — | — / — | — |
| a037_1 | cyd→peer pcm16 24000 2ch 8f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a038_2 | cyd→peer pcm16 24000 2ch 8f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a039_3 | cyd→peer pcm16 24000 2ch 8f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a040_1 | cyd→peer pcm16 24000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a041_2 | cyd→peer pcm16 24000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a042_3 | cyd→peer pcm16 24000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a043_1 | cyd→peer pcm16 24000 2ch 32f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a044_2 | cyd→peer pcm16 24000 2ch 32f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a045_3 | cyd→peer pcm16 24000 2ch 32f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a046_1 | cyd→peer pcm16 48000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a047_2 | cyd→peer pcm16 48000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |
| a048_3 | cyd→peer pcm16 48000 2ch 16f 6M | FAILED / FAILED | — / — | — | — / — | — |

Coverage uses the complete scheduled source block count. Quality sums cover rendered blocks, including PLC; source blocks skipped before the receiver anchor or during obsolete-work discard remain coverage loss, not silent zero-error samples. TX API acceptance minus admitted RX fragments includes radio, callback queue, age/deadline and stale/future-window effects; it is not isolated RF loss.

| Run | source skipped / expired prep | RX queue drops / expired callback | stale / future / CRC | PLC blocks / skipped playout | max callback age / slot dwell / render lateness µs |
| --- | --- | --- | --- | --- | --- |
| a001_1 | 0 / 6923 | 0 / 0 | 100 / 0 / 0 | 7024 / 0 | 226 / 2190 / 307 |
| a002_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a003_3 | 0 / 6890 | 0 / 0 | 85 / 0 / 0 | 6975 / 0 | 179 / 2210 / 357 |
| a004_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a005_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a006_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a007_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a008_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a009_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a010_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a011_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a012_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a013_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a014_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a015_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a016_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a017_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a018_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a019_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a020_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a021_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a022_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a023_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a024_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a025_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a026_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a027_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a028_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a029_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a030_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a031_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a032_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a033_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a034_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a035_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a036_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a037_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a038_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a039_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a040_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a041_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a042_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a043_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a044_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a045_3 | — / — | — / — | — / — / — | — / — | — / — / — |
| a046_1 | — / — | — / — | — / — / — | — / — | — / — / — |
| a047_2 | — / — | — / — | — / — / — | — / — | — / — / — |
| a048_3 | — / — | — / — | — / — / — | — / — | — / — / — |

The age/dwell/lateness values above use receiver-local scheduling. Elapsed encode/receive/render/send and quality-observer costs include preemption; their maxima are observed samples, not worst-case execution-time guarantees. Exact call counts, sums, callback outcomes, in-flight state and active-window heap minima remain in JSON/CSV.

| Run / channel | observed samples | unaligned digital error RMS / max (PCM16 LSB) | SNR dB | correlation |
| --- | --- | --- | --- | --- |
| a001_1 / 0 | 72000 | 11000.342 / 30830 | 2.005 | — |
| a001_1 / 1 | 72000 | 11993.893 / 31483 | 1.254 | — |
| a003_3 / 0 | 72000 | 10958.065 / 30206 | 2.038 | — |
| a003_3 / 1 | 72000 | 11887.092 / 30462 | 1.331 | — |

Zero observed error has unbounded mathematical SNR and is left unavailable rather than replaced by a finite number. Correlation is unavailable because the target exports energy/error sums without cross-moments. Reference comparison is unshifted and sequence-aligned; no fitting removes packet/PLC errors.

Only successful, validated repetitions with matching requested settings, actual codec/queue/PHY configuration and both source/image identities enter grouped min/median/max summaries. Failed sessions and unattempted repetitions retain their status. The offline continuous-PDM model has its own evidence class and artifacts.

- `a002_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 7.28822799999034}
- `a004_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 13.963152799988165}
- `a005_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 14.98758600000292}
- `a006_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 15.977598900033627}
- `a007_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 16.962189799989574}
- `a008_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 17.985625400033314}
- `a009_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 19.048641700006556}
- `a010_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 20.073667899996508}
- `a011_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 21.097667400026694}
- `a012_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 22.12627180002164}
- `a013_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 23.14827470004093}
- `a014_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 24.209289800026454}
- `a015_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 25.26372280000942}
- `a016_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 26.321318500034977}
- `a017_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 27.415325300011318}
- `a018_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 28.478339400026016}
- `a019_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 29.576371800038032}
- `a020_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 30.67440680001164}
- `a021_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 31.810383300005924}
- `a022_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 32.90740130003542}
- `a023_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 34.04340710001998}
- `a024_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 35.141428700007964}
- `a025_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 36.31099170004018}
- `a026_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 37.59188449999783}
- `a027_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 38.76389260002179}
- `a028_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 39.93470059998799}
- `a029_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 41.10650490003172}
- `a030_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 42.24152010004036}
- `a031_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 43.561184699996375}
- `a032_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 44.7336516000214}
- `a033_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 45.904563900025096}
- `a034_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 49.112606599985156}
- `a035_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 50.43162270000903}
- `a036_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 51.63763509999262}
- `a037_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 52.99565709999297}
- `a038_2`: Audio endpoint acknowledgment/summary timeout; raw evidence retained
- `a039_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 69.87687680003}
- `a040_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 71.19489660003455}
- `a041_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 73.25391450000461}
- `a042_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 74.4969345999998}
- `a043_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 75.78039840003476}
- `a044_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 77.09742270002607}
- `a045_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 78.45142530003795}
- `a046_1`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 79.8084710999974}
- `a047_2`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 81.24145150004188}
- `a048_3`: Deferred metadata rejected diagnostic: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 82.63248630001908}
