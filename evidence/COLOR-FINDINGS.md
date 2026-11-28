# Fullscreen packed-colour findings

The experimental packed intermediates substantially reduced frame rate in this
implementation. Native Gate completed **25.00 FPS** with one effect and
**22.33 FPS** with Gate+Trails. RGB111 completed **7.89/3.67 FPS**; RGB444 and
RGBA5551 each completed **7.00/3.00 FPS**. These are measurements of the added
generic packing/conversion/compositing path around the existing renderer. They
do not establish hardware limits for native low-depth rendering or an optimized
renderer designed around packed storage.

All **36/36 windows completed**: twelve settings, three repetitions each,
one-second warmup and three-second steady window, fixed seed23063. The board was
the owner's classic ESP32 CYD at240 MHz, with40 MHz LCD SPI. This used the approved
`cyd-profile-audio` image, source
`ad9cbfd670e456de5e08fca19810fb93f7f1db47`, firmware SHA256
`407eb96f8fe2b74ed1cf45f4a163558eacde0ff287a4b878a9ed1faadfaccc93`.

| Fullscreen Gate setting | Completed presents / 9 target seconds | Pooled FPS | Per-repetition FPS | One/two-effect slowdown versus matching native |
| --- | ---: | ---: | --- | ---: |
| Native, one effect |225|25.00|25.00,25.00,25.00|1.00×|
| RGB111, one effect |71|7.89|7.67,8.00,8.00|3.17×|
| RGB444, one effect |63|7.00|7.00,7.00,7.00|3.57×|
| RGBA5551, one effect |63|7.00|7.00,7.00,7.00|3.57×|
| Native, Gate+Trails |201|22.33|22.33,22.33,22.33|1.00×|
| RGB111, Gate+Trails |33|3.67|3.67,3.67,3.67|6.09×|
| RGB444, Gate+Trails |27|3.00|3.00,3.00,3.00|7.44×|
| RGBA5551, Gate+Trails |27|3.00|3.00,3.00,3.00|7.44×|

The existing maximum of **two effect paths** was also exercised as
Plasma+Kaleido and Ashfire+Rain. Both pairs completed3.67 FPS with RGB111 and
3.00 FPS with RGB444, across all three repetitions. These are the current
Visions base/overlay combinations, not a general compositor with an arbitrary
number of resident layers. Fullscreen trails use bounded geometric paths;
`color_run` reported no temporal pixel history.

The actual post-window `color_run` records are authoritative for format,
base/overlay and layer count. Configuration receipts can precede the final
`perf vision` command and describe the previous layout. Every packed case had
positive successfully applied frame/band counts and **zero rejected frames**;
native controls had positive native-frame counts and zero packed bands. The
records assign observations by renderer DMA completion inside the steady
window. Those counts should not be required to equal the later HAL presentation
counter at a boundary.

| Actual intermediate layout | Packed base B | Packed overlay B | External coverage B | Packed payload B | Total borrowed scratch B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Native control |0|0|0|0|10,240|
| RGB111, one effect |960|0|0|960|11,200|
| RGB111, two effects |960|960|320|2,240|12,480|
| RGB444, one effect |3,840|0|0|3,840|14,080|
| RGB444, two effects |3,840|3,840|320|8,000|18,240|
| RGBA5551, one effect |5,120|0|0|5,120|15,360|
| RGBA5551, two effects |5,120|5,120|0|10,240|20,480|

These are reused **eight-row** surfaces at320-pixel output width. Total scratch
includes10,240 bytes for two native RGB565 transfer strips. The existing canvas
backing remained **64,800 bytes in every case**; the experiment did not shrink
that reservation or export a full resident packed framebuffer. Source raster
and display wire remained **RGB565/16 bits per pixel**. Consequently these
measurements establish neither a lower SPI byte count nor a realized backing
allocation saving.

RGB111 uses three colour bits and RGB444 twelve. Two-effect cases use an
additional external one-bit coverage mask. RGBA5551 uses fifteen colour bits
plus one alpha bit in its sixteen-bit packed word, with no external mask.
Coverage is inferred from changed pixels after the existing RGB565 overlay
blend; this is not a native one-bit-alpha rasterizer. Quantization correctness
has separate portable tests; this campaign is not a calibrated display-colour
measurement.

The slow packed cases kept sampled core1 utilization around98.5–99.5%, compared
with95.0%/97.3% for the matching native Gate controls. Sampled minimum free heap
was88,508 bytes for Gate/Plasma cases and81,408 bytes for Ashfire; the largest
free block stayed47,092 bytes. These finite sampled minima describe this
profiling image, not ordinary firmware headroom or proof of leak-free long
sessions. Native Gate present maxima were38.01/43.53 ms; the two-effect packed
present maxima reached282.33 ms for RGB111,331.02 ms for RGB444 and328.31 ms for
RGBA5551. The few-dozen visual observations do not support an exact visual p99.

Across72 retained in-window samples, the classic sensor reported raw codes
**136–138**, using `RTC_library_raw_undocumented_uncalibrated`. They are recorded
indications, not calibrated die temperature. This short colour matrix is
separate from the extended CPU stress test. I2S event records had zero
TX_Q_OVF/DMA_ERROR indications, and the inspected scheduling counters had zero
late/write/short events; neither establishes analog audio continuity. No
physical contact-to-photon or analog fixture ran in this matrix.

For VIBEWARE, use native RGB565 as the measured baseline for these current
fullscreen paths. Do not budget the generic experimental packed conversion
path as a frame-rate improvement or memory-reservation reduction. A renderer
that works directly in packed data, a different blend strategy, or hardware
with other display paths would need its own matched physical benchmark. No
optimization was undertaken here.

The [published report](campaign-color/REPORT.md),
[scene summary](campaign-color/summary-scenes.csv) and
[publication manifest](campaign-color/manifest.json) retain detailed counts and
scope. The immutable [raw archive](PHYSICAL-CAPTURES-COLOR.zip) has SHA256
`0e1eec6eea902cba428cf37f63e50a2a5f0d505d827cafaf7a5690731a593cee`;
member `physical/color-quick-01/results.json` has SHA256
`f7910e7a3877c33ceee35e9d71d32665e4b5009f5e0a00fc6824e54b97a5614e`.
