# COVEN and SPECTRA convergence

This implementation integrates the upstream instruments at
`c1b1175bcf15072bc2538ccdde6806d5901b16e9`, preserving the CV1 musical concept
and identity-to-pitch mappings while separating audio ownership and tightening
radio lifecycle behaviour. It is a **Draft/HOLD** software candidate. Physical
radio interoperability, acoustic timing, embedded headroom and owner usability
are **NOT RUN** in this mission environment.

## Implemented boundaries

- `src/coven_protocol.h` contains the allocation-free CV1 parser, membership,
  election, duplicate/order filtering and musical clock. The firmware and
  deterministic simulation compile this same implementation.
- `src/radio_queue.h` is the fixed 15-packet receive queue used by the ESP-NOW
  backend and fault simulation. The firmware serializes it under a short
  `portMUX` critical section; its template is not independently thread-safe.
- `src/radio_sonification.h` contains deterministic identity hashing, scale
  mapping, RSSI amplitude and observation-age calculations.
- COVEN and SPECTRA keep oscillators, filters, delays, envelopes and the formant
  reader exclusively on the audio thread. UI controls use `Latest<T>` complete
  snapshots; bell/speech events use bounded queues. Display telemetry travels
  through a separate audio-to-UI snapshot. Queue saturation drops new events
  and increments counters; the callback never waits or allocates.
- A displaced SPECTRA voice completes its fade before its slot can be reused.
  Repeated sightings of the weaker identity no longer undo that displacement.
  Cached Wi-Fi rows retain the original observation time, preventing every UI
  cache read from making a vanished device appear newly observed.
- SPECTRA's vibrato/breath oscillators advance by the actual 64-sample control
  stride. Their frequency settings now represent cycles per second, rather than
  accidentally running at one sixty-fourth of the intended rate.
- Radio observation age uses wrap-safe half-range arithmetic. If an RTOS
  callback/scan producer samples its millisecond clock just after the UI frame
  sampled its own `now`, that small future skew is treated as age zero instead
  of wrapping into an apparently ancient observation.

Both instruments detach their callback before freeing state and explicitly stop
radio resources on leave. SPECTRA also tears down its radio after failed state
allocation. COVEN remains an autonomous local instrument if ESP-NOW startup
fails and displays `radio unavailable`.

## CV1 compatibility and timing

CV1 remains exactly ten bytes; no packed-struct or native-endian dependence is
introduced:

| Bytes | Meaning | Accepted range |
| --- | --- | --- |
| 0–2 | ASCII `CV1` | exact magic |
| 3 | Kind | 0 presence, 1 call |
| 4 | Root pitch class | 0–11 |
| 5 | Musical mode | 0–3 |
| 6 | Local degree | 0–12 |
| 7 | Beat number | unsigned byte, wraps after 255 |
| 8–9 | Milliseconds into beat | little endian, 0–1799 |

Presence is sent once per second. The beat lasts 1,800 ms. At most ten remote
members are retained, plus the local device; six separate visual familiar slots
cannot evict a musical member. Unknown members beyond that capacity are refused
until a slot expires. Member liveness expires 15,000 ms after the last accepted
new packet. The CV1 `Session` peer table is the sole authority for musical-member
liveness; COVEN's visual/audio seat roster mirrors its corrected receive time and
cannot independently extend a member lease after queue delay. The lowest lexicographic active MAC is leader: this is an ordering
rule, **not device age**. Only that elected peer supplies root, mode and clock.
A surviving elected peer takes over after expiry; becoming local leader retains
the inherited musical phase instead of resetting the instrument.

The beat/phase pair is a modulo-460,800 ms cursor. Older/duplicate packets do not
refresh membership. A call may share a presence timestamp once; calls are
limited per peer and globally to one audible event per 700 ms. Presence bursts
inside 80 ms are refused. Packet lengths, field ranges, source addresses and
queue residence exceeding 2,000 ms are checked. Monotonic elapsed arithmetic
handles 32-bit millisecond wrap. Large phase errors acquire the leader directly;
errors within ±200 ms slew by one quarter of the observed error, capped at
12 ms per accepted update. Unknown one-way delay remains a bias.

These rules coordinate **musical pulses**. The sound event is still scheduled by
the UI/audio-block boundary; there is no synchronized DAC clock, common sample
counter or distributed sample-synchronous DSP graph.

CV1 has no session nonce, global sequence number, authentication or advertised
transitive leader identity. Exact delayed duplicates cannot be distinguished
from a legitimate peer restart after its lease expires; replay across the beat
cycle is likewise ambiguous. A live peer clock moving backward may need expiry
and reacquisition. Compatibility assumes a bounded bidirectional one-hop radio
circle. Hidden nodes and persistent hostile traffic can prevent a common leader
or starve broadcasts. The implementation bounds processing and storage; it does
not claim denial-of-service resistance. A subsequent CV2 design could add boot
nonces, sequence numbers and elected-leader identity while continuing to observe
CV1. No incompatible extension is required for this candidate.

DYAD text remains transient local performance input. The parser bounds the frame
and output, requires the tenth delimiter, strips control bytes, suppresses
repeated lines, and rate-limits speaking. It uses anonymous local seat labels
instead of printing address suffixes. Diagnostics contain counts and timing,
not device names, identifiers or incoming text.

## Platform radio behaviour

ESP-NOW uses broadcast channel 1. Startup checks Wi-Fi mode, channel selection,
ESP-NOW initialization, receive callback registration and broadcast peer
registration; a failed step unwinds initialized resources. IDF 5 receive
callbacks supply RSSI when available. The IDF 4 callback used by classic CYD
lacks RSSI, so it explicitly reports `rssiAvailable=false` and `-127`, rather
than inventing a measurement. Receive callbacks run on the Wi-Fi task, copy at
most 250 bytes, and perform no synthesis, logging or heap allocation. Oversize
frames are refused rather than truncated. `nowSend()` success means SDK
admission, not receiver delivery.

`nowStatus()` exposes received, queued, dropped, invalid and send counts, queue
occupancy/high water, readiness, RSSI availability and the last startup/send
error. COVEN's periodic diagnostic line also reports invalid/replayed protocol
packets, latest phase error and dropped sound commands.

Wi-Fi observation explicitly disables automatic reconnection and association.
The scan remains **active** and may transmit probe requests. It is distinct
from `snifferBegin()` receive-only promiscuous monitoring. BLE observation also
uses active scanning and may send scan requests. Neither observation mode
provides calibrated distance or spatial position from RSSI.

The Wi-Fi cache is bounded at 64 rows and is released on radio teardown.
SPECTRA drops its previous band result vectors before switching stacks. The
existing classic-CYD BLE controller startup and 70 KiB/16 KiB host memory
safeguards remain intact; SDK scan results are cleared between windows to
avoid accumulation. BLE mode changes release the previous radio before mutex
allocation. A failed Arduino 3.3.9 startup is detected before scanner access;
teardown checks residual Bluedroid/controller status only where Bluedroid is
enabled because the library's `deinit()` returns early when initialization
failed partway. The same guarded
cleanup tolerates already stopped SDK components. The pinned Cardputer SDK
selects NimBLE, while classic CYD selects Bluedroid. NimBLE scanning remains
enabled through the Arduino BLE API; `BLEDevice::deinit(false)` owns both its
host-task and controller teardown ordering, with no manual controller fallback
that could stop a still-active host. Bluedroid-only headers and calls are
excluded on NimBLE builds. Arduino's NimBLE startup
waits internally for host synchronization; low-heap and failed-sync behaviour
remain physical owner tests, not a claimed bounded SDK initialization path.
Radio diagnostics use the
HAL's PCM-aware logging path, including when diagnostics and PCM build flags
are combined. SDK-internal logging is outside that application logging path.
Heap fragmentation, injected SDK allocation failure, and coexistence cost still
require physical measurement with the final firmware.

## Touch interaction

| Instrument | Logical 240×135 region | Action |
| --- | --- | --- |
| SPECTRA | Waterfall x4–124, y22–118 | Select nearest active musical voice |
| SPECTRA | Band row x128–239, y22–34 | Toggle Wi-Fi/BLE observation |
| SPECTRA | Mode row x128–239, y78–91 | Cycle dorian, lydian, pent, phrygian |
| SPECTRA | Air slider x168–238, y92–106 | Continuous normalized air control |
| COVEN | Ring centred (62,64), radius 12–48 | Drag local degree around thirteen positions |
| COVEN | Centre radius <12 | Call the circle, subject to event rate limit |
| COVEN | Panel x128–239, y36–46 | Cycle mode when leader |
| COVEN | Panel x128–239, y47–61 | Increment root when leader |

The common CYD dock also exposes explicit band/mode/hold/octave and
note/call/hush/root controls. COVEN retains keyboard piano degrees and adds
Fn+Left/Fn+Right root control for the elected leader. Followers keep local note
and octave control while shared root/mode follow the circle. Their panel says
`following: lock`, and the touch footer explicitly labels root/mode locked.

## Reproducible qualification

From the repository root, with a C++17 compiler and Python 3:

```sh
python tests/run_radio.py --output-dir /tmp/diviner-radio --audio
python tests/run_radio.py --sanitizer address,undefined --output-dir /tmp/diviner-radio-asan
python tests/run_radio.py --sanitizer thread --output-dir /tmp/diviner-radio-tsan
```

The runner records the exact local source SHA, dirty paths, compiler, commands
and per-case status. `--output path.json` optionally writes the deterministic
model result at a specific path. Native application scripts may use
`GRIM_COVEN_NODES=1..11` (including the local instrument); `1` guarantees solo
operation, and `GRIM_FAKE_COVEN=1` remains an upstream-compatible one-peer
shortcut. Virtual participants execute the real CV1 session implementation and
never open network sockets. All preview identities and text are synthetic.

The shared-controller test checks packet validation, single-source election,
replay/order handling, call limiting, expiry, bounded admission, text cleaning,
queue overflow, RSSI mapping and actual observation freshness. An independent
elapsed-time oracle spans both wire beat and millisecond counter wrap. Expected
leaders in the multi-node simulation are computed from the test topology and
active population, not from the controller's own election result.

The six deterministic scenarios produced these development results:

| Model | Participants | Steady phase median / p95 / max | Recovery |
| --- | ---: | ---: | --- |
| Clean | 2 | 0 / 0 / 0 ms | — |
| Clean | 3 | 0 / 0 / 0 ms | — |
| 20% random loss, ±300 ppm clocks, 1–80 ms delay, reordered duplicates | 11 | 44 / 57 / 66 ms | — |
| Partition and reunion, loss/delay/drift | 6 | 46 / 58 / 62 ms | 1,080 ms after reunion |
| Leader leaves, another joins, original returns | 3 | 33 / 41 / 44 ms | 14,050 ms after departure |
| Bursty unrelated traffic saturating the 15-packet queue | 3 | 41 / 55 / 61 ms | 3,615 deliberate queue drops |

Recovery depends on the time since the last accepted beacon: the 14,050 ms
result is consistent with a 15-second liveness lease. Steady timing statistics
exclude the specified acquisition/recovery windows, and are measured relative
to the active leader. They do not measure hardware end-to-end event latency.
The model's regression bound derives from its ≤80 ms one-way delay and 10 ms
scheduler assumptions; it is not an embedded performance target.

`radio_instrument_test.cpp` compiles the **actual COVEN and SPECTRA source** with
only graphics, time and radio seams replaced. It checks two state-allocation
failures, autonomous audio after radio startup failure, touch/key state changes,
stronger-population replacement, stale cached observations, hold/departure,
leader/follower control, bounded command overflow, 10,000 concurrent UI/render
iterations, 300 lifecycle repetitions and zero observed callback allocations.
It detaches/destroys state while the render worker is still live. Six finite
24 kHz mono WAV specimens exercise eight-voice Wi-Fi/BLE populations, replacement
and departure, a local leader and a follower speaking a synthetic DYAD line.

Development runs passed the portable/model and actual-instrument tests under
normal compilation, ASan+UBSan, and ThreadSanitizer. LeakSanitizer could not run
under the workspace's ptrace environment; the ASan+UBSan pass used
`ASAN_OPTIONS=detect_leaks=0`. This is **not a leak-check pass**. TSan evidence is
limited to these actual instrument callbacks and the mocked UI/radio boundary;
it does not certify the complete application, ESP32 SDK or physical radio tasks.
Final qualification should use the retained CI/source-specific results.

The native test ABI measured COVEN state at about 19 KiB and SPECTRA at about
12 KiB, excluding the SDK, receive queue, scan vectors and display buffers.
Exact `sizeof` values, callback medians/p95/p99/max and signal RMS/peak appear
in the test logs. The 128-sample callback deadline at 24 kHz is
`128 / 24000 = 5.333333 ms`. Native callback utilization divides host execution
by that deadline and does **not** establish ESP32 real-time margin.

## Owner physical evaluation

Flash only the separately authorized exact candidate images using the supplied
platform instructions. Capture source/profile identity and radio diagnostics
alongside the board model, power arrangement, radio layout and selected instrument.

| Combination | Candidate status | First checks |
| --- | --- | --- |
| CYD + CYD | NOT RUN physically | Channel 1 discovery, same root/mode, independent notes, calls, leader removal/rejoin |
| CYD + Cardputer ADV | NOT RUN physically | Same checks plus IDF 4/5 interoperability and explicit missing CYD RSSI |
| CYD + ESP32-S3 auxiliary | NOT RUN; auxiliary firmware not supplied | Implement documented ten-byte CV1 participant before claiming interoperability |
| CYD + ESP32-C3 auxiliary | NOT RUN; auxiliary firmware not supplied | Same protocol participant prerequisite; establish heap/radio timing independently |
| Larger bounded circle | NOT RUN physically | Up to eleven total members, deliberate twelve-member refusal, partitions/reunion |

Measure steady and recovery event offsets with one common external clock where
possible. Distinguish UI pulse alignment, sample playback timing and acoustic
arrival. Test sustained speech/calls with animated presentation while recording
audio callback deadlines, queue high water/drops, free heap and largest free
block. Repeatedly alternate Wi-Fi, BLE, COVEN and non-radio rites; inspect heap
recovery and failed startup/retry paths. Listen for discontinuities independently
of callback timing. A zero queue-drop count or successful `esp_now_send()` does
not prove absence of RF packet loss or audio underruns.

## Downstream portability notes

The parser/session, queue and deterministic sonification mappings depend only on
C++ standard integer/memory facilities. The session used 376 bytes on the tested
native ABI and allocates nothing; target ABI size must be checked. Keep the
session on one control thread and serialize the receive ring at the transport
boundary. A consumer can use CV1 without adopting Diviner graphics or DSP.

The musical coordination layer works with autonomous local synths and is useful
for CYD VIBEWARE clusters, FM-1 companions and S3/C3 nodes. It is not a sample
transport or an implementation of a shared DSP graph. The formant voice remains
owned by `vox.h`; COVEN demonstrates safe bounded speech submission and a finite
local audio engine at the existing 24 kHz rate. Reuse the accompanying numerical
and concurrency tests when changing sample rate, hardware or host plugin APIs.
Retain the repository's MIT licence, original notices and upstream attribution.
No related repository is modified by this mission.
