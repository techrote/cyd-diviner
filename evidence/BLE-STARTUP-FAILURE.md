# Directed BLE startup failure in the instrumented CYD image

The closed `physical/link-ble-01` attempt is **FAILED: 18/18 planned cases**.
The first CYD `perf link arm ble oneway 64 1` causes an actual core-0
`StoreProhibited` panic and reboot before its setup acknowledgment. No BLE
traffic window, RTT, goodput or connected-idle measurement is established.
The later 17 failures report `ble_mode_required` after that reboot; they are a
state-loss cascade, not 17 independent radio or hardware capability tests.

The host preserves the panic in
`physical/link-ble-01/cyd/serial-rx.redacted.bin` at byte offset **6,788**. The
raw file has 62,967 bytes and SHA256
`966742e11eb883c2102d9d2d46c2350517030ca8a575dedd1bb2b5809c84c675`.
The closed `results.json` SHA256 is
`1d231d28c256d1e81fe796e2c683342457893c3df8b6e3a4427faf7e7038fedd`.
Both are retained failure evidence; failed setup is not a zero-throughput result.

The panic prints ELF SHA256 prefix `479054cfb079834d`, matching the exact
approved-build ELF:

```text
C:/cyd-diviner/link-approved-build/.pio/build/cyd-profile-link/firmware.elf
SHA256 479054cfb079834ddc4f4a0ca07ec9e2f82be9b39607b50d9aab2a706739b0dd
```

The saved exception contains:

```text
Core 0: StoreProhibited
PC 0x40091ecc   EXCCAUSE 0x0000001d   EXCVADDR 0x00000000
A2 0x00000000   A3 0x00000000         A4 0x00000130
Backtrace:
0x40091ec9:0x3fff9970 0x40172ad4:0x3fff9980
0x4015336d:0x3fff99a0 0x4016d333:0x3fff99c0
```

The installed `xtensa-esp32-elf-addr2line.exe -a -f -C -p -i -e firmware.elf`
decodes these addresses as follows. Source line numbers come from the actual
shipped SDK archive's debug information, whose build paths are not local source
files.

| Address | Function | SDK/debug source |
|---|---|---|
| `0x40091ec9` | `memset` | Xtensa newlib `memset.S:143` |
| `0x40091ecc` | `memset` | Xtensa newlib `memset.S:150` |
| `0x40172ad4` | `bta_sys_init` | `bta/sys/bta_sys_main.c:171` |
| `0x4015336d` | `btu_task_start_up` | `stack/btu/btu_task.c:276` |
| `0x4016d333` | `osi_thread_run` | `common/osi/thread.c:165` |

Disassembly of this **same ELF** establishes the immediate failure more firmly
than a guessed source line: `bta_sys_init` loads `bta_sys_cb_ptr` (symbol address
`0x3ffd5b84`), passes its value as the destination, zero as the fill byte and
`0x130` (304) as the length to `memset`. The exception registers identify a null
destination. This is an SDK BTA control-block null dereference during Bluedroid
startup, before application packet processing.

The same ELF's `btu_task_start_up` calls `BTE_InitStack` at `0x4015336a`, then
calls `bta_sys_init` at `0x4015336d` **without testing the initialization result**.
`BTE_InitStack` has checked initialization/allocation branches into
`BTE_DeinitStack` and an error return. That cleanup frees and clears BTA pointers.
The matching public IDF 4.4.7 sources show the same
[initialization/error-cleanup structure](https://github.com/espressif/esp-idf/blob/v4.4.7/components/bt/host/bluedroid/main/bte_init.c#L310-L480),
[unconditional startup sequence](https://github.com/espressif/esp-idf/blob/v4.4.7/components/bt/host/bluedroid/stack/btu/btu_task.c#L247-L268)
and [control-block zeroing](https://github.com/espressif/esp-idf/blob/v4.4.7/components/bt/host/bluedroid/bta/sys/bta_sys_main.c#L154-L173).
The local SDK identifies itself as `v4.4.7-dirty`; the actual ELF/disassembly is
the authority for the flashed executable, rather than assuming identical line
offsets to the public tag.

An initialization/allocation failure followed by this unchecked continuation is
the leading explanation. **The exact failed allocation and its heap capability
were not recorded**, so neither ordinary out-of-memory, fragmentation, prior
heap corruption nor a particular SDK subcomponent is independently proven as
the original trigger. The backtrace does not identify a GATT callback or payload
buffer overflow. Adapter source calls `esp_bluedroid_init` before registering
its GAP/GATT callbacks, which further locates the observed failure before the
adapter's traffic processing. This does not rule out every possible earlier
application contribution to heap state.

Before startup, CYD `link_info` reports free heap **83,024 bytes** and largest
block **47,092 bytes**. That info-time reading includes its temporary 6,144-byte
formatting allocation; its destructor frees the buffer before the later arm
command. The raw scene-entry diagnostic then reports **89,184 free bytes**.
There is no heap snapshot after controller/host allocations and immediately
before the null dereference. Do not claim that the temporary JSON buffer was
still resident at the crash or subtract the initial heap values to invent a
precise Bluedroid memory requirement.

The pinned classic SDK configuration enables both Bluedroid and classic BT,
with BTC and BTU task stacks of **8,192 bytes each**, pinned to core 0, and
`CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST` disabled. The actual CYD has no PSRAM.
The exact ELF's startup code initializes RFCOMM, A2D and AVRC before its BTA
allocations even though the adapter requests the controller's runtime BLE mode.
That mode request does not remove the compiled classic host components. The
profiling image also retains its static histogram/sample arrays, display state,
link task and bounded adapter queues. Consequently this failure characterizes
**the approved full instrumented image and pinned SDK memory/startup path**; it
does not establish that classic ESP32 silicon cannot support BLE GATT or that a
minimal BLE-only image would fail.

The separate controller-initialization capture and its accepted return values
must remain distinct from this failed full host/GATT startup. An earlier
primary-image generic BLE initialization error also lacks the status/backtrace
needed to declare it the same cause. No measured BLE service envelope or
offload break-even recommendation can be derived from this failed attempt.

Any later investigation should first record allocation failures and per-stage
internal-heap state, then compare an isolated minimal BLE-only host build with
the full profiling image. Such work would be a separate concrete image and
physical trial. This audit makes **no firmware changes, library patches, memory
release calls, resets, flashes or serial accesses**. Independent ESP-NOW/Wi-Fi
measurements can proceed after owner-controlled fresh boots without presenting
this BLE failure as a hardware limit.
