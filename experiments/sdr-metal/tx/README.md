# Isolated classic ESP32 SDR-metal TX experiment

This original headless IDF v5.5.5 project is separate from all approved probes.
Boot, `info`, `reserve`, `observe`, `init` and `load` do not start the radio.
There is no display, audio worker, wireless packet stack, NVS initialization or
calibration-storage access in the default direct-PHY path. The optional Wi-Fi
NULL initialization comparison does start the driver. Every TX needs a fresh
private one-use permit and an owner-authorized exact image/physical test.

Build with the pinned SDK and a separate output directory:

```powershell
./build.ps1 -IdfRoot C:/Users/_/.codex/esp-idf-v5.5.5 `
  -IdfToolsRoot C:/Users/_/.codex/idf-tools-v5.5.5 `
  -OutputRoot C:/cyd-diviner/sdr-metal-build/tx
```

The script reconfigures source digests, compiles and emits a complete source/
binary/map receipt. It never opens a port or invokes a flashing command.

The pinned SDK is commit `b774170ff46c393eeb5e495ea37936038d3f4f4f`.
The installed official `esp-rom-elfs/20241011/esp32_rev300_rom.elf` implements
`rom_start_tx_tone_step` at `0x400035d0`. Its instructions at `0x40003673`
shift the 0/1 enable argument by18; at `0x40003679` the negated eight-bit
attenuation argument shifts by10. It writes `0x600060b8`, preserves bits28..31,
and has an analogous second-tone word at `0x600060bc`. These are facts from
the SDK's own ROM bytes, not a supported public register specification. The
RAM wrapper reads the corresponding front-end register at `0x3ff460b8`;
the SDK register-base header names that region FE, not the documented BB base.

`gate` initializes a tone through the SDK, requires bit18 already set, then
precomputes two words differing only in bit18. `ask` is explicit optional
research: it initializes both requested SDK backoffs outside timing, captures
their observed amplitude fields, and changes only bits10..17 while preserving
the first initialized word's other bits. The RF-test wrapper transforms the
requested backoff, so no direct raw-field mapping is assumed. Unexpected enable
state produces an unavailable/unverified result without the experimental MMIO
loop. The saved first-tone word is restored before the SDK stops the tone.
RF amplitude, bandwidth, phase continuity, edges and actual cessation require
independent receiver evidence; a CPU store alone proves none of those.

LF-only command lines:

```text
info
reserve SYMBOL_CAPACITY TRACE_CAPACITY
observe TRACE_STRIDE
init phy|wifi
load BIT_COUNT CRC32_HEX
permit PRIVATE_NONZERO_8HEX
tx api|ram|gate|ask BAUD BACKOFF0 BACKOFF1 DURATION_US MASK_SEGMENT_US PRIVATE_PERMIT
```

`load` responds `ready`, then accepts exactly ceil(BIT_COUNT/8) packed MSB-first
bytes using the existing seed-zero ROM CRC32 convention. A CRLF terminator
would insert LF into the binary upload; send LF only. The waveform repeats for
the requested duration, so buffer capacity is independent of symbol-rate and
run-duration ceilings. `reserve` replaces preallocated internal byte-accessible
DRAM buffers; failure leaves an unloaded idle probe. Capacity is reported and
configurable up to actual successful allocation, not silently clipped.

BAUD0 selects an unpaced store/call loop. Positive baud accepts up to the actual
240MHz cycle clock, with no inherited23/100kbaud cap; unrealizable requests
report skipped slots and achieved throughput. Slots are skipped rather than
compressed into a backlog. The finite payload duration is separately limited
to2seconds to bound this experiment, not presented as a throughput ceiling.
The caller should start with bursts≤2ms to match raw RX snapshot horizons.

MASK_SEGMENT_US0 leaves interrupts enabled. A positive value requests repeated
local RTOS-level3-masked segments, up to20ms each; higher-level interrupts can
still run. Masking is accepted only for `gate`/`ask`, avoiding blocking PHY calls
inside a masked region. Actual masked duration and intersegment gaps are
reported. An interrupt or one final loop iteration may overrun a requested
segment; these are measured software bounds, not a hardware RF watchdog.
Mask times bracket the mask-entry/restore calls and can include an interrupt
immediately after restore. They are measured upper bounds on locally masked
time; the implementation does not subtract an invented entry/exit cost.
Each segment and preceding intersegment gap has its own `tx_segment` receipt.
The command requires sufficient preallocated segment capacity for the requested
duration/chunk count; use `reserve` to change that actual memory limit. No
segment loss is silently accepted. `tx_end` follows every trace/segment record.

`observe0` removes per-symbol trace writes for throughput runs. A nonzero stride
samples software completion into the bounded preallocated trace until capacity
is reached; this changes observer cost and must be a separate context. The
IRAM loop uses internal DRAM waveform words and prints/allocates nothing in the
timed interval. `elapsed_cycles/writes` includes loop overhead and enabled
interrupt work; it is neither a single MMIO instruction latency nor the radio's
ultimate modulation limit. `api`/`ram` provide deliberately slower comparisons.
Unpaced, unmasked, observe0 `gate`/`ask` uses a separate minimal store-loop
variant. It does not include per-symbol trace, scheduling or library dispatch;
its aggregate cycle count still includes cycle reads, buffer wrap and any
enabled interrupt work. The result names that variant explicitly. A difference
from the more observed masked loop does not isolate interrupt cost alone.

The direct `esp_phy_enable(PHY_MODEM_WIFI)` path is experimental: the SDK header
normally delegates it to Wi-Fi/Bluetooth drivers. It performs calibration with
persistent storage compiled off; its void enable/disable functions expose no
return-status proof. The expected tone-register state is the pre-MMIO check.
Its prerequisite is balanced `esp_wifi_power_domain_on()` before PHY enable,
then PHY disable before `esp_wifi_power_domain_off()`. The pinned SDK's
`esp_phy/test/test_phy_rtc.c` uses this same power-before-enable order; enabling
clocks alone does not power/de-isolate MAC/BB. Ownership fields record our
void-API invocation references, not physical power or RF attestation.
The alternative Wi-Fi NULL path records stop/deinit return codes. Neither path
scans, associates, sends Wi-Fi packets or changes credentials. Sensor readings
are outside the symbol loop and are undocumented/uncalibrated indications.

Every info/result includes byte-capable heap diagnostics using
`MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT`. The older all-internal free/largest figures
can include32bit-only memory and cannot establish telemetry allocation space.
That distinction is needed for the separately approved PCM setup failure;
this experiment does not modify that firmware or claim to fix it.
