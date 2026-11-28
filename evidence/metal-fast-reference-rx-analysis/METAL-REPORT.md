# Native snapshot and software-loop observations

Original capture status **CAPTURE COMPLETE** remains unchanged.

| Run | mode | snapshots | nominal horizon sum µs | observed capture sum µs | loop µs | samples/loop second |
| --- | --- | --- | --- | --- | --- | --- |
| d001_1 | bench | 20 | 20475.000 | 20510 | 44243 | 7404561.2 |
| d002_2 | bench | 20 | 20475.000 | 20510 | 44235 | 7405900.3 |
| d003_3 | bench | 20 | 20475.000 | 20514 | 44235 | 7405900.3 |
| d004_1 | bench | 20 | 8190.000 | 8224 | 31911 | 10266052.5 |
| d005_2 | bench | 20 | 8190.000 | 8225 | 31911 | 10266052.5 |
| d006_3 | bench | 20 | 8190.000 | 8225 | 31913 | 10265409.1 |
| d007_1 | bench | 20 | 4095.000 | 4123 | 27788 | 11789261.6 |
| d008_2 | bench | 20 | 4095.000 | 4126 | 27789 | 11788837.3 |
| d009_3 | bench | 20 | 4095.000 | 4125 | 27788 | 11789261.6 |
| d010_1 | capture | 1 | 1023.750 | 1025 | 264723 | 61876.0 |
| d011_2 | capture | 1 | 1023.750 | 1026 | 264720 | 61876.7 |
| d012_3 | capture | 1 | 1023.750 | 1025 | 264716 | 61877.6 |
| d013_1 | capture | 1 | 409.500 | 411 | 264709 | 61879.3 |
| d014_2 | capture | 1 | 409.500 | 411 | 264676 | 61887.0 |
| d015_3 | capture | 1 | 409.500 | 411 | 264667 | 61889.1 |
| d016_1 | capture | 1 | 204.750 | 206 | 264079 | 62026.9 |
| d017_2 | capture | 1 | 204.750 | 206 | 263649 | 62128.1 |
| d018_3 | capture | 1 | 204.750 | 206 | 263643 | 62129.5 |

- Every burst resets sample index0. Separate snapshots and requested inter-burst gaps are not a continuous IQ stream.
- Native-clock arithmetic horizons differ from CPU-observed trigger/completion intervals and whole-loop elapsed time.
- No-export bench and USB-export capture have different observer/deadtime scopes and are not pooled.
- TX achieved writes/s and mask cycle brackets measure software execution, not independent RF edges or ultimate modulation bandwidth.
- TX late_cycles/late_cycles_max refer to the pending deadline before obsolete slots are skipped; they measure backlog/missed deadlines, not final output-symbol jitter.
- IQ statistics are receiver codes; spectral frequencies assume the configured native clock and are not calibrated RF frequency or power.
- No on-board audio reconstruction, analog-output proof or synchronized end-to-end latency claim.

Exact MMIO/backend/PHY-init mode, observer stride, mask/gap cycles, register words, heap/sensor values, CRC and canary/restoration records remain in structured JSON/CSV. API/void-call completion alone does not prove physical RF cessation or edge shape.
