# Fan-assisted framed repeat — separate closed cohort

With the owner's blower fan and boards moving, RX2 recovered headers at requested **8.832 Msymbol/s** in all three snapshots, with **196 errors in 768 compared payload bits (25.52%)**. RX1 recovered no headers at that setting, so its conditional BER remains unknown. This changed observation does **not** establish a cooling effect or a reliable higher-rate audio link.

The new capture is closed **6/6 shared TX windows**, each requested for one second, with 12 retained receiver snapshots. The [earlier findings](METAL-FAST-FINDINGS.md) remain unchanged. Endpoint images/source, uploaded-bit SHA256, complete per-window settings and decoder core match the corresponding six earlier windows. All three endpoints still use source `e8b4c8a9f5073db86706f9cebbaefb0476549ef6`; both receivers use bare/direct PHY and verified manual gain index 40. The JSON retains exact image identities and matching settings.

## Conditional payload comparison

Each cell gives matching-header snapshots out of three, then errors/compared payload bits. Counts use the first known sequence per snapshot and sum separate repetitions. No-header snapshots contribute no compared bits; they are not zero-error observations or an RF packet-loss denominator.

| Requested Msymbol/s / source | Receiver | Earlier matched cohort | Fan/moving-board cohort |
| --- | --- | --- | --- |
| 4.416 / 48 kHz × 4, OSR16 | RX1 | 3/3; 53/3072 (1.73%) | 3/3; 56/2560 (2.19%) |
| 4.416 / 48 kHz × 4, OSR16 | RX2 | 3/3; 41/2816 (1.46%) | 3/3; 64/2816 (2.27%) |
| 8.832 / 48 kHz × 4, OSR32 | RX1 | 0/3; BER unknown | 0/3; BER unknown |
| 8.832 / 48 kHz × 4, OSR32 | RX2 | 0/3; BER unknown | 3/3; 196/768 (25.52%) |

At 4.416M the conditional error ratios are higher in this repeat on both receivers; different selected frame sequences/counts prevent a matched-sample comparison. At 8.832M, RX2's individual snapshots contain 84/256, 16/256 and 96/256 errors. Those are three unique known frames and only **six reconstructed source samples per channel**. This does not establish continuous 48 kHz delivery or audio fidelity. The comparison does not turn the earlier unknown BER into a numerical baseline.

## Source, timing and temperature

All six new source-CRC and checked-output contracts pass, with zero failure, format, write-error or flush counter increases. Each 4.416M window completed 4,416,000 source words and each 8.832M window completed 8,832,000, with **zero unsent target tail**. Cycle-derived average rates span 4,415,999.834–4,415,999.871 and 8,831,999.374–8,831,999.558 writes/s. Signed end clock errors are 7–17 CPU cycles; these are finite software-clock boundaries, not per-word jitter or radio latency. The earlier matching 8.832M set retains its one 50-word unsent tail. Source order and boundary CRC checks do not independently attest over-air delivery or exclude transient changes/CRC collisions.

| Matched six-window cohort | Readings per endpoint | TX driver °C | RX1 driver °C | RX2 driver °C |
| --- | ---: | --- | --- | --- |
| Earlier matched subset | 12 | 98.9–98.9 | 62.1–62.1 | 53.0–54.0 |
| Fan/moving-board repeat | 12 | 29.9–55.9 | 39.1–44.1 | 36.0–41.0 |

The new parent capture contains **36 before/after driver readings**, with successful read/cleanup statuses. Shared TX points are counted once, not twice through receiver children. These are outside-loop indications, not independently calibrated die or ambient temperatures. New TX values lie within the requested 10–80°C sensor range; all 12 readings in the matched earlier TX subset were 98.9°C, outside that requested range and with unverified accuracy. Fan airflow, board/antenna motion, RF geometry, elapsed time, preceding workload and reset/calibration history co-vary. None is isolated as the cause of header recovery or error changes.

The unchanged decoder core is SHA256 `09c500232d4046ac6021dfcb8e83c998ff96846296abcc583ef62ace176b8f28`, consuming the same signed I10-to-I8 right-shift adapter and host/batch workflow. Native 40/80 MS/s snapshots retain only 409.5/204.75 nominal microseconds. Header-CRC search rejects are overlapping candidates, not independent packet losses; payload CRC/FEC/retransmission remain absent. No synchronized clock, live ADC/DAC, continuous on-device demodulation/playout or one-way audio-latency measurement was added.

## Provenance

- [Structured comparison](FAN-FRAMED-REPEAT.json), [per-window/receiver CSV](FAN-FRAMED-REPEAT.csv) and [hash manifest](FAN-FRAMED-REPEAT-manifest.json). TX fields repeat per receiver row; shared-window counts use the parent capture.
- [Raw fan archive](PHYSICAL-CAPTURES-METAL-FAN.zip) and [receipt](metal-fan-archive-receipt.json).
- [Derived fan analyses](DERIVED-ANALYSES-METAL-FAN.zip), [receipt](derived-analyses-metal-fan-receipt.json) and [extraction guide](DERIVED-ANALYSES-METAL-FAN.md). The two original demodulation manifests pin native validation and selected-frame outputs.
- [RX reflash verification](metal-fan-rx-reflash-receipt.json) and [post-repeat restoration](metal-fan-audio-restore-receipt.json).

Only this new comparison is written. No old result, capture, source, plan, identity or analysis changes, and no native/envelope re-analysis is added. The PR remains Draft/HOLD.
