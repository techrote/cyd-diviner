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
tx api|ram|gate|ask BAUD BACKOFF0 BACKOFF1 DURATION_US MASK_SEGMENT_US PRIVATE_PERMIT
temperature
```

Native USB uses an8192-byte TX and4096-byte RX driver buffer. Host baud selection
does not clock native USB. Only control/upload and deferred result output use
USB; the IRAM/DRAM timing loop performs no USB operation or allocation. GPIO
UART0 is not the input path. The profile is `s3-sdr-metal-tx`, protocol2.

`temperature` is an explicit outside-RF-window command: install sensor driver,
enable/read/disable/uninstall, emitting each return code. No temperature handle,
task or sensor polling remains in the timing loop; ordinary info/run metadata
has raw-temperature null. Driver Celsius is not independently calibrated and
is not interchangeable with the classic undocumented RTC-library raw code.
Read failures remain null. Byte-capable heap diagnostics are retained.

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
