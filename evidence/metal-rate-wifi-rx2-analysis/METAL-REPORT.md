# Native snapshot and software-loop observations

Original capture status **FAILED** remains unchanged.

| Run | mode | snapshots | nominal horizon sum µs | observed capture sum µs | loop µs | samples/loop second |
| --- | --- | --- | --- | --- | --- | --- |
| d001_1 | capture | 1 | 1023.750 | 1026 | 80500 | 203478.3 |
| d002_2 | capture | 1 | 1023.750 | 1026 | 80789 | 202750.4 |
| d003_3 | capture | 1 | 1023.750 | 1026 | 81653 | 200605.0 |
| d004_1 | capture | 1 | 1023.750 | 1026 | 80790 | 202747.9 |
| d005_2 | capture | 1 | 1023.750 | 1026 | 80900 | 202472.2 |
| d006_3 | capture | 1 | 1023.750 | 1026 | 80575 | 203288.9 |
| d007_1 | capture | 1 | 1023.750 | 1026 | 81527 | 200915.0 |
| d008_2 | capture | 1 | 1023.750 | 1026 | 81341 | 201374.5 |
| d009_3 | capture | 1 | 1023.750 | 1026 | 81688 | 200519.0 |
| d010_1 | capture | 1 | 1023.750 | 1026 | 80889 | 202499.7 |
| d011_2 | capture | 1 | 1023.750 | 1026 | 81012 | 202192.3 |
| d012_3 | capture | 1 | 1023.750 | 1026 | 80351 | 203855.6 |
| d013_1 | capture | 1 | 1023.750 | 1026 | 81014 | 202187.3 |
| d014_2 | capture | 1 | 1023.750 | 1026 | 81431 | 201151.9 |
| d015_3 | capture | 1 | 1023.750 | 1026 | 81492 | 201001.3 |
| d016_1 | capture | 1 | 1023.750 | 1026 | 80806 | 202707.7 |
| d017_2 | capture | 1 | 1023.750 | 1026 | 81302 | 201471.1 |
| d018_3 | capture | 1 | 1023.750 | 1026 | 80552 | 203346.9 |
| d019_1 | capture | 1 | 409.500 | 412 | 79831 | 205183.5 |
| d020_2 | capture | 1 | 409.500 | 411 | 81185 | 201761.4 |
| d021_3 | capture | 1 | 409.500 | 411 | 80412 | 203700.9 |
| d022_1 | capture | 1 | 409.500 | 411 | 79768 | 205345.5 |
| d023_2 | capture | 1 | 409.500 | 411 | 81596 | 200745.1 |
| d024_3 | capture | 1 | 409.500 | 411 | 80223 | 204180.8 |
| d025_1 | capture | 1 | 204.750 | 206 | 79554 | 205897.9 |
| d026_2 | capture | 1 | 204.750 | 206 | 80302 | 203980.0 |
| d027_3 | capture | 1 | 204.750 | 206 | 80048 | 204627.2 |
| d028_1 | capture | 1 | 204.750 | 207 | 81624 | 200676.3 |
| d029_2 | capture | 1 | 204.750 | 206 | 80393 | 203749.1 |
| d030_3 | capture | 1 | 204.750 | 206 | 80419 | 203683.2 |
| d031_1 | capture | 1 | 204.750 | 206 | 80212 | 204208.8 |
| d032_2 | capture | 1 | 204.750 | 206 | 80391 | 203754.2 |
| d033_3 | capture | 1 | 204.750 | 206 | 79974 | 204816.6 |
| d034_1 | capture | 1 | 204.750 | 206 | 81675 | 200551.0 |
| d035_2 | capture | 1 | 204.750 | 206 | 79571 | 205853.9 |
| d036_3 | capture | 1 | 204.750 | 206 | 79936 | 204913.9 |
| d037_1 | capture | 1 | 204.750 | 207 | 79608 | 205758.2 |
| d038_2 | capture | 1 | 204.750 | 207 | 79639 | 205678.1 |
| d039_3 | capture | 1 | 204.750 | 206 | 79905 | 204993.4 |

- Every burst resets sample index0. Separate snapshots and requested inter-burst gaps are not a continuous IQ stream.
- Native-clock arithmetic horizons differ from CPU-observed trigger/completion intervals and whole-loop elapsed time.
- No-export bench and USB-export capture have different observer/deadtime scopes and are not pooled.
- TX achieved writes/s and mask cycle brackets measure software execution, not independent RF edges or ultimate modulation bandwidth.
- TX late_cycles/late_cycles_max refer to the pending deadline before obsolete slots are skipped; they measure backlog/missed deadlines, not final output-symbol jitter.
- IQ statistics are receiver codes; spectral frequencies assume the configured native clock and are not calibrated RF frequency or power.
- No on-board audio reconstruction, analog-output proof or synchronized end-to-end latency claim.

Exact MMIO/backend/PHY-init mode, observer stride, mask/gap cycles, register words, heap/sensor values, CRC and canary/restoration records remain in structured JSON/CSV. API/void-call completion alone does not prove physical RF cessation or edge shape.
