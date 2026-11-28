# Native snapshot and software-loop observations

Original capture status **CAPTURE COMPLETE** remains unchanged.

| Run | mode | snapshots | nominal horizon sum µs | observed capture sum µs | loop µs | samples/loop second |
| --- | --- | --- | --- | --- | --- | --- |
| d001_1 | bench | 20 | 20475.000 | 20507 | 44239 | 7405230.7 |
| d002_2 | bench | 20 | 20475.000 | 20509 | 44240 | 7405063.3 |
| d003_3 | bench | 20 | 20475.000 | 20511 | 44239 | 7405230.7 |
| d004_1 | bench | 20 | 8190.000 | 8223 | 31906 | 10267661.3 |
| d005_2 | bench | 20 | 8190.000 | 8225 | 31913 | 10265409.1 |
| d006_3 | bench | 20 | 8190.000 | 8227 | 31911 | 10266052.5 |
| d007_1 | bench | 20 | 4095.000 | 4124 | 27785 | 11790534.5 |
| d008_2 | bench | 20 | 4095.000 | 4126 | 27790 | 11788413.1 |
| d009_3 | bench | 20 | 4095.000 | 4122 | 27790 | 11788413.1 |
| d010_1 | capture | 1 | 1023.750 | 1025 | 81180 | 201773.8 |
| d011_2 | capture | 1 | 1023.750 | 1025 | 80315 | 203947.0 |
| d012_3 | capture | 1 | 1023.750 | 1026 | 80479 | 203531.4 |
| d013_1 | capture | 1 | 409.500 | 411 | 79436 | 206203.7 |
| d014_2 | capture | 1 | 409.500 | 412 | 79560 | 205882.4 |
| d015_3 | capture | 1 | 409.500 | 411 | 79546 | 205918.6 |
| d016_1 | capture | 1 | 204.750 | 206 | 81163 | 201816.1 |
| d017_2 | capture | 1 | 204.750 | 206 | 79455 | 206154.4 |
| d018_3 | capture | 1 | 204.750 | 206 | 79477 | 206097.4 |

- Every burst resets sample index0. Separate snapshots and requested inter-burst gaps are not a continuous IQ stream.
- Native-clock arithmetic horizons differ from CPU-observed trigger/completion intervals and whole-loop elapsed time.
- No-export bench and USB-export capture have different observer/deadtime scopes and are not pooled.
- TX achieved writes/s and mask cycle brackets measure software execution, not independent RF edges or ultimate modulation bandwidth.
- TX late_cycles/late_cycles_max refer to the pending deadline before obsolete slots are skipped; they measure backlog/missed deadlines, not final output-symbol jitter.
- IQ statistics are receiver codes; spectral frequencies assume the configured native clock and are not calibrated RF frequency or power.
- No on-board audio reconstruction, analog-output proof or synchronized end-to-end latency claim.

Exact MMIO/backend/PHY-init mode, observer stride, mask/gap cycles, register words, heap/sensor values, CRC and canary/restoration records remain in structured JSON/CSV. API/void-call completion alone does not prove physical RF cessation or edge shape.
