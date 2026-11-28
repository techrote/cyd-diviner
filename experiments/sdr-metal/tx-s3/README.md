# Isolated ESP32-S3 SDR-metal TX variant

This independent target uses the same finite experiment contract as the
[classic headless transmitter](../tx/README.md), with target-specific PHY
evidence and native USB Serial/JTAG control. It does not modify approved
firmware. No physical result or flashing authorization follows from a build.

Pinned ESP-IDF: `b774170ff46c393eeb5e495ea37936038d3f4f4f` (v5.5.5).
An independent audit of its `components/esp_phy/lib/esp32s3/libphy.a` finds:

* `start_tx_tone` is the S3 six-argument wrapper; it is not the classic
  `ram_start_tx_tone` symbol. Its lower helper is `start_tx_tone_step`.
* The S3 first-tone word is **0x60006040**, with enable from argument0 at bit18
  and negated eight-bit amplitude argument2 at bits10..17. Second-tone word
  0x60006044 corresponds to arguments3/5. These addresses were not inherited
  from the classic ROM's different first-tone address0x600060b8.
* Full tone setup also changes global controls at0x60006000 bit26 and
  0x600061e4 bit10. This probe performs SDK initialization once and preserves
  that setup while gating only first-tone bit18. It never invents a second-tone
  configuration or writes the classic address on S3.
* The vendor `stop_tx_tone(1)` path clears first-tone bit18. That supports the
  field inference, not a measured promise of bandwidth, phase or RF cessation.

`gate` and optional `ask` preserve other initialized bits. The expected-enable
readback check precedes the experimental MMIO loop; unexpected state yields an
unavailable result. ASK fields come from observing each SDK-initialized backoff,
not assuming that a requested quarter-dB value is the register's raw byte.

Direct-PHY initialization is the default experimental path; `init wifi` selects
the Wi-Fi NULL comparison. Neither default boot nor informational/configuration
commands initialize RF. Both use calibration storage disabled and no NVS
initialization, scans, association or packet traffic. The retained application
capacity is checked by the owner before any separately authorized app-only flash.
The direct sequence powers the MAC/BB domain with
`esp_wifi_power_domain_on()` before `esp_phy_enable()`, then disables PHY before
the matching owned `esp_wifi_power_domain_off()`. This follows the pinned SDK's
own direct-PHY test and Wi-Fi initializer. Clock enables alone do not satisfy
that prerequisite. Both power functions are void; reported ownership is the
invocation/reference scope, not a measured physical-power or RF-off assertion.

The LF-only upload, private one-use permit and command schema match the classic
variant:

```text
info
reserve SYMBOL_CAPACITY TRACE_CAPACITY
observe TRACE_STRIDE
init phy|wifi
load BIT_COUNT CRC32_HEX
permit PRIVATE_NONZERO_8HEX
tx api|ram|gate|ask|fast BAUD BACKOFF0 BACKOFF1 DURATION_US MASK_SEGMENT_US PRIVATE_PERMIT
temperature
```

Native USB uses an8192-byte TX and4096-byte RX driver buffer. Host baud selection
does not clock native USB. Only control/upload and deferred result output use
USB; the IRAM/DRAM timing loop performs no USB operation or allocation. GPIO
UART0 is not the input path. The profile is `s3-sdr-metal-tx`, protocol2.

S3 control/result records use one static **4096-byte** formatter followed by
explicit `usb_serial_jtag_write_bytes()` calls, without per-character stdout
VFS output. The write-all helper checks a five-second deadline between bounded
100 ms driver waits; a final driver call can extend beyond that deadline by
its wait. A separate drain waits at most five seconds. Short/zero/error return
counts, formatting failures, flush failures and requested/accepted byte totals
are exposed by `info` and `tx_end`. The32-bit event counters saturate; byte
totals are64-bit. Snapshots exclude the line currently being formatted. These
are driver-API observations, not independent host delivery/JSON attestations.
The helper retries only unwritten bytes; it never retransmits RF payloads.

A partial output failure marks a pending LF resynchronization before the next
record. Deferred trace/segment emission stops after its first failed record,
then makes one bounded terminal attempt. `tx_end` preserves expected and
driver-accepted record counts, prior-drain status, saved/restored-word equality,
the enable bit read after the SDK tone-stop request but before PHY teardown,
and lifecycle ownership/status. Register/API evidence does not prove physical
RF cessation. The final terminal flush's failure is visible in the next
`info`, since it cannot report its own later result. The one-use RF permit is
consumed before `tx_starting`; RF initialization proceeds only if that start
record was accepted and drained. Both IRAM timed-loop bodies are unchanged.

This serializer addresses an observed suffix-only `tx_result` after a masked
run in the previous image. It does not convert that old failed receipt into a
valid run or establish that every output timeout had the same cause. In the
earlier smoke attempt, two temperature records arrived and the third request
had no received bytes; that alone cannot distinguish a sensor/API stall from
control or output transport failure. The pinned VFS's per-character writer can
drop characters after a failed bounded attempt while the outer VFS write
returns the requested size. Explicit checked driver writes make such failures
observable, but physical validation of this new image remains separate.

`temperature` is an explicit outside-RF-window command: install sensor driver,
enable/read/disable/uninstall, emitting each return code. No temperature handle,
task or sensor polling remains in the timing loop; ordinary info/run metadata
has raw-temperature null. Driver Celsius is not independently calibrated and
is not interchangeable with the classic undocumented RTC-library raw code.
Read failures remain null. Byte-capable heap diagnostics are retained.
Non-finite successful driver values also serialize as null, preserving JSON.

Trace and segment limits, requested≤20ms local RTOS-masked segments, two-second
finite payload limit, no inherited baud cap, skipped-slot policy, raw unpaced
variant and measured cycle-bracket caveats are as described by the classic
contract. These are software store/call receipts, not RF edges or recovered
symbol throughput. All segment timing records precede the final `tx_end`.

Build without opening a port:

```powershell
./build.ps1 -IdfRoot C:/Users/_/.codex/esp-idf-v5.5.5 `
  -IdfToolsRoot C:/Users/_/.codex/idf-tools-v5.5.5 `
  -OutputRoot C:/cyd-diviner/sdr-metal-build/tx-s3
```

Host-only validation, without opening a port:

```powershell
python test_output.py
python test_output_bounds.py
python ../test_tx_lifecycle.py
```

The actual-body fake tests cover short/zero/error writes, the exact formatter
boundary, partial-line recovery, counter saturation and the pre-RF drain
gate. Static bounds cover every complete SDR record; target compilation also
checks printf argument types. These tests do not establish USB or RF behavior.

## Experimental fast paced word stream

`fast` is a separate S3-only paced benchmark; the previous raw and generic
loop bodies remain unchanged. It requires positive baud, `observe 0` and
`MASK_SEGMENT_US=0`. It introduces no baud cap below the CPU cycle clock and
does not claim to attain the requested rate. Its IRQ-enabled IRAM loop has
only the cycle wait/finite guard, MMIO write, next-deadline addition, source
count and a fractional carry once per waveform ring. There is no per-word
64-bit division, trace, lateness histogram, USB, allocation or mask branch.

Before RF initialization, a checked internal 8-bit DRAM array of four bytes per
loaded symbol is allocated on demand. It receives the rational cycle deltas
after all SDK startup calibration and tone initialization.
`reserve` releases that optional schedule; otherwise its retained capacity is
reported in `info`. Both waveform and delta ranges, and the loop's IRAM
placement, are checked before admission. The header digest is independently
reported. A failed schedule allocation/placement check consumes the one-use
permit and returns without RF initialization.

For CPU clock `C`, requested baud `B` and ring length `N`, the precomputed
phase-zero deltas are `floor((i+1)*C/B)-floor(i*C/B)`. A fractional accumulator
adds zero or one cycle at each ring boundary. Every ring start is therefore
the exact floor of the global rational clock, with no accumulating rounding
drift. Within each ring a scheduled start is that exact floor or one cycle
early: less than two cycles early relative to the real rational time. The
precomputed table's min/max excludes its separately reported boundary carry;
the last gap can be one cycle larger. CPU 240 MHz is configured, not independently
calibrated against RF. Cycle-counter wrap is handled by relative unsigned
elapsed time; the finite 2 s duration plus one slowest-symbol interval remains
below half the 32-bit counter range.

Late deadlines never select a later source index: every completed write is
the next source word. The loop catches up in order when it can, and overload
slows the stream. The finite end can leave a source tail unsent; it is not an
individual-bit-skip policy or successful delivery at the requested baud.
The minimal path does not measure per-word lateness or maximum transient
backlog; those fields are null. Request, achieved write rate and intact source
prefix count must accompany any receiver observation.

The separate `tx_fast_clock` record reports the requested ideal word count,
unsent tail/ahead counts, next scheduled and ideal source-word clocks, signed
end error, nonnegative end backlog, phase/ring/delta metadata and rounding
bounds. End error is loop elapsed minus the ideal next source-word start;
a negative value can be the normal final wait. These are software-clock
observations, not radiated edge timestamps, decoded throughput or one-way
audio latency. `tx_end` separately requires that clock record's driver
acceptance. New IRQ/masked implementations or receiver-visible performance
require their own reviewed experiments.

Host-only fast validation:

```powershell
python test_fast_loop.py
python test_source_integrity.py
# Build/run test_fast_schedule.cpp with ASAN+UBSAN on a native/WSL C++ compiler.
```

The portable independent mathematical oracle checks exact rational floors,
rounding, distant rings, boundary carries and buffer guards. The actual-loop
fake covers source order with IRQ/overload and a wrapping counter. Neither
establishes target timing or RF waveform fidelity.

## Source memory integrity at PHY lifecycle boundaries

The uploaded packed stream's expected CRC32 is retained, then checked before
radio initialization and again after startup calibration. A mismatch returns
an integrity record and ordinary error without entering a timed loop; the
post-startup failure first cleans up the owned radio. The packed CRC is checked
again after all SDK tone initialization. Only then are the word ring and fast
deltas populated and checked before the loop. The word builder accumulates a
CRC of the intended words separately from the array's subsequent CRC; the
freshly built delta CRC supplies its before/after-loop reference.

`tx_source_integrity` records expected/upload, pre/post-radio, pre/post-loop and
post-teardown packed CRCs, intended/actual word CRCs, delta build/pre/post CRCs,
buffer addresses and byte counts. Unexecuted stages are explicitly marked by
checked/loop-started flags; their zero fields are not measurements. Pre-loop
packed/word/delta mismatch prevents the loop. Post-loop checks are immediately
after the saved-word restoration and before SDK tone/PHY teardown; the final
checks observe teardown separately. `timed_source_integrity_ok` and
`retained_source_integrity_ok` distinguish those stages. A post-teardown-only
change must not be assigned to an earlier RF payload error.

The packed source and word ring are checked in every backend; deltas apply only
to `fast`. `tx_end` requires delivery of this record and exposes the strict
retained-source result. CRC computation, allocation, population and formatting
remain outside all three timed loops. They add setup/teardown time while SDK
radio/tone requests can be active; the two-second requested payload remains
separate from a hard RF-duration bound. These boundary observations do not
exclude CRC collisions or changes between checks and are not independent
over-air payload attestation. No SRAM reservation or new MMIO write was added
to address an unproven overlap; actual addresses and failed checks remain
evidence for further diagnosis.
