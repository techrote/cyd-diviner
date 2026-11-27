# Physical CYD baseline findings

Collection is continuing. These findings cover the closed baseline archive,
not the forthcoming I2S-event, complete S3 or peer-traffic supplements. The
source images, exact settings and failed attempts remain in
[PHYSICAL-CAPTURES-BASELINE.zip](PHYSICAL-CAPTURES-BASELINE.zip), SHA256
`41f122a120c5f4207f6d30b01d3c0b4be67728a200ba89154fa3d957d38a7797`.
The [coverage ledger](mission-status.json) distinguishes completed windows from
failed captures and pending tests.

## Observations

The identified CYD is an ESP32-D0WD-V3 revision 3.1 at 240 MHz, with 4 MB flash
and no initialized PSRAM. Its LCD writes use 40 MHz SPI; readback uses 8 MHz.
PCB revision and externally measured ambient temperature remain unknown.

The primary and continuation captures completed 214 scene/SPI/stress windows.
The two captures remain **FAILED** because BLE initialization and a subsequent
kernel allocation respectively stopped their plans. A separate fresh-boot
capture completed all 66 CYD kernel windows. These 280 completed windows are
usable evidence; they do not turn either earlier capture into a success.

| Physical observation | Result | Interpretation |
|---|---|---|
| Full 320×240 RGB565 SPI transfer, 128 samples per repetition | First repetition median 32.280 ms, p99/max 32.293 ms; 153,600 bytes per transfer | About 4.76 MB/s including commands and DMA completion; transferring a full frame alone occupies nearly the 33 ms frame budget. |
| Menu with theme backdrop | 29.4–29.5 completed frames/s | The ordinary frame cap is active; this is not an uncapped compute rate. |
| Fullscreen Gate + Trails with drone/seq/beat | 22.7–22.8 frames/s across three repetitions of each scene | A layered workload needs a lower graphics budget than a simple menu. |
| Decorated landscape Trails | RGB444 about 25.0 frames/s; RGB565 about 28.5 frames/s | Smaller history is not automatically faster; conversion and rendering paths matter. Actual history allocations were 41,760 and 55,680 bytes. |
| USB PCM coexistence | 27,537 valid frames, zero CRC errors and zero internal sequence gaps across 12 windows | No loss was observed between received sequence numbers. Leading/trailing edge loss and analog continuity remain unknown. |
| Scripted touch | Poll and dispatch timings retained separately | Software injection does not measure physical contact-to-photon latency. |
| Histogram insertion A/B, menu | Disabled: 29.4 frames/s in all three repetitions; enabled: 29.4, 29.4, 29.5 | No frame-rate change resolved at this capped workload. Timestamp hooks, frame/audio counts and static storage remain in both variants. |

The primary local-audio windows observed callback maxima of approximately
523 µs for drone, 759 µs for sequencer and 325 µs for beat. The primary combined
graphics/audio windows retained approximately 5.33 ms audio periods without
periods exceeding the profiler's 6 ms late threshold. These are finite observed
bounds with ordinary callback, mutex, mixing and blocking I2S work separately
instrumented. Primary firmware had no driver-event queue; it cannot directly
count DMA underruns or prove gap-free analog output.

## Sustained CPU load and sensor readings

The target completed a fixed **365.000-second** stress window and retained 355
sensor/CPU/heap samples spanning 362.939 seconds. Weighted tick observations
showed core 0 busy **98.995%** and core 1 busy **99.960%**. No target sample-ring
drops or reset were observed in this window. Tick sampling is not cycle-exact
and includes its documented scheduling/interrupt attribution limits.

Every retained stress sensor reading was raw code **128**, with the historical
uncalibrated conversion **53.333 °C**. The raw SDK function is undocumented;
the original `ESP32_ROM_raw_uncalibrated` tag is preserved verbatim, but link/map
inspection resolves an SDK RTC-library object, not proven mask ROM. The constant
code establishes neither an accurate die temperature nor a measured thermal
rise. An independent calibrated probe was unavailable. Raw readings throughout
the campaign, including changed codes during radio initialization, are retained.

## Memory and benchmark context

After radio initialization and teardown, the CYD had about 75 KB free but a
largest free block of only 31,732 bytes. Its 40,016-byte benchmark workspace
allocation failed. The same workspace and all 66 kernels succeeded after a fresh
boot with radios inactive. This demonstrates a contiguous-allocation limit in
that measured runtime history; total free heap alone was insufficient.

The profiler itself reserves 24,008 bytes for sample/histogram storage, in
addition to other instrumentation and ordinary application state. Production
headroom cannot be obtained by treating the profiling image as an uninstrumented
image or by simply subtracting that static total from every heap observation.

Representative CYD first-repetition medians for 128 output samples were
80 µs/28 µs for float/Q15 oscillation and 75 µs/50 µs for float/Q15 filtering.
The scalar 1,024-point complex FFT median was 1,405 µs; the 12,288-cell cellular
step median was 4,977 µs. Each kernel window retains 256 actual step timings,
three excluded warmups, seed 23063, output dimensions/checksums and finite/range
checks. Comparative tables must use all repetitions and preserve their memory
and concurrent-task context. The fresh CYD image still has ordinary Diviner
workers; the initial S3 image is standalone and uses 80 MHz flash rather than
the CYD's 40 MHz flash.

The S3 capture completed five windows before a truncated USB status record
stopped collection. A separate read-only probe reproduced truncation while the
board continued responding. Its temperature function is called before the JSON
prefix, so the cutoff at a temperature field does not establish a blocked sensor
driver. The raw failure is retained. A bounded complete-record USB output fix
and a fresh full internal/PSRAM capture are pending separate image approval.

## Provisional VIBEWARE budgets

Use **20 frames/s** as an initial layered fullscreen design target on this CYD,
and validate each chosen effect combination. Simple capped menus can target
about 29 frames/s. A 30 frames/s full-screen RGB565 transfer leaves little time
for additional work in the measured SPI path. Neither number is a hard guarantee.

Keep the actual 128-sample/24 kHz **5.333 ms** audio cadence explicit. The
observed sub-millisecond callbacks provide useful local compute margin, but
blocking I2S time, mutex waits, interrupt load and radio coexistence need their
own checks. Do not interpret average callback time as a scheduling guarantee.

Track largest allocatable blocks as well as total heap; allocate substantial
long-lived workspaces before radio churn when evaluating a design. This is an
architectural recommendation from the observed allocation failure, not an
optimization implemented in Diviner.

Offload conclusions remain pending measured peer latency/goodput and complete
S3 timings. Full-frame transfer has a local SPI ceiling even if computation is
moved elsewhere. A candidate must beat local compute after serialization,
transport, peer computation, return bytes and tail latency; no offload win has
yet been physically demonstrated.
