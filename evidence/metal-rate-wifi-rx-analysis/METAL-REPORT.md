# Native snapshot and software-loop observations

Original capture status **FAILED** remains unchanged.

| Run | mode | snapshots | nominal horizon sum µs | observed capture sum µs | loop µs | samples/loop second |
| --- | --- | --- | --- | --- | --- | --- |
| d001_1 | capture | 1 | 1023.750 | 1025 | 264673 | 61887.7 |
| d002_2 | capture | 1 | 1023.750 | 1025 | 264646 | 61894.0 |
| d003_3 | capture | 1 | 1023.750 | 1025 | 264152 | 62009.8 |
| d004_1 | capture | 1 | 1023.750 | 1025 | 264709 | 61879.3 |
| d005_2 | capture | 1 | 1023.750 | 1025 | 265078 | 61793.1 |
| d006_3 | capture | 1 | 1023.750 | 1025 | 265053 | 61799.0 |
| d007_1 | capture | 1 | 1023.750 | 1025 | 264721 | 61876.5 |
| d008_2 | capture | 1 | 1023.750 | 1025 | 264668 | 61888.9 |
| d009_3 | capture | 1 | 1023.750 | 1025 | 265065 | 61796.2 |
| d010_1 | capture | 1 | 1023.750 | 1025 | 264732 | 61873.9 |
| d011_2 | capture | 1 | 1023.750 | 1025 | 264708 | 61879.5 |
| d012_3 | capture | 1 | 1023.750 | 1025 | 264656 | 61891.7 |
| d013_1 | capture | 1 | 1023.750 | 1025 | 264753 | 61869.0 |
| d014_2 | capture | 1 | 1023.750 | 1025 | 264719 | 61876.9 |
| d015_3 | capture | 1 | 1023.750 | 1025 | 264667 | 61889.1 |
| d016_1 | capture | 1 | 1023.750 | 1025 | 264640 | 61895.4 |
| d017_2 | capture | 1 | 1023.750 | 1025 | 264733 | 61873.7 |
| d018_3 | capture | 1 | 1023.750 | 1025 | 264706 | 61880.0 |
| d019_1 | capture | 1 | 409.500 | 411 | 264654 | 61892.1 |
| d020_2 | capture | 1 | 409.500 | 411 | 264153 | 62009.5 |
| d021_3 | capture | 1 | 409.500 | 411 | 264704 | 61880.4 |
| d022_1 | capture | 1 | 409.500 | 411 | 264646 | 61894.0 |
| d023_2 | capture | 1 | 409.500 | 411 | 264659 | 61891.0 |
| d024_3 | capture | 1 | 409.500 | 411 | 264700 | 61881.4 |
| d025_1 | capture | 1 | 204.750 | 206 | 263643 | 62129.5 |
| d026_2 | capture | 1 | 204.750 | 206 | 263729 | 62109.2 |
| d027_3 | capture | 1 | 204.750 | 206 | 263672 | 62122.6 |
| d028_1 | capture | 1 | 204.750 | 206 | 263642 | 62129.7 |
| d029_2 | capture | 1 | 204.750 | 206 | 263727 | 62109.7 |
| d030_3 | capture | 1 | 204.750 | 206 | 263671 | 62122.9 |
| d031_1 | capture | 1 | 204.750 | 206 | 306066 | 53517.9 |
| d032_2 | capture | 1 | 204.750 | 206 | 263726 | 62109.9 |
| d033_3 | capture | 1 | 204.750 | 206 | 263669 | 62123.3 |
| d034_1 | capture | 1 | 204.750 | 206 | 263641 | 62129.9 |
| d035_2 | capture | 1 | 204.750 | 206 | 263726 | 62109.9 |
| d036_3 | capture | 1 | 204.750 | 206 | 263669 | 62123.3 |
| d037_1 | capture | 1 | 204.750 | 206 | 263640 | 62130.2 |
| d038_2 | capture | 1 | 204.750 | 206 | 263725 | 62110.2 |
| d039_3 | capture | 1 | 204.750 | 206 | 263667 | 62123.8 |

- Every burst resets sample index0. Separate snapshots and requested inter-burst gaps are not a continuous IQ stream.
- Native-clock arithmetic horizons differ from CPU-observed trigger/completion intervals and whole-loop elapsed time.
- No-export bench and USB-export capture have different observer/deadtime scopes and are not pooled.
- TX achieved writes/s and mask cycle brackets measure software execution, not independent RF edges or ultimate modulation bandwidth.
- TX late_cycles/late_cycles_max refer to the pending deadline before obsolete slots are skipped; they measure backlog/missed deadlines, not final output-symbol jitter.
- IQ statistics are receiver codes; spectral frequencies assume the configured native clock and are not calibrated RF frequency or power.
- No on-board audio reconstruction, analog-output proof or synchronized end-to-end latency claim.

Exact MMIO/backend/PHY-init mode, observer stride, mask/gap cycles, register words, heap/sensor values, CRC and canary/restoration records remain in structured JSON/CSV. API/void-call completion alone does not prove physical RF cessation or edge shape.
