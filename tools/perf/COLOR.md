# Opt-in packed color intermediates

`cyd-profile-color` adds `GRIM_PERF_COLOR` to the profiling image. Ordinary
profiles do not compile this rendering stage. Physical use requires the exact
new image/board receipt and owner-authorized flashing; source tests and a build
are not measurements of its performance.

The experiment reuses the existing native fullscreen Visions renderer. It
rasterizes geometry in RGB565, stores genuine packed eight-row intermediates,
and expands/composites back into RGB565 strips for the existing display DMA.
Every LCD pixel still uses the existing **16-bit RGB565 SPI transfer**. The
already allocated logical canvas backs the scratch surfaces; its reservation
is unchanged. Lower active packed payload does not establish reduced heap use
or reduced display bandwidth for this implementation.

Supported controls, outside a running measurement:

```text
perf scene visions
perf color rgb111
perf vision 1 1 1 0
perf run color111_1 1000 3000
```

Wait quietly through warmup and the fixed window, then `perf export`. Formats
are `rgb111`, `rgb444`, `rgba5551` and `off`; `perf color info` reports cumulative
configuration-time diagnostics outside the window. The existing `perf vision`
controls select the actual effect, its one/two-effect setting, fullscreen and
ordinary Trails format. `perf color` can precede `perf vision`, so the
**post-window `color_run` is the authoritative applied-layout receipt**, rather
than the earlier setup receipt.

The current maximum is two simultaneous existing effects, with three supported
pairs: plasma/kaleido, gate/trails and ashfire/rain. Three pair choices do not
mean three simultaneous layers. This experiment is not a search for arbitrary
layer capacity. Root's `evidence/plan-color-quick.json` provides the bounded
three-second quick cases and native-format controls.

Each pixel's native bits are stored LSB first in an independent packed row:

| Format | Native pixel | Expansion/composition |
|---|---|---|
| RGB111 | R2 G1 B0, three packed bits | Top source component bits; expand to the eight RGB corner colors |
| RGB444 | R11..8 G7..4 B3..0, twelve packed bits | Existing Trails packing layout; component-bit replication to RGB565 |
| RGBA5551 | R15..11 G10..6 B5..1 A0 | Five color bits/channel, binary alpha; green expands from five to six bits |

For two effects, the base is packed and expanded first. The existing overlay
rasterizer then blends into that quantized RGB565 base. The compositor compares
the result with the expanded base: changed pixels are opaque, unchanged pixels
retain the base. RGBA5551 stores this binary coverage in A0; RGB111/444 use a
separate packed one-bit coverage plane. Opaque black is distinct from an absent
overlay. This **exercises binary compositing of already-blended changed
pixels**; it is not native one-bit-alpha geometry rasterization or fractional
alpha blending in the packed format.

For landscape width 320, each reused band has eight rows:

| Format | One packed base B | Packed base+overlay+coverage B | Total borrowed scratch with two RGB565 DMA strips B |
|---|---:|---:|---:|
| Native/off | 0 | 0 | 10,240 |
| RGB111 | 960 | 960 + 960 + 320 = 2,240 | 12,480 |
| RGB444 | 3,840 | 3,840 + 3,840 + 320 = 8,000 | 18,240 |
| RGBA5551 | 5,120 | 5,120 + 5,120 + 0 = 10,240 | 20,480 |

The last column describes the two-effect condition. One-effect scratch adds
only the base column to 10,240 bytes. Required scratch fits even the ordinary
32,400-byte RGB332 fallback canvas backing; a normal RGB565 canvas reserves
64,800 bytes. Actual dimensions, backing and fit are exported. Neither table
is a claim that a full-screen packed framebuffer or temporal history is stored.
Fullscreen Trails retain their existing bounded geometric path history. The
ordinary nonfullscreen RGB444/RGB565 feedback-history implementation is
unchanged.

`perf export` emits one `color_run` record before the final `end`, preserving the
19 existing core metrics. It includes actual format/effect layers, packed color
and alpha bits, external coverage bits, base/overlay/mask payloads, borrowed
scratch/backing bytes, source-raster and output-SPI bits, and successful/rejected
applied-frame and band counts. Counts are gated by renderer DMA completion in
the same steady `[begin,end)` window. Completion precedes the HAL's subsequent
LCD transaction close, so equality with the HAL present count is not required.
The off control counts native renderer completions and zero packed bands.

The host's optional `expected_color` plan assertion requires a matching record,
the requested format/layers, a valid fitting layout, unchanged backing, zero
rejections and positive applied-frame/band proof (or native-frame proof for
off). A setup acknowledgment alone is insufficient. Runtime configuration
changes are rejected during active profiling windows. Per-frame code performs
no serial output or new heap allocation. Existing frame-present timings include
the raster, conversion, compositing and SPI costs together; separate stage
timings are not exported.

This new profile also reduces only its outside-window link JSON formatting
buffers to **4,096 bytes for info and 3,072 for summary**. Conservative bounds
are 2,683 and 2,383 bytes including termination. ACK remains 384 and kernel
output remains 6,144; other profiles retain their original 6,144-byte link
buffers. `check_output_bounds.py` verifies the source guards and bounds. This
addresses observed diagnostic allocation pressure without modifying the
measured wireless workload, and new source/image identity keeps the cohort
separate from earlier captures.

The portable oracle exercises all 65,536 RGB565 inputs, storage/row boundaries,
coverage and alpha semantics. The separate pipeline test compiles the actual
`perf_color.cpp` against a small endian-compatible graphics/clock seam, checking
all formats, opaque black, absent coverage, odd row widths, guarded borrowed
storage, window boundaries and rejection counts under ASAN/UBSAN. Its clock
values are test inputs, not physical latency measurements.

Example pipeline test with the installed WSL GNU compiler:

```powershell
wsl -e g++ -std=c++17 -Wall -Wextra -Werror '-fsanitize=address,undefined' `
  -fno-omit-frame-pointer -DGRIM_PERF -DGRIM_PERF_COLOR `
  -I/mnt/c/cyd-diviner/perf-characterisation/tools/perf/fakes/color `
  /mnt/c/cyd-diviner/perf-characterisation/tools/perf/test_color_pipeline.cpp `
  /mnt/c/cyd-diviner/perf-characterisation/src/perf_color.cpp `
  -o /tmp/cyd_perf_color_pipeline
wsl -e /tmp/cyd_perf_color_pipeline
python tools/perf/check_output_bounds.py
```
