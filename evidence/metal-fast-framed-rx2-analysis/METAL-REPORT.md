# Native snapshot and software-loop observations

Original capture status **CAPTURE COMPLETE** remains unchanged.

| Run | mode | snapshots | nominal horizon sum µs | observed capture sum µs | loop µs | samples/loop second |
| --- | --- | --- | --- | --- | --- | --- |
| d001_1 | capture | 1 | 1023.750 | 1025 | 79835 | 205173.2 |
| d002_2 | capture | 1 | 1023.750 | 1025 | 79790 | 205288.9 |
| d003_3 | capture | 1 | 1023.750 | 1025 | 80561 | 203324.2 |
| d004_1 | capture | 1 | 1023.750 | 1025 | 79885 | 205044.8 |
| d005_2 | capture | 1 | 1023.750 | 1025 | 79838 | 205165.5 |
| d006_3 | capture | 1 | 1023.750 | 1025 | 79922 | 204949.8 |
| d007_1 | capture | 1 | 1023.750 | 1025 | 80251 | 204109.6 |
| d008_2 | capture | 1 | 1023.750 | 1025 | 80494 | 203493.4 |
| d009_3 | capture | 1 | 1023.750 | 1025 | 80123 | 204435.7 |
| d010_1 | capture | 1 | 409.500 | 411 | 79436 | 206203.7 |
| d011_2 | capture | 1 | 409.500 | 411 | 79233 | 206732.0 |
| d012_3 | capture | 1 | 409.500 | 411 | 79214 | 206781.6 |
| d013_1 | capture | 1 | 409.500 | 411 | 79559 | 205884.9 |
| d014_2 | capture | 1 | 409.500 | 411 | 79116 | 207037.8 |
| d015_3 | capture | 1 | 409.500 | 411 | 79452 | 206162.2 |
| d016_1 | capture | 1 | 409.500 | 411 | 79145 | 206961.9 |
| d017_2 | capture | 1 | 409.500 | 411 | 79534 | 205949.7 |
| d018_3 | capture | 1 | 409.500 | 411 | 79527 | 205967.8 |
| d019_1 | capture | 1 | 204.750 | 206 | 78728 | 208058.1 |
| d020_2 | capture | 1 | 204.750 | 206 | 78904 | 207594.0 |
| d021_3 | capture | 1 | 204.750 | 206 | 78868 | 207688.8 |

- Every burst resets sample index0. Separate snapshots and requested inter-burst gaps are not a continuous IQ stream.
- Native-clock arithmetic horizons differ from CPU-observed trigger/completion intervals and whole-loop elapsed time.
- No-export bench and USB-export capture have different observer/deadtime scopes and are not pooled.
- TX achieved writes/s and mask cycle brackets measure software execution, not independent RF edges or ultimate modulation bandwidth.
- TX late_cycles/late_cycles_max refer to the pending deadline before obsolete slots are skipped; they measure backlog/missed deadlines, not final output-symbol jitter.
- IQ statistics are receiver codes; spectral frequencies assume the configured native clock and are not calibrated RF frequency or power.
- No on-board audio reconstruction, analog-output proof or synchronized end-to-end latency claim.

Exact MMIO/backend/PHY-init mode, observer stride, mask/gap cycles, register words, heap/sensor values, CRC and canary/restoration records remain in structured JSON/CSV. API/void-call completion alone does not prove physical RF cessation or edge shape.
