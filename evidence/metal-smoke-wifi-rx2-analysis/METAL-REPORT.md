# Native snapshot and software-loop observations

Original capture status **CAPTURE COMPLETE** remains unchanged.

| Run | mode | snapshots | nominal horizon sum µs | observed capture sum µs | loop µs | samples/loop second |
| --- | --- | --- | --- | --- | --- | --- |
| d001_1 | bench | 20 | 20475.000 | 20509 | 44238 | 7405398.1 |
| d002_2 | bench | 20 | 20475.000 | 20513 | 44227 | 7407239.9 |
| d003_3 | bench | 20 | 20475.000 | 20510 | 44234 | 7406067.7 |
| d004_1 | bench | 20 | 8190.000 | 8224 | 31905 | 10267983.1 |
| d005_2 | bench | 20 | 8190.000 | 8224 | 31902 | 10268948.7 |
| d006_3 | bench | 20 | 8190.000 | 8226 | 31906 | 10267661.3 |
| d007_1 | bench | 20 | 4095.000 | 4124 | 27782 | 11791807.6 |
| d008_2 | bench | 20 | 4095.000 | 4121 | 27782 | 11791807.6 |
| d009_3 | bench | 20 | 4095.000 | 4125 | 27776 | 11794354.8 |
| d010_1 | capture | 1 | 1023.750 | 1026 | 83074 | 197173.6 |
| d011_2 | capture | 1 | 1023.750 | 1026 | 82832 | 197749.7 |
| d012_3 | capture | 1 | 1023.750 | 1026 | 83948 | 195120.8 |
| d013_1 | capture | 1 | 409.500 | 412 | 81952 | 199873.1 |
| d014_2 | capture | 1 | 409.500 | 412 | 83262 | 196728.4 |
| d015_3 | capture | 1 | 409.500 | 411 | 82873 | 197651.8 |
| d016_1 | capture | 1 | 204.750 | 206 | 82260 | 199124.7 |
| d017_2 | capture | 1 | 204.750 | 206 | 84324 | 194250.7 |
| d018_3 | capture | 1 | 204.750 | 206 | 83400 | 196402.9 |

- Every burst resets sample index0. Separate snapshots and requested inter-burst gaps are not a continuous IQ stream.
- Native-clock arithmetic horizons differ from CPU-observed trigger/completion intervals and whole-loop elapsed time.
- No-export bench and USB-export capture have different observer/deadtime scopes and are not pooled.
- TX achieved writes/s and mask cycle brackets measure software execution, not independent RF edges or ultimate modulation bandwidth.
- TX late_cycles/late_cycles_max refer to the pending deadline before obsolete slots are skipped; they measure backlog/missed deadlines, not final output-symbol jitter.
- IQ statistics are receiver codes; spectral frequencies assume the configured native clock and are not calibrated RF frequency or power.
- No on-board audio reconstruction, analog-output proof or synchronized end-to-end latency claim.

Exact MMIO/backend/PHY-init mode, observer stride, mask/gap cycles, register words, heap/sensor values, CRC and canary/restoration records remain in structured JSON/CSV. API/void-call completion alone does not prove physical RF cessation or edge shape.
