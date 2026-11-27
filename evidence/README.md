# Evidence classes

This directory intentionally separates observations from unexecuted tests.

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
* `SDR-RESEARCH.md`, `LOW-LATENCY-AUDIO-RESEARCH.md` and the calculated budget
  CSV: pinned primary-source research. Continuous SDR protocol and audio MVP
  development is separate from these already measured baseline captures.
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
* `WINDOWS-CAPTURE-PACKAGE.zip`: portable capture/analysis tools, plans, schemas
  and instructions; install the pinned Python dependency before use.

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
