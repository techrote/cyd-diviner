# Native snapshot and software-loop observations

Original capture status **CAPTURE COMPLETE** remains unchanged.

| Run | mode | snapshots | nominal horizon sum µs | observed capture sum µs | loop µs | samples/loop second |
| --- | --- | --- | --- | --- | --- | --- |
| d001_1 | capture | 1 | 409.500 | 411 | 264720 | 61876.7 |
| d002_2 | capture | 1 | 409.500 | 411 | 264713 | 61878.3 |
| d003_3 | capture | 1 | 409.500 | 411 | 264707 | 61879.7 |
| d004_1 | capture | 1 | 409.500 | 411 | 264670 | 61888.4 |
| d005_2 | capture | 1 | 409.500 | 411 | 264661 | 61890.5 |
| d006_3 | capture | 1 | 409.500 | 411 | 264653 | 61892.4 |
| d007_1 | capture | 1 | 204.750 | 206 | 264063 | 62030.7 |
| d008_2 | capture | 1 | 204.750 | 206 | 263177 | 62239.5 |
| d009_3 | capture | 1 | 204.750 | 206 | 263161 | 62243.3 |
| d010_1 | capture | 1 | 409.500 | 411 | 264153 | 62009.5 |
| d011_2 | capture | 1 | 409.500 | 412 | 264726 | 61875.3 |
| d012_3 | capture | 1 | 409.500 | 411 | 264716 | 61877.6 |
| d013_1 | capture | 1 | 409.500 | 411 | 264707 | 61879.7 |
| d014_2 | capture | 1 | 409.500 | 411 | 264671 | 61888.2 |
| d015_3 | capture | 1 | 409.500 | 411 | 264669 | 61888.6 |
| d016_1 | capture | 1 | 204.750 | 206 | 264075 | 62027.8 |
| d017_2 | capture | 1 | 204.750 | 206 | 264068 | 62029.5 |
| d018_3 | capture | 1 | 204.750 | 206 | 263640 | 62130.2 |
| d019_1 | capture | 1 | 204.750 | 206 | 263745 | 62105.4 |
| d020_2 | capture | 1 | 204.750 | 206 | 263737 | 62107.3 |
| d021_3 | capture | 1 | 204.750 | 206 | 263731 | 62108.7 |
| d022_1 | capture | 1 | 204.750 | 207 | 263723 | 62110.6 |
| d023_2 | capture | 1 | 204.750 | 206 | 263716 | 62112.3 |
| d024_3 | capture | 1 | 204.750 | 206 | 264058 | 62031.8 |
| d025_1 | capture | 1 | 204.750 | 206 | 263675 | 62121.9 |
| d026_2 | capture | 1 | 204.750 | 206 | 263666 | 62124.1 |
| d027_3 | capture | 1 | 204.750 | 206 | 263658 | 62125.9 |
| d028_1 | capture | 1 | 204.750 | 206 | 263652 | 62127.3 |
| d029_2 | capture | 1 | 204.750 | 206 | 264065 | 62030.2 |
| d030_3 | capture | 1 | 204.750 | 206 | 264056 | 62032.3 |
| d031_1 | capture | 1 | 204.750 | 206 | 263160 | 62243.5 |
| d032_2 | capture | 1 | 204.750 | 207 | 263735 | 62107.8 |
| d033_3 | capture | 1 | 204.750 | 206 | 263788 | 62095.3 |
| d034_1 | capture | 1 | 204.750 | 206 | 263726 | 62109.9 |
| d035_2 | capture | 1 | 204.750 | 206 | 263713 | 62113.0 |
| d036_3 | capture | 1 | 204.750 | 206 | 263705 | 62114.9 |
| d037_1 | capture | 1 | 204.750 | 206 | 263671 | 62122.9 |
| d038_2 | capture | 1 | 204.750 | 206 | 263664 | 62124.5 |
| d039_3 | capture | 1 | 204.750 | 206 | 264074 | 62028.1 |
| d040_1 | capture | 1 | 204.750 | 206 | 263647 | 62128.5 |
| d041_2 | capture | 1 | 204.750 | 206 | 263641 | 62129.9 |
| d042_3 | capture | 1 | 204.750 | 206 | 263755 | 62103.1 |

- Every burst resets sample index0. Separate snapshots and requested inter-burst gaps are not a continuous IQ stream.
- Native-clock arithmetic horizons differ from CPU-observed trigger/completion intervals and whole-loop elapsed time.
- No-export bench and USB-export capture have different observer/deadtime scopes and are not pooled.
- TX achieved writes/s and mask cycle brackets measure software execution, not independent RF edges or ultimate modulation bandwidth.
- TX late_cycles/late_cycles_max refer to the pending deadline before obsolete slots are skipped; they measure backlog/missed deadlines, not final output-symbol jitter.
- IQ statistics are receiver codes; spectral frequencies assume the configured native clock and are not calibrated RF frequency or power.
- No on-board audio reconstruction, analog-output proof or synchronized end-to-end latency claim.

Exact MMIO/backend/PHY-init mode, observer stride, mask/gap cycles, register words, heap/sensor values, CRC and canary/restoration records remain in structured JSON/CSV. API/void-call completion alone does not prove physical RF cessation or edge shape.
