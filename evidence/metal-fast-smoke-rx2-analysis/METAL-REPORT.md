# Native snapshot and software-loop observations

Original capture status **CAPTURE COMPLETE** remains unchanged.

| Run | mode | snapshots | nominal horizon sum µs | observed capture sum µs | loop µs | samples/loop second |
| --- | --- | --- | --- | --- | --- | --- |
| d001_1 | capture | 1 | 409.500 | 411 | 78993 | 207360.1 |
| d002_1 | capture | 1 | 409.500 | 411 | 79764 | 205355.8 |
| d003_1 | capture | 1 | 409.500 | 411 | 78919 | 207554.6 |

- Every burst resets sample index0. Separate snapshots and requested inter-burst gaps are not a continuous IQ stream.
- Native-clock arithmetic horizons differ from CPU-observed trigger/completion intervals and whole-loop elapsed time.
- No-export bench and USB-export capture have different observer/deadtime scopes and are not pooled.
- TX achieved writes/s and mask cycle brackets measure software execution, not independent RF edges or ultimate modulation bandwidth.
- TX late_cycles/late_cycles_max refer to the pending deadline before obsolete slots are skipped; they measure backlog/missed deadlines, not final output-symbol jitter.
- IQ statistics are receiver codes; spectral frequencies assume the configured native clock and are not calibrated RF frequency or power.
- No on-board audio reconstruction, analog-output proof or synchronized end-to-end latency claim.

Exact MMIO/backend/PHY-init mode, observer stride, mask/gap cycles, register words, heap/sensor values, CRC and canary/restoration records remain in structured JSON/CSV. API/void-call completion alone does not prove physical RF cessation or edge shape.
