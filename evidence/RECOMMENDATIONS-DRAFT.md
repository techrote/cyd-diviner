# CYD SYNTH VIBEWARE recommendations — DRAFT / HOLD

Prepared from the **closed classic-CYD baseline and completed S3 internal/PSRAM
kernel supplement**, closed owned-peer wireless/I2S-event supplements, and the
separately identified colour, PCM, original raw-RF and first bare-metal
dual-receiver SDR experiments, including the closed faster paced TX and
driver-free RX follow-up. Source validation and image builds remain separate
from physical performance measurements.
Cross-board ratios describe the measured configured systems. This document
asserts no isolated hardware ISA advantage, ultimate wireless capacity,
successful computational offload, physical one-way audio latency or calibrated
cross-chip thermal comparison.
Retain the failed baseline attempts alongside their independently completed
windows; a partial capture is not a completed campaign.

## Evidence and scope

The [baseline findings](BASELINE-FINDINGS.md),
[CYD kernel report](campaign-cyd-kernels/REPORT.md),
[kernel summary JSON](campaign-cyd-kernels/summary.json),
[scene report](campaign-full/REPORT.md) and
[continuation report](campaign-continuation/REPORT.md) are the inputs. Raw
captures are archived in [PHYSICAL-CAPTURES-BASELINE.zip](PHYSICAL-CAPTURES-BASELINE.zip),
SHA256 `41f122a120c5f4207f6d30b01d3c0b4be67728a200ba89154fa3d957d38a7797`.

The measured classic board is ESP32-D0WD-V3 revision 3.1, 240 MHz, 4 MB flash
at 40 MHz and no initialized PSRAM. Kernel source is
`a4549f87cef05d19d9acf96d59ae95a2435ebb67`; firmware SHA256 is
`fb35d75725b15ecd8b6aceb368492cf76ebaee85bbb4aeb50bd61311e229f15c`.
The complete fresh-boot kernel input results SHA256 is
`cb4d53d91c8d625acab8faed07bbf91aa58a726ccc7e65d6a0510b09ed0dc41d`.

All 66 fresh-boot kernel repetitions completed: 11 kernels, two sizes, three
repetitions, 256 exact timings each, seed 23063; **16,896 timed steps** total.
Each table cell below pools only its matching three repetitions, **n=768**.
Allocation, initialization, three warmups, yields and output inspection are
excluded; RTOS preemption during a step is included. The old image's internal
placement is inferred from its zero-PSRAM board, rather than a capability
allocation receipt. It still has ordinary Diviner audio/display workers.

The completed S3 input is
[`physical/s3-kernels-link-03/results.json`](physical/s3-kernels-link-03/results.json),
SHA256 `d89c28832226ddbe53db4efa2a1faa6876d1eb531ef3fb7d1f5c7017405a312d`.
Its source is `174a79670d4b0da656ebd615f0cd1620aa0ddbb1`, source digest
`1b7d5aacd09d257dff759f95413047b74fe2b6de7592b7a3f147e39aea0c07ed`,
firmware SHA256
`7ac5bf7f79bb2ed6048a40367fe1d3966a1d5e53a999b2f09f28f310456bb681`,
profile `kernel-s3-n4r2-peer`. All **132 repetitions completed**: both explicitly
verified memory regions, 11 kernels, two sizes, three repetitions and 256 timed
steps, or **33,792 exact timings**. Each region/case cell below has n=768.
The earlier failed S3 USB/identity attempts remain separate captures.

The S3 is ESP32-S3 revision 0.2 as identified by esptool, configured at 240 MHz,
with 4 MB flash at **80 MHz** and physically identified 2 MiB quad PSRAM
(initialized heap API reports 2,095,087 bytes). It is a standalone image without
ordinary Diviner audio/display workers; the idle peer worker and USB buffer are
present, with link mode requested off and inactive in the retained receipt.
The CYD profiling frame loop pauses during each kernel command while its
background audio/display task infrastructure and observers remain. The
portable `prepare`, `step` and `inspect` algorithms are unchanged between the
two sources. Allocation, output and outside-sample heap-observer wrappers
changed; all timings still enclose only `step`. These differences preclude
isolating CPU/FPU/ISA advantage from the observed configured-system ratios.
C3 remains compile evidence until physically captured.

The expanded experiments retain their own source/image and observation scopes:
[colour findings](COLOR-FINDINGS.md), [PCM findings](PCM-FINDINGS.md),
[SDR calibration](SDR-CALIBRATION-INTERPRETATION.md) and
[finite proprietary SDR proof](SDR-PROOF.md). Their measurements are not pooled
with the baseline. The colour/packet-audio image uses source
`ad9cbfd670e456de5e08fca19810fb93f7f1db47`; the original classic SDR transmitter
uses `bb34b3c27c4c8388956bb05a8a5280d7a2c687a2`. The reports and flash receipts
pin exact image hashes, receiver source, SDK and analysis provenance.

## Local numeric format: choose per workload

The measurements reject a blanket assumption that fixed point is faster.
These are implementation-specific comparisons with the precision described
in [KERNELS.md](../tools/perf/KERNELS.md), including Q30 fixed state and
coefficients in the filter/modal paths and quantized Q15 outputs. They are
not instruction-level FPU claims or proof of interchangeable long-term sound.

| Case | float median / p99 µs | fixed median / p99 µs | Measured median comparison |
| --- | --- | --- | --- |
| Interpolated sine, 128 samples | 80 / 81 | 28 / 28 | Fixed 2.86× faster |
| Interpolated sine, 512 samples | 318 / 319 | 109 / 110 | Fixed 2.92× faster |
| One-pole filter, 128 samples | 75 / 75 | 50 / 50 | Fixed 1.50× faster |
| One-pole filter, 512 samples | 297 / 306 | 192 / 192 | Fixed 1.55× faster |
| Modal, 8 voices × 128 samples | 196 / 197 | 304 / 305 | Float 1.55× faster |
| Modal, 32 voices × 512 samples | 1,916 / 1,924 | 3,713 / 3,715 | Float 1.94× faster |

The existing host numeric checks found at most one Q15 LSB of fresh-block
difference for the tested DSP comparisons; that host result does not establish
long-duration numeric equivalence. The host oracle used seed 343709; the
physical matrices use seed 23063. Every one of the 22 terminal fingerprints,
output minima/maxima/sums and finite flags matched across CYD, S3 internal RAM
and S3 PSRAM, with stable repeats. All physical timing summaries independently
recomputed from the retained raw samples. This detects no numeric regression
in the observed outputs; 32-bit fingerprints and summary equality do not bound
every sample error or establish general accuracy. FFT's legacy fingerprint field is explicitly the XOR of separate
FNV-1a real/imaginary-array fingerprints.

Recommendation: retain both portable numeric implementations as comparison
kernels. Select arithmetic only after measuring the intended voice count,
precision, state lifetime and actual target. No Diviner DSP replacement is
proposed or implemented by this mission.

## Observed S3 execution and memory placement

The larger-case table uses the same seed, dimensions, precision and timed-step
scope on each target. Ratios are CYD median divided by S3 internal median;
the complete 22-case, two-region comparison is retained in
[S3-COMPARISON.md](S3-COMPARISON.md), with independently inspectable
[JSON](S3-COMPARISON.json) and [CSV](S3-COMPARISON.csv). Units are
microseconds, and p99 is exact nearest rank from n=768 samples per cell.

| Larger kernel | CYD median / p99 | S3 internal median / p99 | S3 PSRAM median / p99 | Observed CYD/S3 internal ratio |
| --- | --- | --- | --- | --- |
| Oscillator float, 512 samples | 318 / 319 | 265 / 265 | 265 / 265 | 1.200× |
| Oscillator fixed, 512 samples | 109 / 110 | 97 / 97 | 97 / 97 | 1.124× |
| Filter float, 512 samples | 297 / 306 | 248 / 248 | 248 / 248 | 1.198× |
| Filter fixed, 512 samples | 192 / 192 | 169 / 169 | 169 / 169 | 1.136× |
| Modal float, 32×512 voice samples | 1,916 / 1,924 | 1,741 / 1,741 | 1,741 / 1,741 | 1.101× |
| Modal fixed, 32×512 voice samples | 3,713 / 3,715 | 3,346 / 3,347 | 3,346 / 3,347 | 1.110× |
| FFT float, 1,024 complex points | 1,405 / 1,414 | 1,350 / 1,350 | 1,350 / 1,350 | 1.041× |
| Cellular, 128×96 cells | 4,977 / 4,979 | 4,065 / 4,067 | 4,065 / 4,067 | 1.224× |
| Particles, 512 states | 90 / 91 | 73 / 74 | 73 / 74 | 1.233× |
| Indexed feedback, 160×120 | 4,489 / 4,528 | 3,974 / 3,974 | 4,021 / 4,047 | 1.130× |
| RGB565 generation, 320×16 pixels | 322 / 331 | 301 / 301 | 301 / 301 | 1.070× |

Across all 22 cases, the observed internal-RAM median ratio ranges from
**1.041× to 1.233×**. The S3's own arithmetic ordering agrees with the CYD:
fixed oscillation is 2.68× / 2.73× faster for representative/larger blocks,
fixed filtering 1.41× / 1.47×, while float modal synthesis is 1.57× / 1.92×
faster than its fixed implementation. These numbers describe the supplied
scalar kernels and numeric formats; no vectorized implementation was tested.

S3 PSRAM and internal medians are identical at the retained microsecond
resolution for **21 of 22 cases**. Larger indexed feedback is 4,021 versus
3,974 µs, approximately **1.18% slower in PSRAM**, with p99 4,047 versus
3,974 µs. This is a warmed, repeated workload with bounded active contexts,
not a streaming PSRAM bandwidth, cold-cache or concurrent DMA benchmark.
It does not establish equal latency for arbitrary working sets. PSRAM expands
available capacity on this board; these results do not justify assuming an
unbounded speed benefit or penalty for a different graphics/audio layout.

The S3 output transport reported `telemetry_write_failures=1` at the initial
state and at completion, with zero short writes throughout retained states.
There was **no in-capture counter increase** and no parse error in this
complete matrix. The host logged **one progress recovery query** after a
partial kernel record stalled for approximately 0.809 seconds; the next byte
arrived approximately 0.001405 seconds after that `perf sample` query. The
query occurs after the kernel record starts, hence after its timed steps, and
the completed stream is retained intact. This demonstrates completion with
the logged host recovery mechanism, not generally stall-free native USB.
The pre-existing failure counter is retained; it is not silently reset or
attributed to a measured kernel. Earlier truncated captures
still remain failed rather than being replaced by this successful rerun.

## Display, audio and memory budgets

Use **20 frames/s as an initial layered-fullscreen design target** on the
measured CYD and validate each effect combination. Gate + Trails combined
with drone, sequencer or beat produced approximately 22.7–22.8 frames/s over
the captured repetitions. Menus with the theme backdrop produced 29.4–29.5
frames/s under the existing 33 ms cap. These are finite observations, not
guaranteed sustained or uncapped rates.

The dedicated 320×240 RGB565 transfer benchmark moved 153,600 bytes per
presentation. Across **384 exact samples**, its median was **32,281 µs** and
p99/max **32,293 µs**, equivalent to approximately **4.76 MB/s** of pixel
payload including command overhead and DMA completion. Full-frame transfer
alone nearly occupies a 33 ms budget. Remote computation cannot remove that
local SPI cost. Direct Visions rendering interleaves drawing and transfer;
subtracting this separate strip benchmark from a scene's duration would not
isolate that scene's CPU time.

For decorated landscape Trails, RGB444 used 41,760 bytes of history while
RGB565 used 55,680 bytes; the latter was faster in the observed path, about
28.5 versus 25.0 frames/s. Memory savings and rendering speed are separate
tradeoffs. Fullscreen uses a different bounded geometric trail path, so its
RGB444/RGB565 labels do not imply full-screen pixel-history allocations.

The later [packed-colour matrix](COLOR-FINDINGS.md) completed all **36/36
windows**, twelve settings with three repetitions each. On that same later
image, native fullscreen Gate measured **25.00 FPS** with one effect and
**22.33 FPS** with Gate+Trails. The added RGB111 conversion path measured
**7.89/3.67 FPS**, and RGB444/RGBA5551 each **7.00/3.00 FPS**, for one/two
effects. Plasma+Kaleido and Ashfire+Rain also exercised the current maximum
of two effect paths: 3.67 FPS for RGB111 and 3.00 FPS for RGB444. This tests
existing base/overlay combinations, not arbitrary resident layer capacity.

Those are genuine packed **eight-row intermediates around an RGB565 raster**.
The two-effect RGB111/RGB444/RGBA5551 layouts use 2,240/8,000/10,240 bytes of
packed payload, including external coverage where applicable. Total borrowed
scratch is 12,480/18,240/20,480 bytes including the unchanged two RGB565 DMA
strips. **Every case retains the 64,800-byte backing reservation and 16-bit
LCD transfers.** RGBA5551 coverage comes from changed pixels after the existing
RGB565 blend; it is not a native one-bit-alpha rasterizer. The measurements
show this converter's cost, not a hardware ceiling for direct packed rendering
or lower-depth LCD operation. Use the native path as VIBEWARE's measured
baseline; a direct packed renderer would need a separate matched experiment.

Preserve the configured **128-sample / 24 kHz = 5.333333 ms** audio cadence.
Across the matching landscape/local cases, each with **5,625 callback
observations**, exact callback maxima were **523 µs drone, 759 µs sequencer
and 325 µs beat**. These maxima do not include every independent scheduling,
lock, overlay, mix or I2S cost, and those independent maxima/percentiles must
not be added as if they came from the same block. Blocking I2S enqueue time
includes waiting for DMA space. Primary firmware had no driver-event queue;
its analog continuity and directly detected underflow counts remain unknown.

Recommendation: keep sample generation, buffering and the audio clock local
unless a later complete remote-job capture proves the needed deadline,
delivery behavior and recovery policy. Use wireless first for noncritical
control, analysis or visual state. New-image radio-on versus radio-off/idle
comparisons must use the **same new image**; event collection, worker tasks,
USB output and memory observers differ from the primary baseline.

Largest free blocks are an architectural budget, not just total heap. After
radio churn the CYD retained about 75 KB free but a largest block of **31,732
bytes**, insufficient for the **40,016-byte** common kernel workspace. All
66 kernel repetitions succeeded after a fresh boot. This is evidence of a
contiguous-allocation limitation in that history, not proof of a leak or a
general inability to execute the algorithms. The active algorithm contexts
are smaller than the common allocated workspace; both quantities are recorded.

Heap capabilities must match the allocation. The older `ESP.getFreeHeap()`
and largest-block observations query internal memory and can include regions
that are not byte-addressable. A byte array, formatter or packet workspace
needs the matching `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` free/largest-block
observations and an actual successful allocation. The subsequent PCM setup
failures are consistent with byte-capable allocation pressure, even after a
fresh boot; the precise failed allocation is not independently identified.
The short ACK/run records fit their declared formatter capacities, so a large
generic heap value does not resolve that failure. Preserve telemetry capacity
and admission checks as part of the workload's resource budget.

Recommendation: budget large persistent workspaces and radio lifecycles
together, monitor successful allocation and largest-block minima, and test
the actual scene-transition history. The profiling image itself reserves
**24,008 bytes** in sample/histogram arrays, with other observers/tasks
additional. Its heap cannot be converted into ordinary firmware headroom by
subtracting that figure alone. No allocation optimization is implemented here.

## Packet audio: finite digital reconstruction, substantial concealment

The [PCM prototype](PCM-FINDINGS.md) uses ESP-NOW broadcast, synthetic PCM16,
one TX admission at a time, bounded callback/assembly queues and a two-block
receiver reserve. It has no application retransmission or unicast MAC ACK.
Across eight closed stream-attempt directories, **50 of 195 attempted
three-second windows validate**, including three classic-CYD receiver windows;
setup-only diagnostics are separate. The matched S3 live-metadata cohort
validates **22/48**, and deferred metadata **25/48**. Both captures remain
**FAILED**, with missing receipts, setup/readiness failures and timeouts
retained. The two policies are distinct measurement contexts.

At **24 kHz stereo, 32 frames/block, requested 6 Mbps**, observed nonconcealed
source coverage was **87.87–88.84%** in the live cohort and **86.62–87.56%**
in the deferred cohort. Thus roughly **11–13%** of rendered blocks required
PLC even in this best observed setting. The deferred cohort has no valid
reverse-direction repetition there. Source/error digital SNR was approximately
8.1–9.3 dB across those cases and includes concealment/recovery behavior; it
is not RF SNR or analog fidelity. These finite results do not support choosing
this prototype as a transparent streamed-audio dependency.

Application receive deficits cannot be relabelled RF packet loss. One valid
live run had 4,016 API-accepted TX packets but only 31 accepted receiver blocks,
with **3,891 future-window rejections**; that explains most of its deficit in
receiver scheduling/window admission, rather than demonstrating 99% RF loss.
Other unaccounted deficits remain unattributed. Zero CRC/callback-drop counts
do not prove perfect radio delivery. Live readiness observed power-save enum
0 only at the query; ten other attempts failed because readback was enum 1.
Deferred cases stop radios before export and lack active power-save readback;
API acceptance of a requested PHY rate is not an over-air rate measurement.

Packet size imposes a useful sanity check before RF testing. Stereo PCM16,
eight frames at 24 kHz, needs 3,000 packets/s. A 32-byte payload plus the
20-byte application header already offers **1.248 Mbps** before MAC/PHY
overhead, exceeding a requested 1 Mbps PHY. Larger blocks reduce framing cost
but increase serialization/reserve. The configured two-block reserve is
0.667/1.333/2.667 ms for 8/16/32 frames at 24 kHz, measured relative to the
receiver worker's first admission. These are arithmetic buffering intervals,
not synchronized one-way latency. There is no ADC, DAC/I2S playback or analog
loopback in these digital sink measurements. Keep the local audio clock and
sample generation in the current VIBEWARE architecture.

## Proprietary SDR: working finite low-rate proof, continuous audio unproved

The [original raw-RF proof](SDR-PROOF.md) recovered experimental NRZ/TDM-PDM
frames through classic ESP32 OOK carrier keying and S3 IQ capture with batch
host decoding. It is separate from optional packetized PDM-over-ESP-NOW and
does not rely on its packet transport. At **23,000 symbols/s**, three runs
each recovered **15/16 frames**, with **zero errors in 3,840 compared payload
bits per run**. Missing leading sequence 0 is not counted correct: source-frame
coverage is **93.75%**. Lower-rate mono also had observed sequence-0 corruption;
the report retains the failed original matrix and incomplete acquisition.

The 23 ksymbol/s source is only **2 kHz stereo at OSR4**, reconstructed by a
boxcar, not 24/48 kHz audio. On recovered sequences it exactly matches ideal
boxcar output, yet source/error SNR is **6.74/6.03 dB** because the codec itself
is coarse. A 368-symbol frame nominally occupies **16 ms** and carries 32
stereo source frames. That serialization arithmetic is not measured audio
latency. No continuous on-device demodulator/playout, live input, shared clock,
range sweep or calibrated RF power/noise measurement ran. The finite physical
path is established; its suitability for continuous low-latency audio is not.

Calibration also separated radio observation from capture failure. Initial
API 10 ksymbol/s aborted its software schedule; later RAM keying showed
1–20 ksymbol/s envelope periodicity at 250 kS/s, despite substantial indexed
USB sample gaps. Earlier 125 kS/s silence was confounded by a carrier roughly
82.6 kHz from the receive centre, outside that capture's passband. After
empirical recentering, 125 kS/s framed captures were gap-free. These findings
are not an ESP32 symbol-rate floor or ceiling; see the
[calibration interpretation](SDR-CALIBRATION-INTERPRETATION.md).

The new isolated **SDR-metal** work removes GUI/audio and streaming USB from
timed acquisition. Its [TX projects](../experiments/sdr-metal/tx-s3/README.md)
initialize the SDK tone once, then use target-specific, ROM/library-derived
first-tone MMIO gate/amplitude words. Preallocated internal buffers, a 240 MHz
cycle loop, unpaced mode and configurable bounded IRQ-masked segments expose
software write timing without the earlier fixed baud ceiling. The
[raw RX snapshot](../experiments/sdr-metal/rx/README.md) retains at most 16,380
native IQ words in a checked SRAM aperture, with no decimation/FIR or USB
output during capture. Nominal 16/40/80 MS/s selections provide only
1,023.75/409.5/204.75 µs of aperture per burst; snapshot deadtime is explicit.
Neither write cycles nor nominal ADC rate proves RF edge timing or continuous
bandwidth. The [dual-receiver method](../tools/perf/SDR-METAL-DUAL.md) keeps
shared exposure distinct from synchronized clocks.

The first metal RX image watchdog-reset inside PHY DC calibration before
runtime identity/capture. The [startup failure](metal-startup-failure.json)
is preserved. The corrected source balances explicit Wi-Fi power-domain
ownership around direct PHY enable/disable, following the pinned SDK's test
and Wi-Fi setup order; it does not suppress calibration or the watchdog.
Source `3b4161fbcbd6832264019aa3a4fb993a85798857` was separately approved and
flashed, with [idle runtime verification](metal-power-flash-receipt.json).
The first closed physical cohort uses WiFi_NULL RX initialization because the
direct path's gain checks fail on both receivers. Native capture timing is
consistent with configured sample horizons, while repeated no-export loops
retain about 1.18 ms of aggregate setup/check cost per aperture. Deferred USB
readout takes about 264 ms versus 83 ms on the two boards; its component cause
is unresolved. The native framed decoder recovers known headers and payloads,
with exact conditional BER and cropping domains in the
[framed findings](METAL-FRAMED-WIFI-FINDINGS.md). No continuous on-board audio,
synchronized one-way latency or ultimate hardware limit follows. A separate
firmware cohort tested the direct-RX SDK receive-enable prerequisite and a
minimal paced TX loop that preserves source words when late.

That [fast follow-up](METAL-FAST-FINDINGS.md) completed all 78 finite TX windows
and 18 receiver-only reference windows. Both receivers now pass direct-PHY
tuning. The fast loop sustains requested 1/2.208/4.416/8 million software
writes/s in the alternating-source sweep; higher requests plateau near 9.88
million writes/s for this loop. Native envelope peaks track the tested
alternating patterns through 8 Mbaud, independently of the software count.
This is a tested implementation limit, not an ultimate PHY ceiling.

Finite four-channel 48 kHz/OSR16 patterns at requested 4.416 Mbaud recover
matching headers in all three windows on each receiver. Known-payload
comparison gives 53 errors/3,072 bits on COM22 and 41/2,816 on COM23; these are
different accepted frame populations and do not rank receiver quality. No
matching headers recover at 8.832 Mbaud in three windows per receiver. The
prototype accepts distorted payload bits without retransmission, but has not
established continuous on-device audio output, intelligibility or synchronized
one-way latency. Treat it as a basis for further link/decoder work, not a
replacement for the measured packet-audio delivery/deadline characterization.

[The 576-reading temperature ledger](metal-fast-temperatures.json) preserves
TX driver indications up to 98.9 °C, outside its configured 10–80 °C request
range. Accuracy is externally unverified; no thermal explanation for decoding
failure follows. The two audio profilers were restored and the TX left idle,
as verified in [the final-state receipt](metal-final-audio-restore-receipt.json).

## Candidate heterogeneous work

The following table uses the **larger measured classic-CYD cases**, each
n=768. Compute per output byte is derived from the measured median and actual
kernel output size. It is an output-only screening measure; it omits required
input/state upload, peer compute, transport and serialization.

| Workload | local median / exact p99 µs | current output bytes | median µs per output byte | Candidate product boundary, not a measured offload result |
| --- | --- | --- | --- | --- |
| Modal float, 32×512 voice samples | 1,916 / 1,924 | 1,024 | 1.8711 | Prefer remotely generated modal/control parameters before attempting streamed audio blocks. |
| FFT float, 1,024 complex points | 1,405 / 1,414 | 8,192 | 0.1715 | Test compact spectral features; live input samples still require transfer or local acquisition. |
| Cellular, 128×96 cells | 4,977 / 4,979 | 12,288 | 0.4050 | A peer could retain simulation state and return selected events/density/control values. Initial state and resynchronization still matter. |
| Indexed feedback, 160×120 | 4,489 / 4,528 | 19,200 | 0.2338 | Compact warp/excitation controls are a better first experiment than returning every indexed pixel. |
| Particles, 512 states | 90 / 91 | 8,192 | 0.0110 | Local update is inexpensive relative to this bulk result size; retain locally unless a different heavier workload is demonstrated. |
| RGB565 strip generation, 320×16 | 322 / 331 | 10,240 | 0.0314 | Local generation is inexpensive; physical SPI remains separate. |

Oscillation/filter blocks are cheaper still in the representative cases:
28 µs for 128 fixed oscillator outputs and 50 µs for 128 fixed filter outputs.
Moving such individual jobs requires exceptionally low total round-trip
overhead to beat local work; no such link result is asserted here. Batching
can amortize overhead, but changes latency and state ownership and therefore
requires an actual batched-job measurement.

Compact returns are a design proposal. For example, returning selected
features or parameters can reduce bytes, but the benchmark currently returns
the full arrays listed above. Do not present a reduced feature payload as a
measured result or omit live input upload by replacing it with a deterministic
benchmark seed. A seed can reproduce the synthetic kernel input; it cannot
encode arbitrary live audio or arbitrary user state.

The implemented common link frame has a **40-byte application header**.
ESP-NOW/BLE cases currently carry at most **200 payload bytes** per frame;
the socket path permits **1,024**. A 64-byte payload therefore produces a
104-byte application frame, and a 200-byte payload a 240-byte frame, before
transport/PHY overhead. Large arrays above require additional application
framing/fragmentation or a supported socket stream. The current directed GATT
adapter intentionally rejects oversize/fragmented writes; its measurements
must not be treated as measured 8–19 KB job transfers.

## Offload break-even worksheet — matched-job transfer terms unmeasured

Use the same job, output precision, data dimensions, memory region and workload
context when filling this worksheet. These are screening equations; summed
independent percentiles are not the measured p99 of an end-to-end job.

Let `C_L` be local compute in µs, `C_P` peer compute in µs, `P` packing/unpacking
and job-queue work in µs, `B_in/B_out` actual useful request/result bytes, and
`G_in/G_out` measured useful payload goodput in bytes/s. If transport overhead
`R_fixed` has been independently separated from payload serialization, the
candidate condition is:

```text
C_L > C_P + P + R_fixed
      + 1,000,000 * (B_in / G_in + B_out / G_out)
```

For a simplified equal-goodput model `G`, the required useful compute saving
per transferred byte would be:

```text
(C_L - C_P - P) / (B_in + B_out)
    > R_fixed / (B_in + B_out) + 1,000,000 / G       [µs/byte]
```

The measured S3 internal medians now supply a peer-compute screening term.
Subtracting the observed S3 median from the CYD median gives only a small
transport/packing allowance for executing the **same** scalar job remotely:

| Larger job | CYD median µs | S3 internal median µs | Arithmetic difference µs | Known output bytes |
| --- | --- | --- | --- | --- |
| Modal float | 1,916 | 1,741 | 175 | 1,024 |
| FFT float | 1,405 | 1,350 | 55 | 8,192 |
| Cellular | 4,977 | 4,065 | 912 | 12,288 |
| Indexed feedback | 4,489 | 3,974 | 515 | 19,200 |
| Particles | 90 | 73 | 17 | 8,192 |
| RGB565 strip generation | 322 | 301 | 21 | 10,240 |

These differences compare the recorded contexts and are not a causal estimate
of an isolated S3 CPU advantage. Even this optimistic screening leaves only
17–912 µs for all additional request/result transfer, packaging and scheduling
in the listed cases. Actual live input/state bytes, payload-specific transport
terms and a real job RPC remain missing. There is **no demonstrated offload
win**. A redesigned peer-retained simulation with compact feature returns is
a different workload that must be measured separately.

When using the measured echo RTT at a particular payload size, payload
transfer is **already included**. Do not add that same payload's `B/G` term
again and call the result an identified exact break-even. A matched-size echo
RTT plus incremental peer compute/packing is a useful screening bound only
when the request/result shape and queueing behavior match. The decisive
measurement is a real job RPC, timed on one initiating clock from submission
through validated result receipt, including peer processing and the intended
concurrent audio/display workload.

| Worksheet term | Future evidence to use | Current draft status |
| --- | --- | --- |
| `C_L` | Matching CYD `summary-kernels.csv` median, exact p99/max, precision, output dimensions and sample count | Measured for the listed cases |
| `C_P` | Closed S3 internal or PSRAM kernel scope with matching algorithm/seed/size; source, flash clock and worker context recorded | Measured for the matching scalar kernels; context differs as disclosed above |
| Payload goodput | `network.receiver_payload_goodput_bytes_per_s`, peer unique payload bytes and initiator target window; preserve topology/payload/rate | [Physical wireless findings](WIRELESS-FINDINGS.md), 22 workload types; two phone TCP cells retain 2/3 valid repetitions |
| RTT distribution | `network.rtt_samples`, `rtt_p95_us_upper`, `rtt_p99_us_upper`, exact `rtt_max_us`; conditional on valid replies within the receive tail | Measured ESP-NOW/SoftAP/phone AP echo; no GATT or real job RPC result |
| Delivery evidence | Sender API acceptance, peer unique RX, response counts, CRC errors, duplicates, too-old frames and queue drops | Retained per-run; API admission is not delivery and loss attribution is incomplete |
| Local degradation | Same-image matched idle versus traffic FPS, audio cadence/callback/lock/mix/enqueue, I2S driver events, CPU and heap | Retained; about 22.2 FPS fullscreen idle versus 17.5–19.0 during measured traffic; analog continuity unknown |
| `P`, actual job sizes and RPC completion | A real computational job including state transfer, peer computation and result validation | Not measured by echo/goodput tests alone |

Paced traffic demonstrates the offered workload that was actually captured.
For example, a 200-byte/50-packet-per-second case offers 10,000 payload bytes/s
by configuration; it does not establish maximum BLE capacity. Unsaturated
cases, host UART arrival timing and PHY advertised rates cannot supply missing
maximum-capacity or tail-latency values. A larger TCP throughput result also
does not establish the same rate or latency for a small GATT or ESP-NOW job.

## Sustained load and temperature interpretation

The completed CYD stress window was **365.000 seconds** with **355 retained
samples spanning 362.939 seconds**. Weighted tick observations were **98.995%
core 0** and **99.960% core 1**, with no retained-sample drops or reset in that
window. This demonstrates sustained near-full observed CPU occupancy under
the stated tick-sampling method; it is not cycle-exact arithmetic utilization.

The [I2S-event supplement](campaign-events/REPORT.md) independently completed
another **365-second** stress window, with **356 retained samples spanning
363.605 seconds** and weighted busy observations **98.987% / 99.951%**.
Its raw sensor codes were **139–146**, first 139 and last 142. The arithmetic
conversion is about **59.4–63.3 °C**, explicitly uncalibrated. It is a different
boot/radio history from the first stress run; neither establishes a calibrated
thermal rise. Across the event campaign, **124,687 TX_DONE notifications**
matched completed/drained blocks, with zero observed queue-full/DMA-fault
notifications. Independent analog continuity and lost-notification counts
remain unknown.

Every retained sensor reading in the **first** stress window was raw **128**.
The later fresh-boot
kernel snapshots contained raw **137–139**. Keep those observations and their
separate run contexts. The classic RTC-library indication is undocumented and
uncalibrated; the retained Fahrenheit-derived number is not a calibrated die
temperature. Neither the constant stress code nor changed codes between
captures establish thermal rise, thermal margin or throttling. S3 driver
Celsius indications use a different method and must not be numerically
compared as a calibrated cross-board thermal test. The completed S3 kernel
capture retained **265 target state records** with driver Celsius indications
**37.1–42.1 °C**. The range spans interleaved compute, yields and telemetry;
it is not an S3 360-second near-full-CPU stress test or a calibrated thermal
rise measurement.

The later [temperature ledger](TEMPERATURE.md) also retains **72 in-window
colour readings at classic raw 136–138**, and **100 post-stop PCM readings**
in `pcm-temperature/`: three classic raw readings at 128 and 97 S3 driver
Celsius indications. Validated S3-pair PCM cohorts span **43–68.1 °C** across
endpoints and metadata policies. Those short, post-window readings do not
extend either stress window, demonstrate sample-by-sample thermal behavior
or establish a calibrated cross-chip comparison.

## Reproduction and immutable evidence

The [evidence index](README.md) maps reports to complete raw archives. Extract
their retained `physical/` prefixes; keep failed, partial and zero-run attempts.
Hashes, member lists and verification are in the paired receipts:

| Closed scope | Raw archive | Receipt |
| --- | --- | --- |
| Primary, continuation, classic kernels and initial S3 attempt | [Baseline](PHYSICAL-CAPTURES-BASELINE.zip) | [baseline receipt](baseline-archive-receipt.json) |
| Events/stress, complete S3 matrix and owned-peer wireless | [Supplement](PHYSICAL-CAPTURES-SUPPLEMENT.zip) | [supplement receipt](supplement-archive-receipt.json) |
| Packed-colour matrix | [Colour](PHYSICAL-CAPTURES-COLOR.zip) | [colour receipt](color-archive-receipt.json) |
| Classic/S3 PCM attempts and setup diagnostic | [CYD PCM](PHYSICAL-CAPTURES-PCM-CYD.zip) | [CYD receipt](pcm-cyd-archive-receipt.json) |
| Matched-S3 PCM attempts | [S3 PCM](PHYSICAL-CAPTURES-PCM-S3.zip) | [S3 receipt](pcm-s3-archive-receipt.json) |
| Original SDR calibration and framed attempts | [SDR](PHYSICAL-CAPTURES-SDR.zip) | [SDR receipt](sdr-archive-receipt.json) |
| Initial metal timing, rate, framed and receiver-exchange attempts | [Metal](PHYSICAL-CAPTURES-METAL-INITIAL.zip) | [metal receipt](metal-initial-archive-receipt.json) |
| Direct-PHY, fast TX, framed and compatibility follow-up | [Fast metal](PHYSICAL-CAPTURES-METAL-FAST.zip) | [fast receipt](metal-fast-archive-receipt.json) |

The [Windows capture package](WINDOWS-CAPTURE-PACKAGE.zip) and its
[receipt](windows-package-receipt.json) pin portable tools, plans, tests and
source, without private credentials, backups or automatic flashing. The
[colour method](../tools/perf/COLOR.md),
[PCM method](../tools/perf/AUDIO-MVP.md),
[original SDR protocol](../experiments/sdr/PROTOCOL.md) and
[metal method](../tools/perf/SDR-METAL.md) describe their distinct experiments.
Reviewed original colour/audio/SDR apps and exact adapted GPL receiver source
are in [the prototype bundle](UNAPPROVED-AUDIO-SDR-IMAGES.zip)
([receipt](prototype-bundle-receipt.json)); corrected metal apps, ELF/maps and
source/licenses are in [the power-domain correction bundle](UNAPPROVED-SDR-METAL-POWER-IMAGES.zip)
([receipt](metal-power-bundle-receipt.json)). Historical `UNAPPROVED` filenames
denote no production promotion; subsequent exact-image owner test approvals
are recorded separately, not inferred from a bundle name.

## Conditions for replacing this draft

Incorporate only terminal, hashed supplementary captures and their failed
attempts. The completed S3 internal/PSRAM matrix and owned-peer wireless
cohorts are now closed, with their exact contexts in the linked reports.
BLE peer initialization failed; its symbolicated failure is retained rather
than presented as a capacity measurement. The colour, PCM and original SDR
experiments now have closed reports and distinct source/image/flash receipts.
The closed first metal dual-receiver stage now has
[native timing](METAL-RX-SMOKE-WIFI-FINDINGS.md),
[rate/envelope](METAL-RATE-WIFI-FINDINGS.md),
[framed reconstruction](METAL-FRAMED-WIFI-FINDINGS.md) and
[position-exchange](METAL-FRAMED-POSITION-COMPARISON.md) reports. The old generic
paced loop becomes a software bottleneck above requested 1 Mbaud. Its
11.37-million-store/s unpaced result and received envelope near 5.72 MHz do
not establish decoded payload throughput or a hardware ceiling. The stronger
receiver changes after physical position exchange, so no board-brand ranking
is justified. The separately approved fast/direct-RX follow-up is now closed;
its source-buffer/output checks, finite decoded payload and failed-to-decode
cases retain their own source/image/temperature context.
Total observer overhead, physical
contact-to-photon, analog audio/one-way latency, calibrated temperature and a
complete matched job RPC remain unmeasured. C3 remains without physical data.
Retain unsupported, failed and unmeasured cases as such. Promote an offload
candidate only after a complete job beats its measured local alternative and
meets the intended output/deadline/delivery requirements under combined load.
The PR remains **Draft/HOLD**; this is advisory evidence, not an upstream
dependency adoption, Diviner optimization or merge authorization.
