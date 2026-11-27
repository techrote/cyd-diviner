# Headless S3 SRAM snapshot probe

This separate ESP-IDF application captures the native RF dump into its actual
64 KiB SRAM aperture, then transfers raw words. It has no Diviner, display,
FFT, FIR, software decimator, PSRAM, network sockets or live-audio dependency.
Idle boot initializes only the command interface, with no PHY or Wi-Fi startup.
The explicit `metal init phy` path first powers the owned Wi-Fi/BT MAC-BB domain,
then calls the pinned SDK's direct `esp_phy_enable(PHY_MODEM_WIFI)` and
`phy_wifi_enable_set(1)` receive-mode API without
creating a Wi-Fi driver or default event-loop task. Wi-Fi mode NULL remains an explicitly selectable fallback.
The SDK's own PHY calibration/PLL support remains part of that direct API;
this does not claim that no SDK support code runs. NVS is not
initialized, erased or written; Wi-Fi NVS and PHY calibration persistence are
disabled. This is a new experimental image, not either approved earlier probe.
Building does not open a port, flash or capture RF.

## Provenance and memory ownership

SDK: ESP-IDF 5.5.5 commit
`b774170ff46c393eeb5e495ea37936038d3f4f4f`, checked by build and CMake.
The private PHY ABI and MMIO layout are explicitly scoped to ESP32-S3.
GPL-3.0 ESPARGOS/esp-sdr gain/DC helpers from commit
`3ab3b65bb62f2d9427ee33c2c3b627427e729294` are retained in
[`vendor/esp-sdr`](vendor/esp-sdr/PROVENANCE.json), with upstream license and
source hashes. Helpers remain byte-identical upstream except the explicitly
recorded read-only diagnostic getter appended to `rx_gain_verify.c`; original
apply/check behavior is unchanged. The original small snapshot adapter follows register facts in
that commit's `main/targets/esp32s3/receiver.c`:

* Dump control `0x60033d5c`: start pulse bit 19, completion bit 18, count low bits.
* Dump lanes `0x60033d90`: native lanes 0, 1, 2, 3.
* SRAM owner `0x600c101c`: low four bits set to owner 4 during acquisition.
* Native rate selection: zero for nominal 80 MS/s, bit 15 for 40 MS/s,
  bit 16 for 16 MS/s. These are upstream native clock selections, not software
  resampling or an independently calibrated sample-clock measurement.

Only `[0x3fcd0000,0x3fce0000)` is reserved from the heap. Linker assertions also
keep static data/BSS below its DRAM address and code below its IRAM alias
`0x403c0000`. The continuous receiver's extra ring banks are not reserved.
The hardware count is limited to **16,380 raw 32-bit words**; the remaining four
words of the bank are checked tail canaries. A preexisting active dump is
rejected before any aperture or dump-register write. Changed owner, lanes and
control are restored on completion or timeout, then read back. Unchanged
sentinel words, a changed canary or a restore mismatch fail the snapshot.
Sentinel checking is a diagnostic rather than an independent analog sample
counter: a coincidental valid raw-word match could conservatively reject data.

The acquisition holds the PHY mutex and a core-0 critical section. Before it,
USB output is flushed. During it, application USB/stdio/allocation calls are
absent and the installed core-0 USB ISR is masked. Ordinary USB bus SOF traffic
from a connected host is not a radio timing input. CRC, formatting and transfer
occur after acquisition. Tuning and verified manual gain preparation occur
outside the acquisition interval. No promise of gap-free capture is made.

| Nominal clock | Maximum 16,380-word observation horizon |
| --- | ---: |
| 16 MS/s | 1,023.75 µs |
| 40 MS/s | 409.5 µs |
| 80 MS/s | 204.75 µs |

## Direct-PHY power prerequisite and failed prior image

The first owner-approved direct-PHY RX image failed before reaching the CLI:
the watchdog backtrace was in startup RX DC calibration, waiting for IQ-estimator
completion. Its immutable runtime/symbolication logs remain in
`evidence/hardware-access/metal-rx-com22-*`. The pinned SDK's
`components/esp_phy/test/test_phy_rtc.c` explicitly calls
`esp_wifi_power_domain_on()` before direct PHY initialization, with a comment
that it powers the Wi-Fi/BT MAC-BB domain. `components/esp_wifi/src/wifi_init.c`
also powers that domain before internal initialization. In `phy_init.c`, the
alias target `esp_wifi_bt_power_domain_on()` clears power-down/isolation and
resets the modem block. `esp_phy_enable()` enables PHY/common clocks but does
not perform that MAC-BB power-up.

The corrected direct path therefore owns one domain reference, enables PHY,
enables the SDK receive mode, and on leaving disables that receive mode,
disables PHY, then releases the domain reference. It does not
claim ownership of a domain reference created by the Wi-Fi fallback. Info/init
records expose `direct_power_domain_owned`; info separately records whether
initialization was attempted. Same-mode initialized requests do not double
acquire. Idle boot invokes no initialization, so the explicit fallback remains
reachable after reset. No calibration bypass, watchdog suppression or ad hoc
BB/IQ/SRAM register change hides the original failure. This source correction
is evidence-based. The power-only correction subsequently booted and initialized
on both owned S3 boards, but manual-gain tuning failed for every tested index
in direct mode while the Wi-Fi-NULL fallback succeeded. The further explicit
receive-mode call mirrors `esp_adapter.c` in the pinned SDK, whose public PHY
header documents this API as Wi-Fi RX enable. Its physical effect is pending
separate image approval and measurement. `direct_rx_enabled` records the void
API's owned invocation state; it is not independent proof of RF readiness.
Init/diagnostic records expose enable/disable invocation counters (uint32,
modulo wrap), and same-mode initialization does not repeat the calls.
The timed snapshot body remains byte-identical.

## Build only

```powershell
./build.ps1 -IdfRoot C:/Users/_/.codex/esp-idf-v5.5.5 -IdfToolsRoot C:/Users/_/.codex/idf-tools-v5.5.5 -BuildDirectory C:/cyd-diviner/perf-characterisation/.pio/sdr-metal-rx
```

The partition description retains a factory app at `0x10000` with capacity
`0x140000`; it does not authorize a partition or bootloader write. Existing
partition compatibility and the exact compiled image must be reviewed before
the owner authorizes any flash. Source identity includes the Git commit plus
an exact digest of compiled project sources, vendored helpers, configuration
and partition description. Compilation is not evidence of physical capture.

## Stable host command contract

USB Serial/JTAG uses the native interface; a host baud setting does not set its
physical USB rate. Commands are ASCII lines. Responses are `METAL ` followed
by one JSON object and LF. Schema is integer 1. Run IDs contain 1–31 ASCII
letters/digits/underscore/hyphen. Do not send a new command until `run_end` or a
terminal `error`; this is a single command loop, not a multiplexed transport.

```text
metal info
metal temperature
metal diagnostics
metal init phy|wifi
metal tune MHZ FOFS_KHZ GAIN_INDEX
metal capture ID RATE_HZ SAMPLES REPEATS ARM_DELAY_US GAP_US TIMEOUT_US
metal bench ID RATE_HZ SAMPLES REPEATS ARM_DELAY_US GAP_US TIMEOUT_US
metal read ID
```

`init phy` selects the experimental direct PHY path; `init wifi`
selects the explicit Wi-Fi-NULL fallback. Type `init` reports `ok`, requested
path, actual mode, error code and receiver readiness. Switching tears down the
prior mode, invalidates the retained raw buffer and requires fresh RX/gain
preparation. Wi-Fi stop/deinit and event-loop delete statuses are retained;
`INT_MIN` means not invoked. A direct PHY enable/disable API returns no status,
so a successful command records the invocation, not proven RF readiness.
`rx_ready` becomes true only after the configured tune/manual gain is verified.
An initialization failure retains actual stage flags and does not auto-fallback.

`info` reports profile `s3-metal-snapshot`, source SHA/digest, SDK identity,
backend commit, MAC/revision/CPU clock, actual initialization mode, Wi-Fi/event
loop stage flags, initialization/cleanup error status, memory
reservation, byte-capable internal heap, configured tune/gain and transfer
failure count. It does not start RF capture.

`tune` invalidates retained SRAM data (calibration may reuse the aperture),
then applies the selected receive LO, signed offset in kHz and calibrated
manual gain index, then emits type `tune` with `ok` and requested settings.
It next emits `rx_diagnostics`; `metal diagnostics` can explicitly query the
same record outside any capture. The `failure_stage` is the last tune decision:
`not_attempted`, `invalid_arguments`, `maximum_gain_out_of_range`,
`requested_gain_above_maximum`, `gain_table_code_unavailable`,
`analog_gain_not_latched` or `ready`; idle/partial initialization reports
`phy_not_enabled`. No failure is silently converted to a successful capture.
Read-only fields include the raw AGC maximum/forced register, SYSCON BB config,
clock/reset, gain-table hook/code availability, expected RF/BB analog words
and actual PBUS words. These sequential readbacks are not an atomic hardware
snapshot. They are read only while a known PHY path is enabled; when
`registers_valid` is false their zero fields are placeholders, not readings.
No arbitrary register writes were added to repair a failed gain check.
The default is 2433 MHz, −80 kHz, gain index 40, matching the earlier centering
experiment. Indices are PHY calibration-table indices, not calibrated dB.
The numeric tuning API accepts 1–65535 MHz; acceptance is not a supported RF
band or sensitivity claim. Gain must exist in the actual calibrated table.

A faster repeated-capture mode is a possible later experiment: prepare/verify
gain once before a finite batch, retain current ownership/canary/timeout checks
for every snapshot, and report the verification scope explicitly. It is not
implemented here, and no higher duty cycle is claimed from this proposal.

`capture` and `bench` take nominal `RATE_HZ` exactly 16000000, 40000000 or
80000000; `SAMPLES` is 1–16380. Other numeric arguments are unsigned 32-bit
values; repeats and timeout must be nonzero. A practical timeout is 20000 µs.
It is a polling timeout, not a desired capture length. A very long owner-chosen
timeout can exceed the enabled interrupt watchdog if hardware never completes;
the default SDK watchdog has not been disabled to hide a hung register path.

Examples:

```text
metal init phy
metal tune 2433 -80 40
metal capture rx80 80000000 16380 3 100000 0 20000
metal bench timing80 80000000 16380 100 100000 0 20000
metal read retained80
```

After successful preparation, both run modes emit type `armed` with ID, mode,
requested count/rate/repetitions, arm delay, gap and timeout. That line is
flushed before the delay begins. The pre-arm delay allows a separately
controlled transmitter to run during the receive snapshot; it does not
synchronize independent boards' clocks.

### Explicit temperature queries outside capture windows

`metal temperature` emits one `METAL` type `temperature` JSON acknowledgment,
with `temperature_c_driver` numeric or null, `temperature_value_valid`,
`install_status`, `enable_status`, `read_status`, `disable_status` and
`uninstall_status`. `status_not_called=-2147483648` marks stages not invoked.
The command locally installs the S3 sensor for the SDK's 10–80 °C configured
range, enables/reads/disables/uninstalls it, and retains no successful handle or
polling task. Read failure, nonfinite value or numeric formatting failure leaves
temperature null. The raw SDK statuses remain inspectable. A cleanup failure is
not silently treated as a clean sensor release; the host should reject that
case and stop the measurement sequence for recovery.

`query_completed_us` timestamps completion of the query/cleanup on the receiver
clock. `scope` is `explicit_outside_RF_window; SDK_driver_not_independently_calibrated`.
The driver Celsius indication is not independently calibrated die temperature
and is not interchangeable with classic ESP32 RTC-library raw sensor codes.
The host should query before arming and after `run_end`, or after a completed
`read` payload. It must not queue the query into an active capture command or
include its installation/read/cleanup time in capture or no-export bench timing.
The default capture loop and its register/timing behavior do not poll the sensor.

### Capture mode binary boundary

For every completed snapshot, the target emits one type `snapshot` JSON line,
**exactly `raw_bytes = sample_count * 4` binary bytes**, then one LF. Parse by
the declared length: raw bytes may contain any ASCII/control characters.
There is no delimiter escaping. Validate CRC and the following LF before
reading the next JSON line. On USB write/flush failure a partial payload can
be left; preserve that failed acquisition and resynchronize using a fresh
command/connection rather than interpreting its bytes as complete data.

Important snapshot keys:

* `id`/`repetition`: current export request; capture repetitions start at 1.
  A later `read` uses repetition 0. `source_id`/`source_repetition` preserve
  the capture that created the retained buffer.
* `sample_count`, `raw_bytes`, `raw_format` =
  `LE32_I10_Q10_metadata12_native`. Each little-endian word has signed two's
  complement 10-bit fields in bits 0–9 and 10–19; bits 20–31 are retained
  unmodified native metadata. Sign-extend each 10-bit field. The pinned
  upstream wire convention calls these I and −Q; preserve native words and
  state any later Q-sign conversion. Envelope power is unaffected by that sign.
* `crc32`: eight lowercase hex digits, standard reflected CRC32 equivalent to
  `zlib.crc32(payload) & 0xffffffff`, using `esp_rom_crc32_le(0,payload,bytes)`.
* `nominal_rate_hz`, `nominal_horizon_us`: configured native clock and arithmetic
  count/clock duration. Each snapshot starts its local sample index at zero;
  no continuous stream index is implied.
* `trigger_before_us`, `done_observed_us`, `capture_cycles_mod32`: target
  timestamps and unsigned cycle-counter delta around register triggering and
  observed completion. The cycle field is modulo 2³². These include CPU
  register/polling observation overhead; they are not synchronized RF latency.
* `control`, `status`, `owner_before`/`owner_after`,
  `lanes_before`/`lanes_after`: raw hex diagnostics; completion, canary and
  register restoration are checked before data export.
* Internal byte-capable heap before the run and after the snapshot (or after
  the no-export bench loop for a later `read`). The explicit `heap_scope`
  describes that span; formatting/transfer do not allocate application buffers.

### No-export timing mode and terminal records

`bench` makes repeated captures without per-capture CRC, formatting or USB
output. It retains only the most recent complete raw snapshot. Register/SRAM
setup, sentinel checking, gain checks and requested inter-snapshot gaps remain
real costs in its loop. The final type `run_end` reports requested/completed
counts, reason, loop begin/end/elapsed µs, first trigger/last completion,
sum/max of observed capture intervals, sum of cycle deltas, arithmetic sum of
nominal sample horizons, heap before/after, last canary/restoration state and
transfer-failure delta. It computes the retained buffer's CRC only after the
loop. Use `metal read ID` to export that last snapshot separately.

`capture` also ends with `run_end`; its loop elapsed time includes deferred
USB transfers and is deliberately a different duty/cost scope. In either mode
any capture failure stops the run with `ok=false` and an explicit reason. A
failed subsequent capture invalidates the retained buffer rather than replaying
stale bytes as a new success. `read` exports one snapshot and no `run_end`.
An invalid command/preparation emits type `error` without `armed`/`run_end`.

Compute separate figures for requested nominal sample horizon, observed
trigger-to-completion interval, no-export loop occupancy and exported-loop
deadtime. Gaps between snapshots, startup settling, hardware clock accuracy,
RF bandwidth and physical signal origin require actual measurements. None is
inferred from a successful build, CRC, configured sample rate or CPU timing.
