# Native snapshot and software-loop observations

Original capture status **CAPTURE COMPLETE** remains unchanged.

| Run | mode | snapshots | nominal horizon sum µs | observed capture sum µs | loop µs | samples/loop second |
| --- | --- | --- | --- | --- | --- | --- |
| d001_1 | capture | 1 | 1023.750 | 1026 | 265066 | 61795.9 |
| d002_2 | capture | 1 | 1023.750 | 1025 | 264158 | 62008.3 |
| d003_3 | capture | 1 | 1023.750 | 1025 | 264716 | 61877.6 |
| d004_1 | capture | 1 | 409.500 | 411 | 264668 | 61888.9 |
| d005_2 | capture | 1 | 409.500 | 411 | 264640 | 61895.4 |
| d006_3 | capture | 1 | 409.500 | 411 | 265206 | 61763.3 |
| d007_1 | capture | 1 | 204.750 | 206 | 263700 | 62116.0 |
| d008_2 | capture | 1 | 204.750 | 206 | 264070 | 62029.0 |
| d009_3 | capture | 1 | 204.750 | 206 | 263155 | 62244.7 |

- Every burst resets sample index0. Separate snapshots and requested inter-burst gaps are not a continuous IQ stream.
- Native-clock arithmetic horizons differ from CPU-observed trigger/completion intervals and whole-loop elapsed time.
- No-export bench and USB-export capture have different observer/deadtime scopes and are not pooled.
- TX achieved writes/s and mask cycle brackets measure software execution, not independent RF edges or ultimate modulation bandwidth.
- TX late_cycles/late_cycles_max refer to the pending deadline before obsolete slots are skipped; they measure backlog/missed deadlines, not final output-symbol jitter.
- IQ statistics are receiver codes; spectral frequencies assume the configured native clock and are not calibrated RF frequency or power.
- No on-board audio reconstruction, analog-output proof or synchronized end-to-end latency claim.

Exact MMIO/backend/PHY-init mode, observer stride, mask/gap cycles, register words, heap/sensor values, CRC and canary/restoration records remain in structured JSON/CSV. API/void-call completion alone does not prove physical RF cessation or edge shape.
