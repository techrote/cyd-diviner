# Build and validation receipts

`pinned.log` contains the final seven successful builds for source `a4549f8`.
`events.log` contains the successful supplementary event-profile build from
`aeb9ac2`. The JSON build receipts identify the exact images and map hashes.
`harness-tests.log` and `i2s-counter-test.log` are host validation, not physical
performance captures.

Initial attempts are retained for transparency. `initial-baseline.log` includes
a Windows/WSL shared-build-directory invalidation, corrected by using separate
build directories. `initial-profile.log` includes the ESP32-C3 `int32_t` versus
`int` template mismatch, corrected with explicit-width min/max calls. The later
integration and pinned logs pass. These build-attempt failures are not hardware
fault measurements.
