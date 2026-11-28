# Finite audio packet-stream evidence

Capture status **CAPTURE COMPLETE**; 1/1 attempted repetitions. Statuses `{'MEASURED': 1}`; analysis validation `{'MEASURED': 1}`.

[Raw structured source](../physical/audio-deferred-fresh-01/results.json), SHA256 `819ec4dee0e661ea2039c2f26d580bf580a7ec3825927b55a5193bbb9f7f7497`. Per-board redacted RX/target/event logs remain separate raw evidence.

This is the explicit deferred-metadata cohort. Matching successful start ACKs are required, with only the documented post-ACK formatter failure permitted. Both radios stop before export. Active channel/power-save readback is **NOT MEASURED**; checked SDK setup is inferred from the ACK/source and later PHY API status. Post-stop info describes released radios, and last_error/operation describe post-stop command status. Packet/PLC/error counters retain the finite workload observations.

- **cyd**: `cyd-profile-audio`; source `ad9cbfd670e456de5e08fca19810fb93f7f1db47`; tree `49fcfc731717626ed1b102f9d4d4ef5f135b4dbb91f1e323f6853eaa2c357ea6`; image `407eb96f8fe2b74ed1cf45f4a163558eacde0ff287a4b878a9ed1faadfaccc93`.
- **peer**: `kernel-s3-n4r2-audio`; source `ad9cbfd670e456de5e08fca19810fb93f7f1db47`; tree `49fcfc731717626ed1b102f9d4d4ef5f135b4dbb91f1e323f6853eaa2c357ea6`; image `db7a8db3115f061ee01b82850d679e8487467a05f0f220bab2d2957b87eb22b8`.

These are synthetic-source digital reconstruction measurements over ESP-NOW broadcast packets. Packetized PDM, when selected, is an optional comparison and does not implement the requested proprietary SDR TX/RX. There is no I2S/analog-output or synchronized one-way latency claim. PHY values are requests accepted by the firmware API, not over-air rate readback.

| Run | Direction / codec / Hz / channels / frames / PHY request | capture / analysis | source rendered / nonconcealed % | PLC of rendered % | accepted TX / admitted RX | nonconcealed PCM B/s |
| --- | --- | --- | --- | --- | --- | --- |
| a001_1 | peer→cyd pcm16 24000 2ch 8f 1M | MEASURED / MEASURED | 100.00 / 19.51 | 80.49 | 1896 / 1756 | 18730.7 |

Coverage uses the complete scheduled source block count. Quality sums cover rendered blocks, including PLC; source blocks skipped before the receiver anchor or during obsolete-work discard remain coverage loss, not silent zero-error samples. TX API acceptance minus admitted RX fragments includes radio, callback queue, age/deadline and stale/future-window effects; it is not isolated RF loss.

| Run | source skipped / expired prep | RX queue drops / expired callback | stale / future / CRC | PLC blocks / skipped playout | max callback age / slot dwell / render lateness µs |
| --- | --- | --- | --- | --- | --- |
| a001_1 | 0 / 7103 | 0 / 0 | 138 / 0 / 0 | 7244 / 0 | 221 / 2402 / 384 |

The age/dwell/lateness values above use receiver-local scheduling. Elapsed encode/receive/render/send and quality-observer costs include preemption; their maxima are observed samples, not worst-case execution-time guarantees. Exact call counts, sums, callback outcomes, in-flight state and active-window heap minima remain in JSON/CSV.

| Run / channel | observed samples | unaligned digital error RMS / max (PCM16 LSB) | SNR dB | correlation |
| --- | --- | --- | --- | --- |
| a001_1 / 0 | 72000 | 11465.928 / 29648 | 1.645 | — |
| a001_1 / 1 | 72000 | 12170.815 / 30589 | 1.126 | — |

Zero observed error has unbounded mathematical SNR and is left unavailable rather than replaced by a finite number. Correlation is unavailable because the target exports energy/error sums without cross-moments. Reference comparison is unshifted and sequence-aligned; no fitting removes packet/PLC errors.

Only successful, validated repetitions with matching requested settings, actual codec/queue/PHY configuration and both source/image identities enter grouped min/median/max summaries. Failed sessions and unattempted repetitions retain their status. The offline continuous-PDM model has its own evidence class and artifacts.

