# Windows hardware capture

This package controls only the opt-in profiling firmware. It never flashes,
erases flash, changes saved credentials, discovers peers, or tests other devices.
Board identification, full-flash backup, recovery and flashing are separate owner
actions. Keep backup images private: they may contain credentials and NVS data.

Install Python 3.12 or newer and the pinned serial dependency:

```powershell
python -m pip install -r tools/perf/requirements.txt
python -m unittest discover -s tools/perf -p test_capture.py -v
```

Use the existing project virtual environment when available. The PowerShell
wrapper accepts `-Python C:/path/to/python.exe`; remaining arguments are passed
unchanged to the Python CLI. Output directories must be new, preventing accidental
replacement of earlier evidence. Close VLC and other serial-port consumers first.
DTR and RTS are disabled before opening the serial port.

The separate read-only backup helper requires its own pinned esptool API:

```powershell
python -m pip install -r tools/perf/backup-requirements.txt
python -m unittest discover -s tools/perf -p test_board_backup.py -v
```

`board_backup.py` uses esptool 4.14.0 in one persistent serial session, starts at
115200 baud, and retries the requested `--baud` if an existing RAM stub was left
at that speed. It verifies the requested silicon identity, reads named non-key
security flags, and checks the JEDEC capacity before reading. The optional full
backup is hashed locally and independently checked with the target's flash MD5.
Use a new output directory outside the repository; neither an earlier backup nor
its receipt can be replaced. Each API operation records arguments, output,
duration and any failure. Classic ESP32's unsupported `get_security_info` CLI
command and separate espefuse subprocesses are not used. The capture dependency
list remains serial-only.

On completion or a recoverable failure, the helper requests 115200 baud and
leaves the RAM stub running without soft/hard reset or application start. The
receipt reports whether that state was reached; failed sync or cleanup must not
be treated as a confirmed recovery state. RAM stub upload and volatile flash
geometry setup do not write flash, NVS or eFuses. Private backups and receipts
stay on the owner's machine.

After the owner has flashed the positively identified profiling image, inspect its
reported identity without starting a benchmark:

```powershell
python tools/perf/capture.py identify --port COM21 --baud 533333 --output evidence/identify-01
```

Prepare a board JSON file with at least `board` and all independently observed
hardware details, for example:

```json
{
  "board": "ESP32-2432S028R (owner-confirmed marking)",
  "port": "COM21",
  "usb_vid_pid": "1A86:7523",
  "chip_model": "replace with actual esptool identity",
  "chip_revision": "replace with actual identity",
  "flash_bytes": null,
  "psram_bytes": null,
  "owner_flash_authorization": "replace with authorization receipt",
  "backup_sha256": "replace with private backup checksum",
  "ambient_temperature_c": null,
  "ambient_temperature_method": "NOT MEASURED"
}
```

Example values are not board identification. Confirm the flash capacity and board
marking before using them. The capture also records actual target chip, revision,
clocks, heap, PSRAM, SPI, audio scheduling, serial profile and sensor method.

```powershell
$sourceSha = git rev-parse HEAD
python tools/perf/capture.py capture --port COM21 --baud 533333 `
  --output evidence/full-01 --plan tools/perf/plan-full.json `
  --board evidence/board.json --source-sha $sourceSha --tree-state clean `
  --firmware .pio/build/cyd-profile/firmware.bin --profile cyd-profile
```

Use the actual build profile and binary path. Supply the exact 64-digit source
tree digest as `--tree-state` when available from the build receipt: capture then
requires equality with the target's compiled source digest. A 40-digit source
commit and matching target profile/serial speed are always required. The binary
SHA256 is computed locally; the target does not attest the flashed binary digest.
Preserve the flash receipt and build receipt to close that evidence chain.

`plan-full.json` has three steady repetitions of ten seconds after two seconds of
warmup for ordinary scenes. It covers menu backdrops, six individual Visions,
three layered combinations, portrait/landscape, fullscreen/UI visible, trail
formats, local DAC audio, local DAC plus USB PCM, separate drone/seq/beat callbacks
with fullscreen layered graphics, scripted touch, controller initialization costs,
histogram accumulation on/off, SPI strips, eleven portable kernels at realistic
and larger sizes, sustained stress and a post-stress scene. Kernels use 256 timing
samples after three warmups, repeated three times. The CPU stress is one measured
365-second window, allowing at least 360 seconds of retained sample coverage.
Scene windows total roughly 49 minutes; kernels, SPI and serial
exports add time. `plan-smoke.json` is a roughly nine-minute subset including the
stress. `plan-stress.json` runs the six-minute-five-second stress alone. Regenerate the plans
with `python tools/perf/make_plan.py`; review the JSON before running it.

Wireless traffic, physical contact-to-photon latency, analog audio acquisition,
USB-only DAC-disable mode and S3/C3 physical execution are explicit `NOT RUN`
entries when their required peers, controls, boards or fixtures are absent.
Controller initialization measures resource and local-workload overhead only;
it does not measure goodput, RTT, loss or coexistence under traffic. Add a separately
reviewed plan only after the relevant boards/peers are owner authorized.

Each scene applies acknowledged setup commands, starts a target-timed run, leaves
its warmup/measurement window quiet, then requests retained telemetry. Firmware
does not print telemetry during active USB PCM. PCM capture writes the binary
stream and validates CYD1 CRC and sequence independently, pings the session, stops
it, drains for at least 400 ms and until 200 ms of quiet (bounded at three seconds),
then exports target telemetry. A zero-frame PCM window fails. The target never mixes
diagnostic text into a live PCM stream. Scripted touch can be replayed inside
windows through explicit timed actions; these commands measure firmware input
processing, not physical stimulus latency.

The stress worker runs on both cores while preserving watchdog service. Capture
starts it immediately after the run acknowledgement, allows an extra second at
the end, and stops it after export so iteration counts are retained. The report
requires a completed >=360-second target window, >=360 seconds of retained sample
coverage, successful CPU hooks, and >=95% weighted tick-sampled busy time on both
cores before setting `near_100_percent_confirmed`. Inspect the measured busy
distribution and raw tick counts as well. Temperature is recorded once per second
using the firmware-reported method. The primary image's `ESP32_ROM` method tag is
legacy wording: its linked implementation is in SDK `librtc.a` / `rtc_analog.o`,
not established as mask ROM. This undocumented API gives uncalibrated sensor
indications with unsupported temperature accuracy; do not present them as accurate
die or ambient temperature.

Every output directory contains:

* `serial-rx.bin`: every received byte, including binary PCM and boot text.
* `serial-events.jsonl`: host monotonic/UTC times, commands and exact RX byte
  offsets/counts; these correlate host transport without implying target timing.
* `serial-text.log`: bytes received during text windows.
* `target.jsonl`: parsed target records before interpretation.
* `rNNN_R-pcm.bin`: the independently replayable binary receive window for each
  PCM workload, also preserved within `serial-rx.bin` at recorded byte offsets.
* `results.json`: versioned identity, settings, raw target summaries, interpreted
  missing values, PCM integrity, explicit limitations and completion status.
* `metrics.csv` and `samples.csv`: long-form records with source/profile identity
  and settings. Histogram percentiles retain `upper` in their names.
* `artifact-hashes.json`: SHA256 of the other capture files.

Malformed telemetry, target errors, unknown identity, incomplete windows, missing
metrics, histogram count mismatches, retained sample loss and missing end markers
fail the capture while retaining raw and partial evidence. Zero-observation
histograms have null percentiles in results; fewer than 100 observations suppress
the interpreted p99. Exact raw firmware records remain in `target.jsonl`.
`perf enable 0` disables histogram accumulation but keeps tick hooks, counters and
sampling, so its comparison measures accumulation overhead rather than the entire
profiling build overhead. Compare separate ordinary/diagnostic build receipts and
external acquisition when measuring total instrumentation overhead.

Offline CSV regeneration and a compact receipt check:

```powershell
python tools/perf/capture.py analyze evidence/full-01/results.json
python tools/perf/capture.py decode-pcm evidence/full-01/rNNN_R-pcm.bin
python tools/perf/summarize.py evidence/full-01/results.json
python tools/perf/report.py evidence/full-01/results.json --label full-01
```

The summary pools actual kernel/SPI samples and frame/audio histogram bins across
matching workload repetitions. It writes JSON and separate scene, metric, kernel,
SPI and missing-workload CSVs. Kernel compute time per output byte ranks potential
offload candidates only. The input/state transfer size, remote computation and
actual wireless goodput/RTT must also be measured before claiming a benefit. The
summary records the corresponding break-even formula without supplying invented
link performance.

`report.py` produces editable Markdown beside the results, or at `--output PATH`.
It links the capture artifacts, includes exact observation counts/repetitions,
scene/heap/CPU summaries, scheduling tails, PCM integrity, stress sensor span,
actual scene-allocation receipts and explicit missing measurements. It labels
preflight captures and incomplete plans, and embeds existing `figures/*.png` or
`plots/*.png` without generating performance data. Summary pooling splits any
differing requested settings, duration, warmup, histogram mode or actual scene
allocation/settings receipts into separate scope IDs. Host receive timestamps
do not split otherwise matching repetitions.

The human report tightens histogram percentile bounds to the lesser of the raw
bucket upper bound and the exact observed maximum. This remains a bound, not an
exact percentile; raw target records and summary CSVs keep their bucket values.
Its touch table reports measured XPT polling-call count per target second and
control-dispatch invocation durations, including no-contact samples. These do
not establish physical touch latency or count distinct contacts.

Read back the physical display outside all measurement and PCM windows:

```powershell
python tools/perf/capture.py shot --port COM21 --baud 533333 --output evidence/shot-01
```

This saves controller RGB565 bytes, a portable PPM preview, dimensions and checksum.
Readback time is not included in the benchmark. A missing DMA underrun detector is
reported as unknown; I2S write duration, block count, inferred late periods and PCM
loss each remain distinct evidence.
