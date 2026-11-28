# Evidence classes

This directory intentionally separates observations from unexecuted tests.
Start with [VIBEWARE recommendations](RECOMMENDATIONS-DRAFT.md),
[current coverage audit](FINAL-COVERAGE-AUDIT.md) and the
[measurement guide](../docs/PERFORMANCE.md). Reports count validated windows
independently of whole-plan status; a closed FAILED capture remains failed.
The latest bare-metal dual-receiver stage is closed, with
[fast/direct-PHY findings](METAL-FAST-FINDINGS.md) and separately checksummed
raw/derived archives. Image approval, software write counts, RF envelope,
decoded payload and audio latency remain different evidence classes.

* `hardware-access/`: COM21 enumeration, initial connection attempts, verified
  backup receipt and separately owner-authorized app-only flash verification.
  The backup operation itself writes only a temporary RAM stub.
* `host-kernels/`: three host correctness/timing repetitions and sanitizer
  receipts. These are **host replay**, not ESP32 performance measurements.
* `native-replay/`: ordinary native build/replay receipts and inspected frames.
  These are simulated display output, not physical LCD readback.
* `build-logs/`, `build-receipt.json`: target compilation, sizes, maps and exact
  binary hashes. Compilation is not physical execution.
* `mission-status.json`, `measurement-status.csv`: explicit campaign coverage.
  Missing physical measurements are NOT RUN, with null values rather than zeros.
* `PHYSICAL-CAPTURES-BASELINE.zip`: closed COM21/COM22 raw captures, including
  failed attempts, actual display readback, PCM, serial RX and event logs.
  `baseline-archive-receipt.json` pins its complete hash and member directories.
* `BASELINE-FINDINGS.md`: preliminary physical observations and their limits.
  `campaign-full/`, `campaign-continuation/`, `campaign-cyd-kernels/` and
  `campaign-s3-partial/` contain compact reports, JSON/CSV, PNG/SVG plots and
  verified provenance manifests. Their original COMPLETE/FAILED states remain.
* `PHYSICAL-CAPTURES-SUPPLEMENT.zip`: closed I2S-event/stress, full S3 memory
  matrix, BLE failure, direct ESP-NOW/SoftAP/phone-AP traffic and display-readback
  captures, including unsuccessful attempts. `supplement-archive-receipt.json`
  pins all 14 capture directories and the archive hash.
* `campaign-events/`, `campaign-ble-init/`, `campaign-s3-kernels/`: compact
  verified reports and structured results for completed supplementary windows.
  `EVENTS-INTERPRETATION-DRAFT.md`, `S3-COMPARISON.md` and `TEMPERATURE.md`
  explain observed audio scheduling, configured-system ratios and sensor limits.
* `WIRELESS-FINDINGS.md`, `WIRELESS-OVERVIEW.json` and `.csv`: finite physical
  topology comparisons, preserving distinct cohorts and failed repetitions.
  Four `campaign-link-*` folders retain complete per-run/group analysis and
  verified raw-archive provenance. `BLE-STARTUP-FAILURE.md` includes exact-image
  symbolication; `DEBUG-LINK-ELFS.zip` preserves its debugging inputs.
* [COLOR-FINDINGS.md](COLOR-FINDINGS.md) and
  [campaign-colour report](campaign-color/REPORT.md): all **36/36** short
  windows, actual packed-intermediate layouts and frame rates. The measured
  path converts/composites around RGB565; it retains the backing reservation
  and 16-bit SPI, without claiming native low-depth LCD savings.
  [PHYSICAL-CAPTURES-COLOR.zip](PHYSICAL-CAPTURES-COLOR.zip) and its
  [receipt](color-archive-receipt.json) preserve the closed raw matrix.
* `SDR-RESEARCH.md`, `LOW-LATENCY-AUDIO-RESEARCH.md` and the calculated budget
  CSV: pinned primary-source research, separate from measured baseline,
  packet-audio and raw-RF results. [SDR-PROOF.md](SDR-PROOF.md),
  [JSON](SDR-PROOF.json) and [CSV](SDR-PROOF.csv) report finite proprietary
  NRZ/TDM-PDM payload recovery and digital reconstruction, with conditional
  payload error, source-frame coverage and missing leading frames separate.
  Host/batch decoding is not continuous on-device audio or one-way latency.
* [SDR-CALIBRATION-INTERPRETATION.md](SDR-CALIBRATION-INTERPRETATION.md) and
  `sdr-signals-*/`: the separately
  authorized physical SDR calibration stage, including the initial API abort,
  250 kS/s delivered-sample gaps, receiver-offset confound and positive RAM
  envelope observations. Signal evidence is distinct from frame/audio decoding.
  [PHYSICAL-CAPTURES-SDR.zip](PHYSICAL-CAPTURES-SDR.zip) retains all ten closed
  raw SDR capture directories, including framed attempts;
  [sdr-archive-receipt.json](sdr-archive-receipt.json) verifies their original
  paths and bytes. These captures are not members of either earlier raw archive.
* [PCM-FINDINGS.md](PCM-FINDINGS.md), `audio-s3-pair-*-analysis/` and the earlier CYD audio
  analyses: finite broadcast PCM digital reconstruction, preserving setup and
  telemetry failures and distinct live/deferred radio-observation policies.
  The live/deferred matched-S3 cohorts validate **22/48** and **25/48** windows;
  both captures remain FAILED. Receiver window/admission rejects are separate
  from unknown RF loss, and no analog latency was measured.
  [PHYSICAL-CAPTURES-PCM-CYD.zip](PHYSICAL-CAPTURES-PCM-CYD.zip)
  ([receipt](pcm-cyd-archive-receipt.json)) and
  [PHYSICAL-CAPTURES-PCM-S3.zip](PHYSICAL-CAPTURES-PCM-S3.zip)
  ([receipt](pcm-s3-archive-receipt.json)) retain all raw attempts.
  [TEMPERATURE.md](TEMPERATURE.md) and
  [pcm-temperature manifest](pcm-temperature/PCM-TEMPERATURE-MANIFEST.json)
  retain 100 explicitly post-stop readings; these do not extend the 1 Hz
  stress-sampling claims or establish calibrated cross-chip temperatures.
* The new isolated [SDR-metal TX](../experiments/sdr-metal/tx-s3/README.md),
  [snapshot RX](../experiments/sdr-metal/rx/README.md) and
  [dual-receiver host method](../tools/perf/SDR-METAL-DUAL.md) remove display,
  audio and streaming USB from timed acquisition. Native finite IQ snapshots,
  MMIO software write timing and shared exposure are distinct evidence from
  continuous bandwidth, physical RF edge timing and synchronized clocks.
  [metal-startup-failure.json](metal-startup-failure.json) preserves the first
  image's PHY-calibration watchdog failure. The separately approved correction
  balances the SDK's power-domain prerequisite; it does not suppress watchdogs
  or calibration. [metal-power-flash-receipt.json](metal-power-flash-receipt.json)
  records exact corrected images and idle runtime identity. The closed first
  metal cohort is retained in [PHYSICAL-CAPTURES-METAL-INITIAL.zip](PHYSICAL-CAPTURES-METAL-INITIAL.zip)
  with its [archive receipt](metal-initial-archive-receipt.json).
  [Receiver timing](METAL-RX-SMOKE-WIFI-FINDINGS.md),
  [software rate and native envelope observations](METAL-RATE-WIFI-FINDINGS.md),
  [native framed payload reconstruction](METAL-FRAMED-WIFI-FINDINGS.md) and
  [the matched position-swap comparison](METAL-FRAMED-POSITION-COMPARISON.md)
  distinguish valid finite windows from failed campaigns. The stronger signal
  changed receivers after their physical positions were exchanged; no
  board-brand ranking follows. The separately approved fast TX/source-CRC and
  direct-RX follow-up completed **78 finite TX windows plus 18 receiver-only
  reference windows**, each with two independent receiver bodies. Its
  [findings](METAL-FAST-FINDINGS.md),
  [raw archive](PHYSICAL-CAPTURES-METAL-FAST.zip) and
  [receipt](metal-fast-archive-receipt.json) retain the new source/image scope.
  Both receivers now initialize/tune through direct PHY. Four-channel 48 kHz
  framed patterns recover at requested 4.416 Mbaud with conditional payload
  errors; requested 8.832 Mbaud produces no matching headers in three windows
  per receiver. Do not infer a hardware ceiling from these finite cases.
  [576 outside-window temperature readings](metal-fast-temperatures.json) and
  [verified final application state](metal-final-audio-restore-receipt.json)
  remain separate evidence.
  A later owner-requested [fan-assisted repeat](FAN-FRAMED-REPEAT.md) retains
  six additional windows with the same images/settings: 4.416 Mbaud control
  and 8.832 Mbaud retry. One receiver then recovered 8.832 Mbaud headers with
  25.5% conditional payload errors; the other recovered none. Fan airflow,
  board movement, geometry and workload history were not independently
  controlled, so this is a separate cohort with no isolated thermal cause.
* [WiDAC Draft/HOLD PR #1](https://github.com/techrote/WiDAC-ESP32S3-SDR/pull/1)
  records the standalone SDR prototypes, lab notebook and byte-exact source
  origins. Historical physical observations retain their original Diviner
  source/image identities; copying a prototype does not rerun its experiment.
* `UNAPPROVED-PROFILING-IMAGES.zip`: opt-in app images and compressed maps as
  evidence unapproved for production. The primary image was owner authorized
  for physical tests. No flashing script, bootloader or partition replacement.
* `UNAPPROVED-I2S-EVENT-IMAGE.zip`: separately identified supplementary image
  with bounded driver-event collection, authorized after the primary campaign.
* `UNAPPROVED-LINK-IMAGES.zip`, when present: the separately identified CYD
  I2S-event/peer profiler and S3 peer/kernel image. These require their own
  explicit flashing authorization. The obsolete event-only image is not
  silently substituted with this different binary.
  Both link applications were subsequently owner-authorized and physically
  flashed; `flash-link-plan.json` and `hardware-access/` preserve verification.
* [UNAPPROVED-AUDIO-SDR-IMAGES.zip](UNAPPROVED-AUDIO-SDR-IMAGES.zip)
  ([receipt](prototype-bundle-receipt.json)): the reviewed colour/PCM and
  original SDR applications, maps and exact adapted GPL receiver source.
  [stage-1](prototype-stage1-flash-receipt.json) and
  [stage-2](prototype-stage2-flash-receipt.json) receipts record their
  subsequent separate owner authorizations and verified physical flashes.
* [UNAPPROVED-SDR-METAL-IMAGES.zip](UNAPPROVED-SDR-METAL-IMAGES.zip)
  ([receipt](metal-bundle-receipt.json)) retains the first metal image/source
  version, including the subsequently observed startup failure. The
  [power-domain correction bundle](UNAPPROVED-SDR-METAL-POWER-IMAGES.zip)
  ([receipt](metal-power-bundle-receipt.json)) pins corrected source
  `3b4161fbcbd6832264019aa3a4fb993a85798857`, exact S3 apps, ELF/maps, licenses
  and build receipts. Keep both versions. Historical `UNAPPROVED` filenames
  do not erase later exact-image test approval or grant production promotion.
* [WINDOWS-CAPTURE-PACKAGE.zip](WINDOWS-CAPTURE-PACKAGE.zip)
  ([receipt](windows-package-receipt.json)): pinned portable capture/analysis
  tools, plans, tests, schemas and source for reproduction. Its source manifest
  and extracted-package checks are retained. Install the pinned Python
  dependency before use; no private provisioning, backups or automatic
  flashing is included. Colour, packet-audio and raw SDR methods remain
  separate even when distributed together.
* [DERIVED-ANALYSES.zip](DERIVED-ANALYSES.zip) and its
  [extraction guide](DERIVED-ANALYSES.md) preserve detailed original spectra,
  decoder trials, native-conversion files and reconstructed PCM/WAVs. Compact
  root reports/JSON/CSV/manifests remain directly inspectable. The separate
  [fast follow-up derived archive](DERIVED-ANALYSES-METAL-FAST.zip)
  ([extraction guide](DERIVED-ANALYSES-METAL-FAST.md)) preserves its own later analysis cohort;
  original manifests verify after extraction into the documented paths.

The source commit for final artifacts is in the build receipt. Firmware identity
also records an exact source-content digest. Evidence-only commits after that
source commit do not change the compiled code. Keep private whole-flash/NVS
backups outside the repository. Raw PCM and physical display readback belong in
new capture directories only after an authorized hardware campaign.

No local performance budget, temperature rise or wireless offload win is claimed
without the required physical measurements. See the
[measurement guide](../docs/PERFORMANCE.md) for methods, limits and recovery.

`mission-status.json` is timestamped. During collection it explicitly identifies
pending measurements; completed campaign reports and raw capture archives are
added when capture ends. Raw serial/PCM/display data remain available locally
under `physical/` during the campaign and are excluded from Git individually.
