# Reproduce the convergence candidate

**Draft/HOLD.** Build and test commands below do not flash hardware. Check out
the exact commit named by the pull request's successful CI artifacts, not an
unrelated branch tip or GitHub's synthetic merge commit. Keep the tree clean.

## Dependencies and matrix

Use Python 3.12, PlatformIO **6.2.0**, a C++17 host compiler and SDL2 development
headers for simulators. The workflow uses Ubuntu 24.04 for firmware/native and
Windows for the existing VLC plugin. Pillow **13.0.0** converts preview images.
`platformio.ini` pins all direct firmware libraries and platform releases.

| Environment | Platform | Important dependencies / layout |
| --- | --- | --- |
| `cardputer` | pioarduino `55.03.39`, Arduino ESP32 3.3.9 | M5Cardputer 1.1.1, M5Unified 0.2.24, M5GFX 0.2.31, IRremote 4.4.1; ESP32-S3, 8 MB, `default_8MB.csv` |
| `cyd` | PlatformIO Espressif32 7.1.3 | Arduino ESP32 2.0.17; LovyanGFX 1.2.32, XPT2046 v1.4; classic ESP32, 4 MB, `huge_app.csv` |
| `cyd-diagnostics` | Same CYD base | Serial diagnostics, timing histograms and existing screenshot/input commands |
| `cyd-pcm` | Same CYD base | CYD1 serial PCM, 24 kHz mono, 533333 baud |
| `native` | Native 1.2.1 | LovyanGFX 1.2.32; keyboard/Cardputer-style application, SDL2 |
| `native-cyd` | Same native base | Actual CYD controller, palette renderer, touch/rotation and opt-in file persistence |

The build's `manifest.json` records the actually resolved package and library
versions, chip, flash capacity, source SHA/tree/content snapshot, image offsets,
partition entries, ELF/map hashes and firmware checksums. Treat that manifest
as authoritative for its artifact. Versionless VCS libraries additionally record
PlatformIO's resolved version and, when available, the full installed Git revision;
the XPT2046 v1.4 tag resolved to `d57f64c8b5f2bc5b8d10d121550806eeff7b06d9` in the
independent audit. Header/source counts are not component
flash-size measurements.

The pinned Cardputer SDK selects NimBLE (IDF 5.5.4); CYD uses the Arduino 2
Bluedroid stack. The shared Arduino BLE interface remains active on both, while
headers and residual cleanup are selected by host capability. SDK initialization,
low-memory recovery and radio/audio coexistence need physical qualification.

```sh
python3 -m venv ../diviner-venv
. ../diviner-venv/bin/activate
python -m pip install platformio==6.2.0 pillow==13.0.0
# Debian/Ubuntu native prerequisites: install libsdl2-dev and g++ using the OS package manager.
pio run -e cardputer -e cyd -e cyd-diagnostics -e cyd-pcm -e native -e native-cyd
python tools/package_firmware.py cardputer
python tools/package_firmware.py cyd
python tools/package_firmware.py cyd-diagnostics
python tools/package_firmware.py cyd-pcm
qualified_sha=$(git rev-parse HEAD)
for target in cardputer cyd cyd-diagnostics cyd-pcm; do
  python tools/audit_firmware.py delivery/$target --source "$qualified_sha" \
    --environment "$target" --repository . --output delivery/$target/audit.json
done
```

Put the virtual environment outside the checkout, or explicitly ignore it
locally, before a qualified build. Untracked source is deliberately considered
dirty. Avoid editing any source while compiling: the pre-build source snapshot
must still match at post-build and packaging. Packaging refuses stale identities,
changed bytes, invalid partitions, overlapping/out-of-flash images and nonempty
output directories. Use a new `--output` directory for a repeat. `--allow-dirty`
is for explicitly labeled development experiments, never qualified delivery.

`firmware.bin` is app-only. The `<environment>-full.bin` artifact starts at zero
and combines the actual bootloader, partition table, configured bootstrap and
application at their actual offsets. No hard-coded Launcher address is assumed.
The independent auditor checks all original bundle checksums, actual ESP image
segment checksums/appended SHA, chip/flash header, partition MD5 and bounds, full
image offsets/erased gaps, embedded source identity and a separately reconstructed
Git content snapshot. It also reports ELF static DRAM, excluding dynamic memory.
It shares no implementation imports with the packager. Its own `audit.json` is
outside the original SHA256SUMS set; supply the same `--output` when rerunning to
exclude only that receipt from the bundle-file inventory. Physical execution and
secure-boot signature qualification are outside this unsigned-image audit.

Read [owner evaluation](OWNER-EVALUATION.md) before any separately authorized
installation. No hardware has been flashed by the mission.

## Portable qualification

```sh
python tools/qualify.py --output delivery/portable-none
python tools/qualify.py --sanitizer address --output delivery/portable-address
python tools/qualify.py --sanitizer thread --output delivery/portable-thread
```

The master retains commands, compiler/runtime logs, exact source identity and
PASS/FAIL/BLOCKED results; timeouts fail. It runs the actual new instrument
renderers with test HAL seams, numerical DSP, same-length text/score publication,
bounded queues, CV1 membership/timing, actual callback lifetime, five allocation
failures, concurrent UI/render work and legacy exact-output comparison. Normal
runs retain seventeen WAV specimens. The packaging suite uses 28 synthetic binary
fixtures; those are not firmware artifacts. Five installed-dependency fixtures
check declared versus resolved versions, VCS metadata locations and full revisions.
The firmware jobs additionally audit their real freshly built bytes.

Normal qualification also runs eight evidence-classification guard cases and an
independent bounded SPSC interleaving model. The Cardputer job retains a separate
atomic-probe artifact containing actual compiler configuration and disassembly
for the selected runtime, snapshot exchange, state allocator and callback gate.
This establishes code-path evidence; embedded cycle timing still needs hardware.

ASan includes UBSan; TSan is a separate run. Default ASan enables leak checking.
The development workspace's ptrace restriction blocked LeakSanitizer; setting
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` allowed memory/undefined-behavior
checks but does not establish leak freedom. The first complete CI passed with leak checking enabled; every final CI job keeps
it enabled and reports its own result.
The TSan CI job reduces that runner's `vm.mmap_rnd_bits` to 28 for GCC runtime
compatibility and uses a non-PIE test executable. A sanitizer startup failure is
BLOCKED, never PASS. These component tests do not certify all older UI/audio
transfers, the ESP32 SDK, SDL internals or physical driver scheduling.

## Actual application and touch tours

```sh
pio run -e native -e native-cyd
python tests/native_tour.py --output delivery/native
tests/touch_tests.sh
SANITIZE=1 tests/touch_tests.sh
python tests/touch_tour.py --binary .pio/build/native-cyd/program --output delivery/native-cyd
```

The keyboard tour enters all twenty rites three times, verifies matching leave
events and captures the five instruments' final rendered frames, sustained PCM
and timing records. The touch tour runs the production controller in eight
finite application processes, checks rendered labels against the pinned Font0
glyphs with negative controls, changes all five instruments, types annotated
lyrics, exercises layered fullscreen Visions, persists a palette across restart
and tests all four orientations. Its radio identities are deterministic fixtures.

For interactive desktop use run `.pio/build/native/program` or
`.pio/build/native-cyd/program`. `GRIM_WAV=/path/output.wav` records PCM;
`GRIM_METRICS=/path/metrics.json` records callback and frame-cycle histograms;
`GRIM_SETTINGS=/path/settings.bin` opts native-CYD into checksummed persistence.
Frame-cycle timing includes deliberate 33 ms pacing. Synthetic `GRIM_SCRIPT`
input is handled on the UI thread; `quit` drains/releases the audio and rite.
Script dispatch preserves declared inter-event spacing even after a slow frame;
one action runs per poll. A forced 600 ms stall fixture exercises the real
controller's release debounce and timer rollover. See the [qualification history](QUALIFICATION.md).
Native timing is desktop evidence and must not be presented as ESP32 headroom.

## CI artifact interpretation

The workflow checks out the exact PR head SHA for every job, independently
builds all six application environments, runs three portable modes and the
existing Windows VLC build. Firmware bundles retain app/full images, partition
inputs, debug symbols, manifests, checksums and safety instructions. Simulator
bundles retain the Linux executable, captures, WAVs and reports. Qualification
logs upload even on failure; their presence alone does not mean the job passed.
Artifacts expire after 30 days and may require GitHub sign-in to download.
Preserve the exact manifest and checksums with any owner evaluation receipt.
