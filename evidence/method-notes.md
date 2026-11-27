# Interpretation of temperature, CPU samples and audio gaps

Prepared 2026-11-27 from the pinned local Arduino 2.0.17 / ESP-IDF 4.4.7
package and primary Espressif sources. This note changes interpretation only;
the active primary hardware campaign was not changed. A separately identified
supplementary event-queue build is described below; compilation or presence of
that code is not a physical measurement.

## Profiler memory footprint and portable output fingerprints

The approved primary profiling ELF allocates 21,120 bytes for the fixed
480-entry CPU/heap/temperature sample array and 2,888 bytes for the 19 bounded
timing/counter histograms: **24,008 bytes in these two static arrays alone**.
The ELF symbols are `perf_profile`'s `samples` (`0x5280` bytes) and `hist`
(`0x0b48` bytes). Other profiler state, PCM queue/task storage, optional stress
task stacks and normal application/display allocations are additional.
`perf enable 0` retains these arrays and the other documented observer work.
Available heap and largest-block measurements describe this profiling image
under its actual preceding workload history; they cannot be substituted for
ordinary firmware memory headroom or adjusted by subtracting just this array
footprint. Allocation-success receipts and measured heap/stack values must
accompany scene and kernel timings, especially after radio initialization.

The FFT kernel's legacy `checksum_fnv1a` field is a composed fingerprint:
`FNV1a32(real_float_array_bytes) XOR FNV1a32(imag_float_array_bytes)`, with each
array covering the reported complex-point count. It is not a single canonical
FNV-1a chain over the complete complex output. Other kernel outputs use one
FNV-1a chain; particle fields are explicitly serialized little-endian. Raw
records retain the original field name and values. Cross-architecture floating
checksums can differ with compiler/math behavior; numeric error is assessed
separately by the host double-precision DFT and DSP output comparisons. Host
validation and S3/C3 compile results establish no physical FPU/throughput claim.

## Classic ESP32 temperature indication

For `CONFIG_IDF_TARGET_ESP32`, Arduino 2.0.17 declares the undocumented
`uint8_t temprature_sens_read()` function and implements `temperatureRead()` as
`(raw - 32) / 1.8`. That branch has no calibration, error return or special
handling for raw 128. The separate S2/S3/C3 branch uses the IDF temperature
driver; its behavior and accuracy must not be transferred to classic ESP32.
[Arduino 2.0.17 implementation](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/esp32-hal-misc.c#L52-L71)

The local `cores/esp32/esp32-hal-misc.c` has SHA256
`d6374832d4e7d987e8c9eb71aa36f12e6f03a5d16c298a139a6b222a108788d1`.
The profiling link map resolves this function to `librtc.a(rtc_analog.o)` at
`0x4009b3c8`, in linked executable code. Thus the retained JSON tag
`ESP32_ROM_raw_uncalibrated` is a legacy method name: the exact implementation
observed here is the SDK RTC binary-library function, not a proven mask-ROM
implementation. Preserve that original tag and raw readings for reproducibility.
The supplementary build uses `RTC_library_raw_undocumented_uncalibrated` for
the same function. See the archived profiling map and build receipt.

Espressif member `igrr` explained on 2022-10-05 that the classic ESP32 temperature
functionality was removed from the TRM because the hardware was not reliable
enough; the supported IDF driver described there applies to S2 and later chips.
The current ESP32 series datasheet covers D0WD-V3 and records removal of the
temperature-sensor description in revision 2.2. These sources do not establish
a calibrated temperature specification for this D0WD-V3 board.
[Espressif clarification](https://github.com/espressif/esp-idf/issues/732),
[ESP32 datasheet revision history](https://documentation.espressif.com/esp32_datasheet_en.html)

Report `temperature_raw` as an undocumented, uncalibrated internal sensor
indication. `temperature_c_uncalibrated` is only the Arduino arithmetic applied
to that indication. Neither field establishes accurate die, enclosure or ambient
temperature, a thermal safety margin, or throttling. A changing raw value is an
observed response, not a calibration. Raw 128 is not a documented failure
sentinel in the inspected API. Initial inventory recorded 128 and preflight
records subsequently contain 129–130; retain these integers rather than replacing
them with an assumed invalid value. Ambient temperature was not measured.

## Per-core utilization is periodic task sampling

The local SDK sets `CONFIG_FREERTOS_HZ=1000`; its trace facility and runtime-stat
generation are disabled. The profiler registers a tick hook for each CPU and
counts whether that CPU's current task handle differs from its idle-task handle.
Each exported interval therefore reports:

`busy_pct = 100 * non_idle_tick_observations / total_tick_observations`.

Espressif documents tick hooks as callbacks run at the particular CPU's tick
interrupt. The corresponding task getter reads the current TCB pointer.
[IDF 4.4.7 hooks](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/system/freertos_additions.html#hooks),
[IDF 4.4.7 task getter implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/freertos/tasks.c#L4339-L4348)

Method inference: these are periodic samples, not a cycle counter or an integral
of task runtime. Short work/idle spans between ticks can be missed. A workload
synchronized with the tick may produce persistent sampling bias; more samples
alone do not remove that phase relationship. Interrupt execution is not separately
attributed and can remain associated with the interrupted task or delay sampling.
Consequently, near-100% means nearly all observed ticks found a non-idle task;
it does not prove that every cycle was spent on benchmark arithmetic.

Keep per-core raw busy/total counts, interval timestamps, distribution and observed
stress duration with that claim. Unsupported hooks or intervals without tick
observations are `null`. ESP-IDF's optional runtime statistics instead require
a timer/counter and accumulate execution time; they are a different method.
[IDF 4.4.7 runtime-stat requirements](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/system/freertos.html#_CPPv420vTaskGetRunTimeStatsPc)

## Legacy I2S: enqueue timing and underflow are distinct

The IDF 4.4.7 classic ESP32 ISR distinguishes descriptor errors
(`I2S_EVENT_DMA_ERROR`) from the TX buffer queue becoming full of empty buffers.
The latter branch identifies underflow and emits `I2S_EVENT_TX_Q_OVF` when an
application event queue exists. `tx_desc_auto_clear` clears completed TX buffers
to avoid replaying old data. The public event name alone does not explain that
underflow meaning; this distinction comes from inspecting the reference driver.
[IDF 4.4.7 I2S ISR](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/i2s.c#L413-L463)

The primary `a4549f8` Diviner build installs I2S with event queue size zero and a null queue
handle. It consequently does not count those driver events. Report hardware/DMA
underruns as unknown in this capture, not zero; also do not claim that legacy
I2S intrinsically cannot expose underflow information. Adding event collection
would require a separately identified build and measurements of its overhead.

`i2s_write` waits for available DMA buffer space and copies input into that space;
a successful write counts enqueue completion, not analog output continuity.
The profiler's >6000 us block-start flag is an inferred scheduling observation.
PCM queue drops and receiver CRC/sequence gaps describe the USB/UART path.
These observations cannot be summed or substituted for detected I2S underflows,
audible glitches, or an external analog continuity measurement.
[IDF 4.4.7 I2S write implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/i2s.c#L1982-L2037),
[legacy I2S API](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/peripherals/i2s.html)

## Supplementary opt-in driver-event build

`cyd-profile-events` extends the profiling build with
`GRIM_PERF_I2S_EVENTS=1`; ordinary and base profiling builds retain the zero-event-
queue installation and have no audio-loop event drainer. The supplement requests
a 32-entry I2S event queue and drains at most 32 entries at the beginning of each
audio iteration, including warmup and periods outside collection. No library
patch or replacement ISR is used. It requires its own binary identity, owner
flash authorization, physical captures and overhead comparison.

`perf info` reports configured event support, capacity and actual queue
availability. After a run, a `type: i2s_events` record precedes the final end
marker; the original 19 timing/counter metrics are unchanged. The record separates
`tx_underflow_indications` (driver `TX_Q_OVF`: the free-TX-buffer queue is full
of completed/empty buffers), `dma_descriptor_faults`
(`DMA_ERROR`), TX completions and other events. An underflow indication counts
an observed driver notification, not a distinct audible incident or the duration
of a physical signal gap.

Events have no ISR generation timestamp. All counters are assigned using the
drain-completion timestamp in the half-open steady interval `[start, end)`.
`first_drain_offset_us` and `last_drain_offset_us` are relative to steady start.
Warmup/boundary events can therefore shift between windows by the drain delay;
the first counted drain can include events generated before steady start, while
events still queued after the fixed end are excluded even if generated in-window.
`max_drain_interval_us` includes the interval leading into the first counted
drain. Reset and aggregation share the profiler lock and recheck the active
window after acquiring it. Histogram disabling does not disable event draining
or these counters.

Queue occupancy is sampled before and after each drain. `pending_max`,
`queue_full_observations` and `drain_limit_hits` reveal observed pressure, but
cannot count notifications lost between observations. Both `event_loss_count`
and `analog_gap_count` remain `null`. Zero observed underflow indications does
not prove analog continuity; observed saturation makes completeness particularly
uncertain. `max_drain_us` times queue reads/occupancy checks and excludes the
subsequent counter-fold operation. Compare the supplement's measured resource,
CPU, scheduling and audio timing against matching base-profile captures.

The host-only test exercises the actual portable aggregation logic:

```sh
g++ -std=c++17 -Wall -Wextra -pedantic tools/perf/test_i2s_events.cpp -o /tmp/cyd-perf-i2s-counter-test
/tmp/cyd-perf-i2s-counter-test
```

It checks inclusive start/exclusive end, collection gating, stale-drain rejection
after reset, separate event categories, saturation observations and an observed
empty drain. It does not simulate the ESP32 ISR, UART, DAC or a physical underflow.

## Pinned watchdog configuration and audit limits

The actual classic ESP32 SDK `tools/sdk/esp32/sdkconfig` contains the following
settings; the corresponding `qio_qspi/include/sdkconfig.h` agrees:

| Setting | Pinned value |
|---|---|
| `CONFIG_ESP_TASK_WDT` | enabled |
| `CONFIG_ESP_TASK_WDT_PANIC` | enabled |
| `CONFIG_ESP_TASK_WDT_TIMEOUT_S` | 5 seconds |
| `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0` | enabled |
| `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1` | **not set** |
| `CONFIG_ESP_INT_WDT` / `CONFIG_ESP_INT_WDT_CHECK_CPU1` | both enabled |
| `CONFIG_ESP_INT_WDT_TIMEOUT_MS` | 300 ms |
| `CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT` | enabled |

SDK-config SHA256:
`a4fe490abd8cd6447874edd79bde1e78d9630af7845bd53f70440a56b3504907`.
Generated-header SHA256:
`93480847ddf04cf0bb3c1e2aba1b0707eca15ec4d5acdba93a0b3dce32be2241`.

Arduino 2.0.17 starts its priority-1 loop task on core 1 with
`loopTaskWDTEnabled=false`. Repository source has no call enabling the loop or
CPU1-idle task watchdog. IDF startup subscribes each idle task only when its
specific configuration flag is set.
[Arduino startup](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/main.cpp#L64-L66),
[IDF startup subscriptions](https://github.com/espressif/esp-idf/blob/v4.4.7/components/freertos/port/port_common.c#L107-L126)

The worker's 99 ms busy interval plus `vTaskDelay(1)` permits idle execution but
does not guarantee it: another ready task can consume that opportunity. In this
pinned configuration, however, lack of CPU1 idle execution alone cannot expire a
CPU1-idle TWDT subscription because that subscription is absent. CPU0 idle remains
watched. Interrupt-watchdog coverage of CPU1 is separate; ordinary busy task work
with interrupts enabled is not itself evidence of blocked tick interrupts.
[IDF watchdog distinctions](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/system/wdts.html)

If a task-watchdog diagnostic occurs, the missed-subscriber list identifies who
failed to reset; the separate list of currently running tasks is contextual and
does not make every named CPU/task an offender. Panic/reboot is enabled here, so
such an interruption must remain a failed or incomplete capture, not be hidden
by retaining only earlier samples.
[IDF diagnostic implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/task_wdt.c#L140-L161)

Two additional source audits bound interpretation. Stress-iteration counters are
unsigned 32-bit values incremented in batches of 256 and can wrap modulo 2^32;
they are progress indicators, not benchmark throughput totals. Use the timed
window and retained per-core tick observations for sustained-load evidence.
Radio `off` acknowledgments report requested software state. Disable/deinitialize
return statuses and actual RF emissions are not independently exported or
measured; this audit found no observed failure, but it does not establish a
physically verified RF-off baseline. Radio comparisons remain controller-init
and requested-state measurements without peer traffic or RF verification.
