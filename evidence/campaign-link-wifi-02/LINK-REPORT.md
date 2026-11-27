# Owned-board wireless evidence

Source capture status: **FAILED**. Execution reached plan end: **True**. Run statuses: `{'MEASURED': 37, 'FAILED': 29}`. Input SHA256 `14563e4539e86e77d35a4f22c50af574f5278e22efc10113c19a0d93dafb471a`.

[Raw capture archive](../PHYSICAL-CAPTURES-SUPPLEMENT.zip) (member `physical/link-wifi-02/results.json`); redacted per-board RX/target/event logs and image/flash receipts remain separate evidence.

* **cyd**: `cyd-profile-link`, source `174a79670d4b0da656ebd615f0cd1620aa0ddbb1`, source digest `1b7d5aacd09d257dff759f95413047b74fe2b6de7592b7a3f147e39aea0c07ed`, binary SHA256 `a980f7e2b9409686ae2d325ebeddfd525966f06e8ded03b2d8a723b36771ab11`.
* **peer**: `kernel-s3-n4r2-peer`, source `174a79670d4b0da656ebd615f0cd1620aa0ddbb1`, source digest `1b7d5aacd09d257dff759f95413047b74fe2b6de7592b7a3f147e39aea0c07ed`, binary SHA256 `7ac5bf7f79bb2ed6048a40367fe1d3966a1d5e53a999b2f09f28f310456bb681`.

Only matching settings/source/image/allocation/connection contexts are aggregated. Goodput is unique application payload per initiator target second, excluding the 40-byte header and PHY overhead. RTT p50/p95/p99 values below are histogram upper bounds capped at the exact observed maximum; p99 is absent below 100 observations. The displayed not-observed fraction is an end-to-end counter difference, not isolated RF loss. A 64-sequence horizon, callback queues and the receive tail limit attribution.

## Per-repetition observations

| Run | Workload | link/profile status | received B/s | API/peer unique | not seen fwd/RTT % | RTT n / p95≤ / max µs | CYD FPS |
| --- | --- | --- | --- | --- | --- | --- | --- |
| l001_1 | direct_espnow/menu/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 29.60 |
| l002_2 | direct_espnow/menu/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 29.60 |
| l003_3 | direct_espnow/menu/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 29.60 |
| l004_1 | direct_espnow/menu/rtt | MEASURED/MEASURED | 3059.20 | 478/478 | 0.00/0.00 | 478/65536/93019 | 29.30 |
| l005_2 | direct_espnow/menu/rtt | MEASURED/MEASURED | 3059.20 | 478/478 | 0.00/0.00 | 478/62019/62019 | 29.30 |
| l006_3 | direct_espnow/menu/rtt | MEASURED/MEASURED | 3059.20 | 478/478 | 0.00/0.00 | 478/65536/78991 | 29.30 |
| l007_1 | direct_espnow/menu/espnow_paced | MEASURED/MEASURED | 17100.00 | 855/855 | 0.00/— | 0/—/— | 28.90 |
| l008_2 | direct_espnow/menu/espnow_paced | MEASURED/MEASURED | 15660.00 | 783/783 | 0.00/— | 0/—/— | 28.90 |
| l009_3 | direct_espnow/menu/espnow_paced | MEASURED/MEASURED | 12880.00 | 644/644 | 0.00/— | 0/—/— | 29.10 |
| l010_1 | direct_espnow/full_gate_drone/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 22.20 |
| l011_2 | direct_espnow/full_gate_drone/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 22.20 |
| l012_3 | direct_espnow/full_gate_drone/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 22.20 |
| l013_1 | direct_espnow/full_gate_drone/rtt | MEASURED/MEASURED | 3059.20 | 478/478 | 0.00/0.00 | 478/32768/36032 | 19.10 |
| l014_2 | direct_espnow/full_gate_drone/rtt | MEASURED/MEASURED | 3052.80 | 477/477 | 0.00/0.00 | 477/32768/86024 | 19.00 |
| l015_3 | direct_espnow/full_gate_drone/rtt | MEASURED/MEASURED | 3052.80 | 477/477 | 0.00/0.00 | 477/32768/46025 | 19.00 |
| l016_1 | direct_espnow/full_gate_drone/espnow_paced | MEASURED/MEASURED | 26000.00 | 1300/1300 | 0.00/— | 0/—/— | 18.10 |
| l017_2 | direct_espnow/full_gate_drone/espnow_paced | MEASURED/MEASURED | 25200.00 | 1260/1260 | 0.00/— | 0/—/— | 18.20 |
| l018_3 | direct_espnow/full_gate_drone/espnow_paced | MEASURED/MEASURED | 25860.00 | 1293/1293 | 0.00/— | 0/—/— | 18.20 |
| l019_1 | direct_softap/menu/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 29.60 |
| l020_2 | direct_softap/menu/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 29.60 |
| l021_3 | direct_softap/menu/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 29.60 |
| l022_1 | direct_softap/menu/rtt | MEASURED/MEASURED | 5792.00 | 911/905 | 0.66/0.88 | 903/131072/158122 | 28.30 |
| l023_2 | direct_softap/menu/rtt | MEASURED/MEASURED | 5753.60 | 911/899 | 1.32/1.43 | 898/131072/152219 | 28.30 |
| l024_3 | direct_softap/menu/rtt | MEASURED/MEASURED | 5811.20 | 908/908 | 0.00/0.00 | 908/116096/116096 | 28.20 |
| l025_1 | direct_softap/menu/udp_paced | MEASURED/MEASURED | 157286.40 | 1587/1536 | 3.21/— | 0/—/— | 28.70 |
| l026_2 | direct_softap/menu/udp_paced | MEASURED/MEASURED | 151142.40 | 1515/1476 | 2.57/— | 0/—/— | 28.60 |
| l027_3 | direct_softap/menu/udp_paced | MEASURED/MEASURED | 137728.00 | 1459/1345 | 7.81/— | 0/—/— | 28.60 |
| l028_1 | direct_softap/menu/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l029_2 | direct_softap/menu/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l030_3 | direct_softap/menu/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l031_1 | direct_softap/full_gate_drone/idle | FAILED/MEASURED | — | —/— | —/— | —/—/— | 22.20 |
| l032_2 | direct_softap/full_gate_drone/idle | FAILED/MEASURED | — | —/— | —/— | —/—/— | 22.10 |
| l033_3 | direct_softap/full_gate_drone/idle | FAILED/MEASURED | — | —/— | —/— | —/—/— | 22.20 |
| l034_1 | direct_softap/full_gate_drone/rtt | FAILED/MEASURED | — | —/— | —/— | —/—/— | 18.30 |
| l035_2 | direct_softap/full_gate_drone/rtt | FAILED/MEASURED | — | —/— | —/— | —/—/— | 17.50 |
| l036_3 | direct_softap/full_gate_drone/rtt | FAILED/MEASURED | — | —/— | —/— | —/—/— | 17.60 |
| l037_1 | direct_softap/full_gate_drone/udp_paced | FAILED/MEASURED | — | —/— | —/— | —/—/— | 18.60 |
| l038_2 | direct_softap/full_gate_drone/udp_paced | FAILED/MEASURED | 0.00 | 0/0 | —/— | 0/—/— | 18.20 |
| l039_3 | direct_softap/full_gate_drone/udp_paced | FAILED/MEASURED | 0.00 | 0/0 | —/— | 0/—/— | 18.30 |
| l040_1 | direct_softap/full_gate_drone/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l041_2 | direct_softap/full_gate_drone/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l042_3 | direct_softap/full_gate_drone/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l043_1 | through_ap/menu/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 29.60 |
| l044_2 | through_ap/menu/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 29.60 |
| l045_3 | through_ap/menu/idle | MEASURED/MEASURED | — | —/— | —/— | —/—/— | 29.60 |
| l046_1 | through_ap/menu/rtt | MEASURED/MEASURED | 4448.00 | 868/695 | 19.93/21.66 | 680/500246/500246 | 28.50 |
| l047_2 | through_ap/menu/rtt | MEASURED/MEASURED | 4550.40 | 893/711 | 20.38/22.28 | 694/262144/559205 | 28.50 |
| l048_3 | through_ap/menu/rtt | MEASURED/MEASURED | 4332.80 | 898/677 | 24.61/25.39 | 670/524288/568765 | 28.60 |
| l049_1 | through_ap/menu/udp_paced | MEASURED/MEASURED | 105676.80 | 1038/1032 | 0.58/— | 0/—/— | 28.60 |
| l050_2 | through_ap/menu/udp_paced | MEASURED/MEASURED | 99635.20 | 992/973 | 1.92/— | 0/—/— | 28.60 |
| l051_3 | through_ap/menu/udp_paced | MEASURED/MEASURED | 90419.20 | 892/883 | 1.01/— | 0/—/— | 28.70 |
| l052_1 | through_ap/menu/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l053_2 | through_ap/menu/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l054_3 | through_ap/menu/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l055_1 | through_ap/full_gate_drone/idle | FAILED/MEASURED | — | —/— | —/— | —/—/— | 22.20 |
| l056_2 | through_ap/full_gate_drone/idle | FAILED/MEASURED | — | —/— | —/— | —/—/— | 22.10 |
| l057_3 | through_ap/full_gate_drone/idle | FAILED/MEASURED | — | —/— | —/— | —/—/— | 22.20 |
| l058_1 | through_ap/full_gate_drone/rtt | FAILED/MEASURED | — | —/— | —/— | —/—/— | 17.70 |
| l059_2 | through_ap/full_gate_drone/rtt | FAILED/MEASURED | — | —/— | —/— | —/—/— | 17.70 |
| l060_3 | through_ap/full_gate_drone/rtt | FAILED/MEASURED | — | —/— | —/— | —/—/— | 17.80 |
| l061_1 | through_ap/full_gate_drone/udp_paced | FAILED/MEASURED | — | —/— | —/— | —/—/— | 18.60 |
| l062_2 | through_ap/full_gate_drone/udp_paced | MEASURED/MEASURED | 117043.20 | 1150/1143 | 0.61/— | 0/—/— | 18.00 |
| l063_3 | through_ap/full_gate_drone/udp_paced | FAILED/MEASURED | — | —/— | —/— | —/—/— | 18.60 |
| l064_1 | through_ap/full_gate_drone/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l065_2 | through_ap/full_gate_drone/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |
| l066_3 | through_ap/full_gate_drone/tcp_goodput | FAILED/NOT MEASURED | — | —/— | —/— | —/—/— | — |

Idle cells are intentionally unavailable for RTT/goodput/loss. FAILED/BLOCKED traffic cases do not enter the successful-link aggregates; a completed local profiler window remains separately inspectable when peer traffic fails.

| Run | sender API errors | receiver dup/reordered/too-old | CRC CYD/peer | callback drops CYD/peer (session incl warmup) |
| --- | --- | --- | --- | --- |
| l001_1 | — | —/—/— | —/— | —/— |
| l002_2 | — | —/—/— | —/— | —/— |
| l003_3 | — | —/—/— | —/— | —/— |
| l004_1 | 0 | 0/0/0 | 0/0 | 0/0 |
| l005_2 | 0 | 0/0/0 | 0/0 | 0/0 |
| l006_3 | 0 | 0/0/0 | 0/0 | 0/0 |
| l007_1 | 857 | 0/0/0 | 0/0 | 0/0 |
| l008_2 | 929 | 0/0/0 | 0/0 | 0/0 |
| l009_3 | 1054 | 0/0/0 | 0/0 | 0/0 |
| l010_1 | — | —/—/— | —/— | —/— |
| l011_2 | — | —/—/— | —/— | —/— |
| l012_3 | — | —/—/— | —/— | —/— |
| l013_1 | 0 | 0/0/0 | 0/0 | 0/0 |
| l014_2 | 0 | 0/0/0 | 0/0 | 0/0 |
| l015_3 | 0 | 0/0/0 | 0/0 | 0/0 |
| l016_1 | 382 | 0/0/0 | 0/0 | 0/0 |
| l017_2 | 421 | 0/0/0 | 0/0 | 0/0 |
| l018_3 | 376 | 0/0/0 | 0/0 | 0/0 |
| l019_1 | — | —/—/— | —/— | —/— |
| l020_2 | — | —/—/— | —/— | —/— |
| l021_3 | — | —/—/— | —/— | —/— |
| l022_1 | 0 | 0/0/0 | 0/0 | 0/0 |
| l023_2 | 0 | 0/0/0 | 0/0 | 0/0 |
| l024_3 | 0 | 0/0/0 | 0/0 | 0/0 |
| l025_1 | 108 | 0/0/0 | 0/0 | 0/0 |
| l026_2 | 195 | 0/0/0 | 0/0 | 0/0 |
| l027_3 | 248 | 0/0/0 | 0/0 | 0/0 |
| l028_1 | — | —/—/— | —/— | —/— |
| l029_2 | — | —/—/— | —/— | —/— |
| l030_3 | — | —/—/— | —/— | —/— |
| l031_1 | — | —/—/— | —/— | —/— |
| l032_2 | — | —/—/— | —/— | —/— |
| l033_3 | — | —/—/— | —/— | —/— |
| l034_1 | — | —/—/— | —/— | —/— |
| l035_2 | — | —/—/— | —/— | —/— |
| l036_3 | — | —/—/— | —/— | —/— |
| l037_1 | — | —/—/— | —/— | —/— |
| l038_2 | 1671 | 0/0/0 | 0/0 | 0/0 |
| l039_3 | 1671 | 0/0/0 | 0/0 | 0/0 |
| l040_1 | — | —/—/— | —/— | —/— |
| l041_2 | — | —/—/— | —/— | —/— |
| l042_3 | — | —/—/— | —/— | —/— |
| l043_1 | — | —/—/— | —/— | —/— |
| l044_2 | — | —/—/— | —/— | —/— |
| l045_3 | — | —/—/— | —/— | —/— |
| l046_1 | 47 | 0/0/0 | 0/0 | 0/0 |
| l047_2 | 24 | 0/0/0 | 0/0 | 0/0 |
| l048_3 | 15 | 0/1/0 | 0/0 | 0/0 |
| l049_1 | 681 | 0/0/0 | 0/0 | 0/0 |
| l050_2 | 728 | 0/0/0 | 0/0 | 0/0 |
| l051_3 | 837 | 0/0/0 | 0/0 | 0/0 |
| l052_1 | — | —/—/— | —/— | —/— |
| l053_2 | — | —/—/— | —/— | —/— |
| l054_3 | — | —/—/— | —/— | —/— |
| l055_1 | — | —/—/— | —/— | —/— |
| l056_2 | — | —/—/— | —/— | —/— |
| l057_3 | — | —/—/— | —/— | —/— |
| l058_1 | — | —/—/— | —/— | —/— |
| l059_2 | — | —/—/— | —/— | —/— |
| l060_3 | — | —/—/— | —/— | —/— |
| l061_1 | — | —/—/— | —/— | —/— |
| l062_2 | 504 | 0/0/0 | 0/0 | 0/0 |
| l063_3 | — | —/—/— | —/— | —/— |
| l064_1 | — | —/—/— | —/— | —/— |
| l065_2 | — | —/—/— | —/— | —/— |
| l066_3 | — | —/—/— | —/— | —/— |

## Matching-context envelopes

| Group / workload | valid/attempts | goodput median [min,max] B/s | RTT n / p50≤ / p95≤ / p99≤ / max µs | FPS / Δ idle | CPU0/1 % | min heap/largest B | callback p95≤/max µs | period max µs | TX_Q_OVF/DMA_ERROR |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| d341938ad369 direct_espnow/menu/idle 0B@0pps 10000ms | 3/3 | — [—,—] | —/—/—/—/— | 29.60/0.00 | 6.25/87.48 | 39340/14324 | 2/3 | 5366 | 0/0 |
| ab8e2e956637 direct_espnow/menu/rtt 64B@50pps 10000ms | 3/3 | 3059.20 [3059.20,3059.20] | 1434/16384/65536/65536/93019 | 29.30/-0.30 | 9.23/94.60 | 38424/14324 | 2/8 | 6011 | 0/0 |
| 537a5cd201a7 direct_espnow/menu/espnow_paced 200B@200pps 10000ms | 3/3 | 15660.00 [12880.00,17100.00] | 0/—/—/—/— | 28.90/-0.70 | 9.96/95.42 | 25604/13300 | 2/430 | 5910 | 0/0 |
| 598bdff9c2a4 direct_espnow/full_gate_drone/idle 0B@0pps 10000ms | 3/3 | — [—,—] | —/—/—/—/— | 22.20/0.00 | 12.52/95.57 | 39100/14324 | 512/594 | 5362 | 0/0 |
| 03ca07339b4a direct_espnow/full_gate_drone/rtt 64B@50pps 10000ms | 3/3 | 3052.80 [3052.80,3059.20] | 1432/16384/32768/65536/86024 | 19.00/-3.20 | 20.31/97.49 | 38808/14324 | 1024/1585 | 6251 | 0/0 |
| c7e602142803 direct_espnow/full_gate_drone/espnow_paced 200B@200pps 10000ms | 3/3 | 25860.00 [25200.00,26000.00] | 0/—/—/—/— | 18.20/-4.00 | 22.67/97.33 | 25404/13300 | 1641/1641 | 6022 | 0/0 |
| 81d37d5e33b5 direct_softap/menu/idle 0B@0pps 10000ms | 3/3 | — [—,—] | —/—/—/—/— | 29.60/0.00 | 7.11/87.51 | 38152/22516 | 2/2 | 7039 | 0/0 |
| 5c3c2f2b6b15 direct_softap/menu/rtt 64B@100pps 10000ms | 3/3 | 5792.00 [5753.60,5811.20] | 2709/65536/131072/131072/158122 | 28.30/-1.30 | 33.50/96.46 | 32052/13300 | 2/5613 | 11097 | 0/0 |
| d3cae9314340 direct_softap/menu/udp_paced 1024B@200pps 10000ms | 3/3 | 151142.40 [137728.00,157286.40] | 0/—/—/—/— | 28.60/-1.00 | 31.49/96.43 | 17140/13300 | 2/965 | 9115 | 0/0 |
| 6551443bec9f direct_softap/menu/tcp_goodput 1024B@0pps 10000ms | 0/3 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| 8d17539393d2 direct_softap/full_gate_drone/idle 0B@0pps 10000ms | 0/3 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| cb78e51828a3 direct_softap/full_gate_drone/rtt 64B@100pps 10000ms | 0/3 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| 46b89fd44a1b direct_softap/full_gate_drone/udp_paced 1024B@200pps 10000ms | 0/1 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| 4861766f3948 direct_softap/full_gate_drone/udp_paced 1024B@200pps 10000ms | 0/2 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| 421d3644e09d direct_softap/full_gate_drone/tcp_goodput 1024B@0pps 10000ms | 0/3 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| 23e5657b14ac through_ap/menu/idle 0B@0pps 10000ms | 3/3 | — [—,—] | —/—/—/—/— | 29.60/0.00 | 7.09/87.59 | 37624/20468 | 2/2 | 7344 | 0/0 |
| ad0a04e4d5ac through_ap/menu/rtt 64B@100pps 10000ms | 3/3 | 4448.00 [4332.80,4550.40] | 2044/131072/524288/524288/568765 | 28.50/-1.10 | 25.99/96.29 | 26812/13300 | 2/913 | 15456 | 0/0 |
| 0fe1f4f95ce4 through_ap/menu/udp_paced 1024B@200pps 10000ms | 3/3 | 99635.20 [90419.20,105676.80] | 0/—/—/—/— | 28.60/-1.00 | 21.10/96.03 | 18496/13300 | 2/999 | 8968 | 0/0 |
| e726d46054fe through_ap/menu/tcp_goodput 1024B@0pps 10000ms | 0/3 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| f6ca05a634e1 through_ap/full_gate_drone/idle 0B@0pps 10000ms | 0/3 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| 8e18aa526b4d through_ap/full_gate_drone/rtt 64B@100pps 10000ms | 0/3 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| 50cb2fb09c0c through_ap/full_gate_drone/udp_paced 1024B@200pps 10000ms | 0/2 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| 8e3dee991647 through_ap/full_gate_drone/udp_paced 1024B@200pps 10000ms | 1/1 | 117043.20 [117043.20,117043.20] | 0/—/—/—/— | 18.00/— | 37.16/98.09 | 23476/13300 | 2048/4820 | 10400 | 0/0 |
| d25f5d577a74 through_ap/full_gate_drone/tcp_goodput 1024B@0pps 10000ms | 0/1 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |
| d7fa51e001e8 through_ap/full_gate_drone/tcp_goodput 1024B@0pps 10000ms | 0/2 | — [—,—] | —/—/—/—/— | —/— | —/— | —/— | —/— | — | —/— |

The baseline delta uses a matching topology/local/source/allocation/connection control, when present; transport resource differences remain part of the comparison. CYD utilization uses non-idle tick samples. Peer CPU is unmeasured. Enqueue duration includes DMA blocking; independent timing maxima/percentiles must not be added as one block. TX_Q_OVF counts the legacy driver's free-TX-buffer-queue-full notification, not a measured audible gap. Queue limits/drain timing can hide events; analog continuity remains unknown.

RTT jitter is the mean/max absolute difference between adjacent received RTTs within each run; repetitions are not joined. The JSON/CSV retains counters, jitter, callback/lock/mix/enqueue/period histograms, BLE negotiated parameters, CRC/duplicates/reordering/too-old/queue-drop counts and actual allocation receipts.

## Failures and measurement limits

* `l028_1` direct_softap/menu/tcp_goodput: **FAILED** — Known-peer connection was not established; no unrelated endpoints attempted.
* `l029_2` direct_softap/menu/tcp_goodput: **FAILED** — Known-peer connection was not established; no unrelated endpoints attempted.
* `l030_3` direct_softap/menu/tcp_goodput: **FAILED** — Known-peer connection was not established; no unrelated endpoints attempted.
* `l031_1` direct_softap/full_gate_drone/idle: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 454.81882929999847}.
* `l032_2` direct_softap/full_gate_drone/idle: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 469.5064510999946}.
* `l033_3` direct_softap/full_gate_drone/idle: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 484.19265240000095}.
* `l034_1` direct_softap/full_gate_drone/rtt: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 498.96744869998656}.
* `l035_2` direct_softap/full_gate_drone/rtt: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 513.7590626999736}.
* `l036_3` direct_softap/full_gate_drone/rtt: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 528.6338506999891}.
* `l037_1` direct_softap/full_gate_drone/udp_paced: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 543.4964738999843}.
* `l038_2` direct_softap/full_gate_drone/udp_paced: **FAILED** — No valid end-to-end traffic/RTT established; target profile retained separately.
* `l039_3` direct_softap/full_gate_drone/udp_paced: **FAILED** — No valid end-to-end traffic/RTT established; target profile retained separately.
* `l040_1` direct_softap/full_gate_drone/tcp_goodput: **FAILED** — Known-peer connection was not established; no unrelated endpoints attempted.
* `l041_2` direct_softap/full_gate_drone/tcp_goodput: **FAILED** — Known-peer connection was not established; no unrelated endpoints attempted.
* `l042_3` direct_softap/full_gate_drone/tcp_goodput: **FAILED** — Known-peer connection was not established; no unrelated endpoints attempted.
* `l052_1` through_ap/menu/tcp_goodput: **FAILED** — Known-peer connection was not established; no unrelated endpoints attempted.
* `l053_2` through_ap/menu/tcp_goodput: **FAILED** — Known-peer connection was not established; no unrelated endpoints attempted.
* `l054_3` through_ap/menu/tcp_goodput: **FAILED** — Known-peer connection was not established; no unrelated endpoints attempted.
* `l055_1` through_ap/full_gate_drone/idle: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 830.3844061000273}.
* `l056_2` through_ap/full_gate_drone/idle: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 845.4361917999922}.
* `l057_3` through_ap/full_gate_drone/idle: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 860.5718130000168}.
* `l058_1` through_ap/full_gate_drone/rtt: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 875.6956117000082}.
* `l059_2` through_ap/full_gate_drone/rtt: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 890.8058096999885}.
* `l060_3` through_ap/full_gate_drone/rtt: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 906.0060107000172}.
* `l061_1` through_ap/full_gate_drone/udp_paced: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 921.202644200006}.
* `l063_3` through_ap/full_gate_drone/udp_paced: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 951.4666378999827}.
* `l064_1` through_ap/full_gate_drone/tcp_goodput: **FAILED** — Known-peer connection was not established; no unrelated endpoints attempted.
* `l065_2` through_ap/full_gate_drone/tcp_goodput: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 971.0239139000187}.
* `l066_3` through_ap/full_gate_drone/tcp_goodput: **FAILED** — Endpoint rejected command: {"schema": 1, "type": "kernel_error", "error": "bounded telemetry allocation or length failure", "host_received_elapsed_s": 973.1469345000223}.

* FAILED/BLOCKED cases and separately valid profiler windows retain their own statuses. Only MEASURED link cases enter successful-link aggregates.
* Idle has no RTT/goodput/loss observation; missing values remain null. Per-repetition goodput uses median/range, not an unsupported p99 from three repetitions.
* RTT percentiles pool bounded histogram bins across matching sessions, are upper bounds capped at exact observed max, and require>=100 samples for p99.
* Adjacent absolute RTT differences are not one-way delay jitter; cross-repetition boundaries are not joined.
* RTT histograms are conditional on valid replies received within the bounded tail. Missing responses never enter latency bins; sender/receiver/RTT counts must accompany the tails.
* Receiver counts exclude packets outside the 64-sequence deduplication horizon. too_old can include uncounted unique packets, making observed unique goodput a lower bound and the not-observed fraction an overestimate of loss.
* API acceptance is not peer receipt. Not-observed fraction includes software queues, horizon and tail effects; it is not isolated RF packet loss.
* TCP frames created in the steady window can finish API acceptance in the defined reply tail. Sender and receiver rates therefore share marked-window-plus-tail semantics.
* Peer CPU utilization is not measured by this image; CYD tick utilization is an estimate. Audio enqueue includes blocking DMA waits; driver events do not prove analog continuity.
* Only matching source/image, topology, local settings/allocation receipts, channel/power/connection parameters, payload, rate and timing scopes are pooled. Idle comparison includes transport resource/connection setup differences stated by its mode.

## Offload interpretation

Compare local compute with remote compute plus packing/unpacking, queueing and measured transport for the actual input/output message shapes. A full matched-payload RTT includes its payload transfer; do not add that transmission again. A separate fixed-overhead/throughput model needs independently estimated fixed overhead and directional byte-transfer costs. The 64-byte echo and paced one-way throughput are not a measured job RPC for arbitrary larger inputs/outputs. A throughput average or finite maximum gives no hard real-time guarantee; these links do not by themselves justify moving the local DAC deadline loop off-board.

Artifacts: [link-summary.json](link-summary.json), [per-run CSV](link-runs.csv), [group CSV](link-groups.csv), [analysis manifest](link-analysis-manifest.json).

## Publication provenance

Extract the raw archive into `evidence/`, preserving its `physical/` prefix. The publication verifies every capture-manifest member against the ZIP and preserves failed statuses. `manifest.json` pins archive, source and derived-output hashes.
