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
* `UNAPPROVED-PROFILING-IMAGES.zip`: opt-in app images and compressed maps as
  evidence unapproved for production. The primary image was owner authorized
  for physical tests. No flashing script, bootloader or partition replacement.
* `UNAPPROVED-I2S-EVENT-IMAGE.zip`: separately identified supplementary image
  with bounded driver-event collection, authorized after the primary campaign.
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
