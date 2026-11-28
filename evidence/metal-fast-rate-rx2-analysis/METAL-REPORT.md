# Native snapshot and software-loop observations

Original capture status **CAPTURE COMPLETE** remains unchanged.

| Run | mode | snapshots | nominal horizon sum µs | observed capture sum µs | loop µs | samples/loop second |
| --- | --- | --- | --- | --- | --- | --- |
| d001_1 | capture | 1 | 409.500 | 411 | 79250 | 206687.7 |
| d002_2 | capture | 1 | 409.500 | 411 | 79131 | 206998.5 |
| d003_3 | capture | 1 | 409.500 | 411 | 79323 | 206497.5 |
| d004_1 | capture | 1 | 409.500 | 411 | 79054 | 207200.1 |
| d005_2 | capture | 1 | 409.500 | 411 | 79417 | 206253.1 |
| d006_3 | capture | 1 | 409.500 | 411 | 79402 | 206292.0 |
| d007_1 | capture | 1 | 204.750 | 206 | 79007 | 207323.4 |
| d008_2 | capture | 1 | 204.750 | 206 | 78782 | 207915.5 |
| d009_3 | capture | 1 | 204.750 | 206 | 78912 | 207573.0 |
| d010_1 | capture | 1 | 409.500 | 411 | 79006 | 207326.0 |
| d011_2 | capture | 1 | 409.500 | 411 | 79147 | 206956.7 |
| d012_3 | capture | 1 | 409.500 | 411 | 79274 | 206625.1 |
| d013_1 | capture | 1 | 409.500 | 411 | 79465 | 206128.5 |
| d014_2 | capture | 1 | 409.500 | 411 | 79635 | 205688.5 |
| d015_3 | capture | 1 | 409.500 | 411 | 79172 | 206891.3 |
| d016_1 | capture | 1 | 204.750 | 206 | 78710 | 208105.7 |
| d017_2 | capture | 1 | 204.750 | 206 | 78610 | 208370.4 |
| d018_3 | capture | 1 | 204.750 | 206 | 78623 | 208336.0 |
| d019_1 | capture | 1 | 204.750 | 206 | 78708 | 208111.0 |
| d020_2 | capture | 1 | 204.750 | 206 | 78980 | 207394.3 |
| d021_3 | capture | 1 | 204.750 | 206 | 78749 | 208002.6 |
| d022_1 | capture | 1 | 204.750 | 206 | 78655 | 208251.2 |
| d023_2 | capture | 1 | 204.750 | 206 | 78658 | 208243.3 |
| d024_3 | capture | 1 | 204.750 | 206 | 78795 | 207881.2 |
| d025_1 | capture | 1 | 204.750 | 206 | 78675 | 208198.3 |
| d026_2 | capture | 1 | 204.750 | 206 | 78639 | 208293.6 |
| d027_3 | capture | 1 | 204.750 | 206 | 78761 | 207971.0 |
| d028_1 | capture | 1 | 204.750 | 206 | 78807 | 207849.6 |
| d029_2 | capture | 1 | 204.750 | 206 | 78826 | 207799.5 |
| d030_3 | capture | 1 | 204.750 | 206 | 78703 | 208124.2 |
| d031_1 | capture | 1 | 204.750 | 206 | 78723 | 208071.3 |
| d032_2 | capture | 1 | 204.750 | 206 | 78746 | 208010.6 |
| d033_3 | capture | 1 | 204.750 | 206 | 78591 | 208420.8 |
| d034_1 | capture | 1 | 204.750 | 206 | 78919 | 207554.6 |
| d035_2 | capture | 1 | 204.750 | 206 | 78615 | 208357.2 |
| d036_3 | capture | 1 | 204.750 | 206 | 78868 | 207688.8 |
| d037_1 | capture | 1 | 204.750 | 206 | 78781 | 207918.2 |
| d038_2 | capture | 1 | 204.750 | 206 | 78674 | 208200.9 |
| d039_3 | capture | 1 | 204.750 | 206 | 78660 | 208238.0 |
| d040_1 | capture | 1 | 204.750 | 206 | 78910 | 207578.3 |
| d041_2 | capture | 1 | 204.750 | 206 | 78896 | 207615.1 |
| d042_3 | capture | 1 | 204.750 | 206 | 78647 | 208272.4 |

- Every burst resets sample index0. Separate snapshots and requested inter-burst gaps are not a continuous IQ stream.
- Native-clock arithmetic horizons differ from CPU-observed trigger/completion intervals and whole-loop elapsed time.
- No-export bench and USB-export capture have different observer/deadtime scopes and are not pooled.
- TX achieved writes/s and mask cycle brackets measure software execution, not independent RF edges or ultimate modulation bandwidth.
- TX late_cycles/late_cycles_max refer to the pending deadline before obsolete slots are skipped; they measure backlog/missed deadlines, not final output-symbol jitter.
- IQ statistics are receiver codes; spectral frequencies assume the configured native clock and are not calibrated RF frequency or power.
- No on-board audio reconstruction, analog-output proof or synchronized end-to-end latency claim.

Exact MMIO/backend/PHY-init mode, observer stride, mask/gap cycles, register words, heap/sensor values, CRC and canary/restoration records remain in structured JSON/CSV. API/void-call completion alone does not prove physical RF cessation or edge shape.
