# Physical wireless characterization — DRAFT / HOLD

Both owned boards executed direct ESP-NOW, S3-hosted SoftAP and phone-hotspot
traffic using the approved `174a796` applications. The latest finite cohorts
provide all 22 planned workload types with at least one valid traffic or idle
window. Two phone-AP TCP cells retain only two valid repetitions out of three.
This is measured performance of the current profiler and software stack, not
the radio's ultimate latency or capacity.

The [overview CSV](WIRELESS-OVERVIEW.csv) and [selection JSON](WIRELESS-OVERVIEW.json)
select the latest attempted cohort for each workload without pooling boot or
host-sequencing contexts. Full attempts remain in the
[initial matrix](campaign-link-wifi-02/LINK-REPORT.md),
[sequence-corrected retry](campaign-link-wifi-retry/LINK-REPORT.md), and
[fresh phone-AP cohort](campaign-link-fresh-ap/LINK-REPORT.md). All three capture
statuses are FAILED, even though they contain independently completed windows.
The [raw supplement archive](PHYSICAL-CAPTURES-SUPPLEMENT.zip) also retains the
zero-window USB-open attempt and BLE failure. Its SHA256 is
`15dcd4b987177ca3c8c7e504a1e9a41eef1f342414519519ce9610b3100cf8a7`.

## Observed application RTT

These are 64-byte payload echoes. Each cell pools only its matching three
repetitions, and counts only valid replies received within the bounded tail.
The timing includes packet creation, software queues, peer CRC/echo and local
CRC; it is not an RF propagation measurement. Histogram percentiles are upper
bounds, capped at the exact observed maximum.

| Topology | Local workload | Replies | Mean µs | Minimum µs | p99 upper bound µs | Maximum µs |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| ESP-NOW | Menu | 1,434 | 18,338 | 4,980 | 65,536 | 93,019 |
| ESP-NOW | Fullscreen Gate + drone | 1,432 | 12,273 | 5,063 | 65,536 | 86,024 |
| S3 SoftAP | Menu | 2,709 | 50,949 | 7,856 | 131,072 | 158,122 |
| S3 SoftAP | Fullscreen Gate + drone | 2,685 | 39,388 | 6,981 | 119,563 | 119,563 |
| Phone AP | Menu | 2,044 | 100,989 | 10,262 | 524,288 | 568,765 |
| Phone AP | Fullscreen Gate + drone | 2,377 | 53,978 | 11,568 | 262,144 | 326,464 |

The heavier local workload sometimes has a lower RTT in this sequence. These
observations do not establish that GUI load causes the delay or that removing
it would recover a specific number of microseconds. The existing link worker
uses priority 1 on core 1 and a one-tick polling delay. Those are candidates for
controlled follow-up; no queue/callback timestamp split isolates their cost.
The audio MVP is a separate implementation with broadcast, timer-driven pacing
and bounded playback; its results must not be substituted into this baseline.

## Observed payload delivery

Numbers below are unique application payload bytes per target second, excluding
the 40-byte protocol header, Wi-Fi overhead and failed API admissions. Ranges
are across the three repetitions, not a percentile estimate from three values.
ESP-NOW paced payload is 200 bytes; UDP/TCP payload is 1,024 bytes. They are
different message shapes and offered loads.

| Topology / mode | Menu median [min, max] B/s | Fullscreen Gate + drone median [min, max] B/s |
| --- | ---: | ---: |
| ESP-NOW paced | 15,660 [12,880, 17,100] | 25,860 [25,200, 26,000] |
| S3 SoftAP UDP | 151,142 [137,728, 157,286] | 138,240 [132,301, 141,824] |
| Phone AP UDP | 99,635 [90,419, 105,677] | 131,994 [130,150, 139,059] |
| S3 SoftAP TCP | 55,603 [39,322, 58,163] | 79,053 [71,680, 80,998] |
| Phone AP TCP, 2/3 valid | 35,584 [34,202, 36,966] | 56,627 [49,254, 64,000] |

The two-valid-repetition rows average their two middle values; they are not a
three-repetition estimate. In the fresh phone-AP UDP fullscreen cohort,
5.89–6.66% of API-accepted packets were not observed as unique peer packets.
This includes finite-tail, software queue and sequence-horizon effects and is
not isolated RF loss. There were also 229–307 sender API failures per window.

24 kHz stereo PCM16 requires 96,000 payload B/s before transport headers.
48 kHz stereo requires 192,000 B/s. These calculations explain why average
goodput alone is insufficient: some measured UDP means exceed the first rate,
but loss, deadline misses and burst delay remain uncharacterized for an audio
playout policy. Eight channels at 48 kHz PCM16 require 768,000 B/s before any
overhead, well beyond these observed payload rates.

Fullscreen idle Gate + drone was about 22.2 FPS. Matching traffic cohorts
observed roughly 17.5–19.0 FPS with RTT/UDP/TCP, depending on transport. Driver
queue-full and DMA-fault notifications were zero in these finite successful
windows. This does not establish uninterrupted analog output; enqueue blocking,
callback tails and individual counters remain in the reports.

## Failures constrain the interpretation

The initial harness encountered sticky completion state before TCP connection
setup and 6 KiB diagnostic allocation failures after large graphics/DSP scenes.
The corrected host sequence prepares connections in the menu, runs the actual
scene, then frees scene state before exporting diagnostics. See the
[sequence audit](LINK-HOST-SEQUENCE-AUDIT.md). This changes outside-window
measurement setup, not the approved firmware's timed kernels.

The final fresh phone-AP cohort still had one zero-traffic menu TCP run. Its
third fullscreen TCP run contains a malformed CYD sample line: a byte segment
inside the temperature/CPU fields is missing. The raw bytes are retained and
that run is excluded; it is a telemetry failure, not a measured radio loss.
There is no reconstructed or invented replacement reading.

Directed BLE traffic failed before the first arm acknowledgement with a
symbolicated null write in SDK Bluedroid initialization. Later mode-required
rejections followed that reboot and are not independent BLE capacity tests.
[BLE startup evidence](BLE-STARTUP-FAILURE.md) preserves the exact ELF/source
chain. Controller initialization alone did complete in six separate windows;
usable GATT latency, goodput and connection parameters remain NOT MEASURED.

## Consequences for heterogeneous offload

The measured S3 kernel median savings over this CYD are only 17–912 µs for the
tested cases. Even the best observed baseline application RTT is several
thousand microseconds. None of these measurements demonstrates a successful
deadline-critical DSP offload. Keep DAC scheduling, immediate control response
and short stateful feedback local. Longer batched analysis, asynchronous
precomputation, independent voices with advance scheduling and remote display
assets are candidates for a separate matched-input/output job RPC experiment.

For an actual job, compare local compute with remote compute plus packing,
queueing and measured matched-message transport. A full payload RTT already
contains its transfer time; do not add the same bytes divided by goodput again.
The current echo is not an arbitrary job RPC and gives no hard deadline bound.
