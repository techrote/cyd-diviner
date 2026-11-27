# Portable kernel protocol

These scalar C++17 reference workloads compile without Diviner, LovyanGFX, an
RTOS, a radio stack or a DSP library. The same implementation is used in the
CYD profiling image and standalone `kernel-esp32`, `kernel-s3`, `kernel-c3`
PlatformIO environments. Standalone images are compile evidence until tested
on an explicitly identified board. They do not configure LCD or I2S pins.

Use a quiet UART window (stop PCM first):

```text
perf kernel list
perf kernel osc-f32 343709 256 0
perf kernel feedback 343709 256 1
```

Arguments are name, uint32 seed, timed iteration count (1..256) and size flag
(`0` representative, `1` larger). All storage allocation, initialization,
twiddle/LUT generation, three warmup iterations and final output inspection
are outside timed samples. State is reset after warmup. Each timed sample
includes RTOS preemption; each iteration is followed by `delay(1)` outside the
sample. Total elapsed includes those yields. One final `PERF` JSON line holds
exact nearest-rank median/p95/p99, minimum, maximum, sum, units, checksum,
output range/sum, CPU clock and memory receipts. There is no per-sample UART
output. `samples_us` preserves the original order of every timed sample.
A `kernel_error` record is a failed workload, never a zero timing.

| Name | Representative / larger case | Output and numeric meaning |
|---|---|---|
| `osc-f32`, `osc-q15` | 128 / 512 samples at 24 kHz, 440 Hz | Same 1024-entry interpolated sine LUT, uint32 phase; float32 vs Q15 table/Q22 fraction; Q15 output |
| `filter-f32`, `filter-q15` | 128 / 512 deterministic input samples | One-pole lowpass, coefficient float32 0.13 vs Q15 4260/32768, Q30 fixed state; Q15 output |
| `modal-f32`, `modal-q15` | 8 x 128 / 32 x 512 voice samples | Damped second-order resonators, 110+37v Hz, per-block 0.004 impulse; float32 vs Q30 state and coefficients; Q15 output |
| `fft-f32` | 256 / 1024 complex points | Scalar unnormalized radix-2 DIT forward FFT; bit reversal, input refresh and butterflies timed; float32 twiddles prepared beforehand |
| `cellular` | 64x48 / 128x96 cells | Toroidal Conway Life B3/S23, two uint8 grids |
| `particles` | 128 / 512 particles | Q16.16 position/velocity, gravity, walls and damped floor; state update only |
| `feedback` | 80x60 / 160x120 indexed pixels | Wrapped nearest-neighbor integer warp, decrement decay and one excitation pixel per step; two uint8 grids |
| `stripe-rgb565` | 320x4 / 320x16 pixels | RGB565 strip generation; actual SPI transfer is a separate HAL benchmark |

The optional common heap workspace is 40,016 bytes, reused across kernels.
`working_bytes` is the active context including capacity reserved for the
larger case; it is not a heap high-water measurement. `heap_min` samples free
heap during the runner; asynchronous allocations between samples can be
missed. Work units are samples, voice samples, points, cells, particles or
pixels as stated, not estimated instruction counts. Keep these distinctions
when comparing compute per returned byte for offload decisions.

FNV-1a checksums describe the output after the specified iteration count.
Seeds use xorshift32, substituting `0x6d2b79f5` for zero. Integer outputs use
little-endian checksums on the supported ESP targets. Floating FFT checksums
can differ with compiler, floating contraction, math library or architecture;
use numeric validation rather than demanding bit equality. The float and
fixed DSP variants intentionally quantize the same output to Q15; their
coefficient/table precision differs and must be reported with timing.

Host validation compiles with warnings as errors, repeats all 22 cases, checks
deterministic output and finite/range metadata, compares DSP output error over
one fresh block, and validates FFT output against an independent direct
double-precision DFT plus Parseval energy check. ASan/UBSan adds a meaningful
arithmetic and bounds check. On a Windows machine with WSL:

```powershell
wsl.exe -e sh -lc 'cd /mnt/c/cyd-diviner/perf-characterisation && python3 tools/perf/validate_kernels.py --git git.exe --sanitize'
```

On a host with a suitable compiler on PATH:

```text
python tools/perf/validate_kernels.py --cxx g++ --sanitize
```

Receipts, compiler output and three raw/structured repetitions are written to
`evidence/host-kernels`. These are explicitly **host replay**, with no claim
about ESP32, ESP32-S3 or ESP32-C3 physical performance. Numerical oracle work
is never included in a kernel benchmark sample.
