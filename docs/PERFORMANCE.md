# CYD performance characterisation

Mission: **CYD-DIVINER-PERF-CHARACTERISATION-001**. Consumer:
[VIBEWARE #8](https://github.com/techrote/cyd-SYNTH-VIBEWARE/issues/8).
The [consumer specification](https://github.com/techrote/cyd-SYNTH-VIBEWARE/blob/main/docs/PERFORMANCE.md)
defines the requested physical campaign. Accepted Diviner baseline was freshly
resolved as `7b1b37eec6ac126f334a0b008d91135071ea60fa` on 27 November 2026.

## Scope and evidence status

`cyd-profile` is a dedicated opt-in image. The original firmware environments,
UI, saved settings and application behavior retain their ordinary paths. The
profiling image adds explicit scene controls and RAM-only test settings. It is
**unapproved evidence**, not a replacement production release. No automatic
flashing, erasing, credential changes, radio discovery or peer traffic occurs.

Physical performance results must come from an identified, authorized board.
`evidence/mission-status.json` records the current campaign status; missing
measurements remain **NOT RUN**. Host correctness tests and target compilation
are different evidence classes. Neither establishes CYD throughput, frame rate,
temperature rise, audio reliability or wireless performance.

The Windows host sees COM21 as CH340, VID:PID `1A86:7523`. Read-only connection
attempts observed classic ESP32 ROM output and boot mode `0x13` (application
boot), rather than download mode. Longer reset timing and a Windows combined
DTR/RTS change did not enter download mode. See `evidence/hardware-access/`.
The ROM boot log reports DIO and clock divisor 2; this does not identify PCB
revision, flash capacity, PSRAM or the configured running application.

## Board identification, backup and recovery

The case can prevent access to BOOT/RESET. **Holding BOOT while reconnecting
USB avoids using RESET.** Do not open a case or change wiring as an automatic
step. A failed automatic reset is an access blocker for both flash readback
and flashing; omitting a backup does not solve download-mode access.

The read-only backup helper runs esptool with `no_reset`, records inventory and
security state, requires an independently checked flash capacity, reads the
whole chip, hashes the image and verifies it against the board:

```powershell
python tools/perf/board_backup.py --port COM21 --chip esp32 `
  --out C:/cyd-diviner/private-backups/cyd-01
# After inventory confirms the capacity, choose a NEW output directory:
python tools/perf/board_backup.py --port COM21 --chip esp32 `
  --out C:/cyd-diviner/private-backups/cyd-01-full --backup-bytes 0x400000
```

`0x400000` is an example requiring confirmation, not a detected capacity.
Keep full-flash images outside Git: they include NVS, potentially credentials.
The backup preserves settings and exact recovery; a normal Diviner rebuild
restores application code but is not a substitute for those saved bytes.

Before a separately owner-authorized flash, verify chip, security state,
original partition table, application offset/capacity and image digest. Prefer
an app-only write if the existing partition is compatible. Do not automatically
replace the bootloader, partition table or NVS. Record the actual esptool command,
output, firmware SHA256 and authorization. Normal esptool writes verify their
written data; retain that receipt. Recovery is a separately authorized write of
the verified private full-flash backup at offset zero. The capture package has
no flashing command or recovery automation.

## Reproduction

Install PlatformIO Core 6.2.0 and the Python dependency in
`tools/perf/requirements.txt`. Environments pin Espressif platform 7.1.3 and
existing display/touch library versions. The build receipts retain resolved
framework/compiler/library versions, image/ELF/map hashes and sizes.

```powershell
pio run -e cyd -e cyd-diagnostics -e cyd-pcm
pio run -e cyd-profile -e kernel-esp32 -e kernel-s3 -e kernel-c3
python tools/perf/build_receipt.py --out evidence/build-receipt.json `
  cyd cyd-diagnostics cyd-pcm cyd-profile kernel-esp32 kernel-s3 kernel-c3
python -m unittest discover -s tools/perf -p test_capture.py -v
```

`tools/perf/identity.py` embeds the source commit, dirty flag, exact digest of
`platformio.ini` and `src/`, and build profile. A capture checks the source SHA,
profile, baud and (when supplied) source digest against the running target.
The supplied firmware file is hashed independently; the target does not attest
that binary hash, so preserve the flash verification receipt too.

See [the Windows harness instructions](../tools/perf/README.md) for the board
metadata file and exact capture commands. Full plans, seeds, warmups,
repetitions, optional action times and workload settings are reviewable JSON.
Default scene repetitions use ten steady seconds after two warmup seconds.
Kernel/SPI commands have their own bounded warmup/sample receipts.

Native reproduction requires SDL2 development libraries and a POSIX compiler:

```sh
PLATFORMIO_BUILD_DIR=/tmp/cyd-native-build pio run -e native
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  GRIM_SCRIPT='100:key:x;600:key:1;1400:esc;1600:key:5;2600:quit' \
  /tmp/cyd-native-build/native/program
python3 tools/perf/validate_kernels.py --sanitize
```

Use different build directories when Windows and WSL run PlatformIO against
the same checkout: platform-specific project checksums can otherwise invalidate
each other's build output. Host replay is a functionality/correctness check.

## What is measured

| Surface | Observation and limits |
|---|---|
| Frames | Actual completed presentation count divided by the exact steady target window; completed frame intervals, input/update work, logical canvas raster and present duration. A frame is not a loop iteration. |
| Direct/fullscreen rendering | CPU raster and SPI commands/transfers interleave. Report combined present time; no invented CPU/SPI split. Buffered logical raster has its own timing. |
| SPI | Precomputed eight-row RGB565 stripe, complete physical frames, three warmups and up to 128 exact timings. Includes LCD command overhead and final DMA wait; excludes stripe generation. |
| Touch | XPT2046 polling and control dispatch duration, plus scripted taps/releases. This is processing time, not physical contact-to-photon latency. |
| Audio | Separate mutex wait, main callback, overlay, mix and blocking `i2s_write` durations; block-start intervals, completed writes, errors and short writes. |
| Audio deadline flags | Periods greater than 6,000 us are explicitly inferred late-period flags. They do not detect DMA starvation. Hardware underruns and analog output gaps remain unknown without a detector/acquisition fixture. |
| USB PCM | Target queue occupancy/drop counters and write timing; host CRC/sequence validation, raw bytes and arrival timing. Host buffering is not target scheduling. Stream-edge loss is unknown. |
| CPU | Per-core 1 kHz tick samples classify interrupted task versus IDLE, with coherent snapshots and hook-validity flags. Interrupt execution is not separately attributed; sampling/aliasing limits apply. Invalid hooks produce null utilization. |
| Memory | Free heap, boot-global minimum, largest allocatable block and audio/main stack high-water bytes. The minimum is not reset at run start. Kernel workspace and active context bytes are recorded separately. |
| Temperature | Raw classic ESP32 ROM sensor value and `(raw-32)/1.8`, explicitly uncalibrated. Not an external/ambient measurement or calibrated die-temperature claim. |
| Radio | Isolated Wi-Fi station, BLE controller or ESP-NOW initialization without association, scanning, peers or traffic. Measures local resource/workload costs only. |

The fixed 480-entry sample ring retains 1 Hz CPU/memory/temperature readings;
overflow is counted and fails capture. Short kernel/SPI windows record sensor
snapshots before and after. The extended stress plan loads both cores for more
than 360 seconds, gives IDLE watchdog service, and assesses actual retained CPU
coverage/utilization before claiming near-100% load. The JSON plan and report
state exact requested and observed durations. No stress/temperature conclusion
is valid until those records exist.

`frame_missed_periods_inferred` counts complete extra 33 ms periods between
completed presentations; it is not a hardware scanout/drop detector or a count
of every frame that exceeds a 33 ms deadline. Combined Visions/audio cases run
the secondary audio callback; the secondary app's UI/frame function is not run.

Timing histograms have 32 ceil-log2 bins. Count, sum, minimum and maximum are
exact; median/p95/p99 are conservative **bin upper bounds**, not exact values.
Zero-observation percentiles become null; interpreted p99 is suppressed below
100 observations. Kernel/SPI arrays retain actual chronological timings and use
nearest-rank percentiles. Raw records are retained before interpretation.

`perf enable 0` disables histogram accumulation while keeping timestamps,
sampling, tick hooks and minimal counts. Its A/B comparison isolates histogram
insertion overhead. Total profiling overhead needs an ordinary/diagnostic build
comparison with equivalent workload and external/minimal counting; the switch
does not establish that total overhead.

## Workload and offload interpretation

[Portable kernels](../tools/perf/KERNELS.md) describe all eleven algorithms,
numeric precision, dimensions, work units and output checks. Three host
repetitions and independent numeric oracles establish reproducibility on that
host. Classic ESP32/S3/C3 builds use the same scalar kernels; compile success is
not a cross-hardware speed comparison, and no PSRAM/FPU capability is assumed.

The configured audio block is 128 samples at 24 kHz: 5.333333 ms. This is a
configuration-derived cadence, **not a measured scheduling margin**. Measure
tail callback/lock/mix/enqueue times and actual cadence under combined loads
before proposing voice, frame or resource budgets. An average cannot establish
a deadline guarantee.

Candidates for later heterogeneous experiments are FFT-to-compact spectral
features, batched modal/control computation, cellular/particle state updates,
and feedback/warp parameter generation. They are candidates, not demonstrated
offload wins. Prefer tests with compact returned features or parameters; record
both input and output bytes, result rate, precision, serialization cost and
local degradation during transport. Full-frame transfer must include its actual
payload and contention costs.

For each candidate, use measured local and remote compute times and measured
coexistence link tails. A conservative break-even condition is:

```text
local_compute > link_setup_and_RTT_tail + input_bytes / input_goodput
              + remote_compute + output_bytes / output_goodput
              + serialization_and_queue_cost
```

If a request is overlapped with transfer, measure that pipeline directly rather
than subtracting costs speculatively. Goodput/RTT/jitter/loss, an owned peer and
remote target results are currently required before a numeric minimum useful
compute-to-transfer ratio or wireless offload recommendation can be established.
Keep missing bounds explicit when reporting to VIBEWARE #8/#9.

No merge or production promotion is part of this mission. The PR remains
**Draft/HOLD** pending owner review and physical evidence.
