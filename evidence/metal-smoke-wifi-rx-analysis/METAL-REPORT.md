# Native snapshot and software-loop observations

Original capture status **CAPTURE COMPLETE** remains unchanged.

| Run | mode | snapshots | nominal horizon sum µs | observed capture sum µs | loop µs | samples/loop second |
| --- | --- | --- | --- | --- | --- | --- |
| d001_1 | bench | 20 | 20475.000 | 20511 | 44325 | 7390862.9 |
| d002_2 | bench | 20 | 20475.000 | 20511 | 44227 | 7407239.9 |
| d003_3 | bench | 20 | 20475.000 | 20504 | 44230 | 7406737.5 |
| d004_1 | bench | 20 | 8190.000 | 8224 | 31904 | 10268304.9 |
| d005_2 | bench | 20 | 8190.000 | 8224 | 31905 | 10267983.1 |
| d006_3 | bench | 20 | 8190.000 | 8225 | 31906 | 10267661.3 |
| d007_1 | bench | 20 | 4095.000 | 4125 | 27783 | 11791383.2 |
| d008_2 | bench | 20 | 4095.000 | 4123 | 27782 | 11791807.6 |
| d009_3 | bench | 20 | 4095.000 | 4126 | 27782 | 11791807.6 |
| d010_1 | capture | 1 | 1023.750 | 1025 | 264661 | 61890.5 |
| d011_2 | capture | 1 | 1023.750 | 1026 | 265055 | 61798.5 |
| d012_3 | capture | 1 | 1023.750 | 1025 | 264722 | 61876.2 |
| d013_1 | capture | 1 | 409.500 | 411 | 264089 | 62024.5 |
| d014_2 | capture | 1 | 409.500 | 412 | 264640 | 61895.4 |
| d015_3 | capture | 1 | 409.500 | 411 | 264724 | 61875.8 |
| d016_1 | capture | 1 | 204.750 | 207 | 263667 | 62123.8 |
| d017_2 | capture | 1 | 204.750 | 206 | 263635 | 62131.4 |
| d018_3 | capture | 1 | 204.750 | 207 | 263722 | 62110.9 |

- Every burst resets sample index0. Separate snapshots and requested inter-burst gaps are not a continuous IQ stream.
- Native-clock arithmetic horizons differ from CPU-observed trigger/completion intervals and whole-loop elapsed time.
- No-export bench and USB-export capture have different observer/deadtime scopes and are not pooled.
- TX achieved writes/s and mask cycle brackets measure software execution, not independent RF edges or ultimate modulation bandwidth.
- TX late_cycles/late_cycles_max refer to the pending deadline before obsolete slots are skipped; they measure backlog/missed deadlines, not final output-symbol jitter.
- IQ statistics are receiver codes; spectral frequencies assume the configured native clock and are not calibrated RF frequency or power.
- No on-board audio reconstruction, analog-output proof or synchronized end-to-end latency claim.

Exact MMIO/backend/PHY-init mode, observer stride, mask/gap cycles, register words, heap/sensor values, CRC and canary/restoration records remain in structured JSON/CSV. API/void-call completion alone does not prove physical RF cessation or edge shape.
