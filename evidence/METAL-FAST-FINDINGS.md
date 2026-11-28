# Bare S3 fast SDR findings — finite physical evidence

The new paced backend retained source-word order and ran at approximately **8.832 million writes/s** in the highest framed setting. One of its three requested one-second windows completed **8,831,950 words**, leaving **50 target words unsent** (about5.7ppm); the other two completed8,832,000 words. This finite-end tail is not perfect clock delivery or an RF-loss measurement. In the alternating-pattern sweep, requests of12–240 million/s instead plateaued near **9.881 million writes/s**. These are CPU-observed stores, not wireless payload goodput or an ultimate PHY limit. Both receivers showed the expected power-envelope periodicity through the tested8 million/s alternating setting. The frozen decoder recovered known-source frames at **4.416 Msymbol/s**, with nonzero conditional payload errors; at8.832 Msymbol/s it recovered no headers in three snapshots on either receiver.

All five cohorts are closed **CAPTURE COMPLETE**:3 smoke,42 rate,21 framed,12 compatibility and18 receiver-only windows. That is **96 shared windows**, including **78 finite TX windows**, observed separately by two receivers. There are **192 retained16,380-word buffers** (3,144,960 native words). Firmware reports534 capture completions because each no-export bench repeats20 times; only its last buffer is retained. The unretained buffers are not analyzed as additional waveforms.

[Structured JSON](METAL-FAST-FINDINGS.json), [per-receiver/window CSV](METAL-FAST-FINDINGS.csv) and [hash manifest](METAL-FAST-FINDINGS-MANIFEST.json) preserve counts and scope. TX fields repeat across the two receiver rows in the CSV; count shared TX exposures once. Blank TX proof in receiver-only rows is inapplicable.

## Exact system and source proof

All three headless S3s use source `e8b4c8a9f5073db86706f9cebbaefb0476549ef6` and ESP-IDF5.5.5 `b774170ff46c393eeb5e495ea37936038d3f4f4f`, CPU240MHz. TX is COM13/MAC `1c2904becc48`, image SHA256 `afe172501b293606c1b5f032d6b27c338d9bc2a19e8b881cab67abcff70fc567`. Receivers COM22/COM23 are MACs `1c2904bec3a0`/`1c2904bec3d8`, image `0aaeb8660fab2877d6735c2ae06445c04ac40d0337ba7b8a63377278894016c4`. These are separate from Diviner’s combined graphics/audio and packet-PCM images.

The [frozen review](metal-fast-image-review.json), [verified app-only flash receipt](metal-fast-flash-receipt.json) and [image/source bundle receipt](metal-fast-bundle-receipt.json) pin the applications. The original raw/generic loop bodies remain unchanged; the new105-byte fast loop is in IRAM with no function call or division. It uses precomputed internal-DRAM cycle deltas, a ring-boundary fractional carry and IRQ-enabled operation. Ring starts equal the rational-clock floor; intra-ring rounding is at most one cycle early relative to that floor, with no accumulating rounding drift. Per-word lateness and maximum transient backlog are intentionally unobserved.

**All78 TX windows passed the checked serial/source contract**, including56 fast-clock receipts. Uploaded packed CRC, independently reconstructed intended LE32 word CRC and applicable rational-delta CRC agree. Checks span startup, tone initialization, pre/post-loop and teardown; arrays are populated after SDK startup/tone preparation. All checked output-failure, format, write-error and flush counter deltas were zero. This detects no boundary source-memory change or observed output failure; CRC collisions and transient changes between checks remain possible. SDK stop calls/register readback are not independent RF-cessation proof.

Both receivers initialized bare/direct PHY, invoked the receive-enable hook, and verified gain40 at nominal FREQ2433/FOFS−80. No Wi-Fi NULL fallback was needed. Native IQ stays I10,−Q10 with upper metadata retained; envelope analysis uses those original values. Clocks and RF controls are configured values, not independent frequency/power calibration.

## Software rate and received envelope

The table reports medians of three windows per setting. Envelope peaks are independent Hann-window AC-power FFT peaks from one finite native snapshot per receiver/window. Their40/80MS/s clock is nominal; bin spacing is2.442/4.884kHz. They are periodicity observations, not decoded bit-rate or qualified RF-edge jitter.

| TX mode / requested million/s | Median software million writes/s | RX1 / RX2 power-peak MHz | Source-slot policy |
| --- | ---: | --- | --- |
| gate / 1 | 0.994931 | 0.500611 / 0.500611 | Generic paced skips obsolete slots |
| gate / 2.208 | 0.867117 | 0.239316 / 0.239316 | Generic paced skips obsolete slots |
| gate / 0 (unpaced) | 11.366435 | 5.719170 / 5.719170 | Different minimal unpaced loop |
| fast / 1 | 1.000000 | 0.500611 / 0.500611 | Next source word retained; unsent tail reported |
| fast / 2.208 | 2.208000 | 1.103785 / 1.103785 | Next source word retained; unsent tail reported |
| fast / 4.416 | 4.416000 | 2.207570 / 2.207570 | Next source word retained; unsent tail reported |
| fast / 8 | 8.000000 | 4.000000 / 4.000000 | Next source word retained; unsent tail reported |
| fast / 12 | 9.881345 | 4.971917 / 4.971917 | Next source word retained; unsent tail reported |
| fast / 240 | 9.881350 | 4.971917 / 4.971917 | Next source word retained; unsent tail reported |

The intermediate16/24/32/64/120M requests also plateaued: all21 high-request fast windows span9.881301–9.881372 million writes/s. This is the observed ceiling of this loop/context, not a hardware ceiling. The unpaced loop reaches11.366414–11.366484 million writes/s with less per-word work. Generic gate at requested2.208M produced only0.867108–0.867119 million writes/s and skipped obsolete source slots; its0.239316MHz envelope subharmonic shows why half the mean store rate cannot always be called its modulation clock.

At overloaded/unpaced settings the local envelope peak differs from half the whole-second average store rate by roughly0.6%. Local cadence versus full-loop interrupt activity is a possible explanation, not an isolated causal measurement or clock calibration. At higher rates the default robust two-level edge gate usually rejects the waveform: RX1 qualifies0/42 rate snapshots; RX2 qualifies13/42. The report therefore makes no high-rate threshold-duty or RF-jitter claim. Rejection is not proof of absent RF.

## Known-source frames and imperfect reconstruction

Framed results use the unchanged decoder core SHA256 `09c500232d4046ac6021dfcb8e83c998ff96846296abcc583ef62ace176b8f28`. The wrapper shifts native signed10-bit fields right2 into its frozen signed8-bit interface; it does not flip Q, remove the mean or resample. This decoder/quantization policy is part of the result. Raw native words remain available for different future analysis.

Each cell is matching-header windows out of3, then **errors / compared payload bits**. Counts are conditional on decoded, source-matched sequences; no compared payload is reported as a dash. Missing headers and unsampled source time are not counted correct or converted into an RF-loss percentage.

| Source Hz × channels / OSR / payload bits | Requested Msymbol/s | RX1: headers; errors/bits | RX2: headers; errors/bits |
| --- | ---: | --- | --- |
| 24000×2 / 4 / 128 | 0.36 | 1/3; 0/128 | 2/3; 0/256 |
| 48000×2 / 4 / 256 | 0.552 | 2/3; 0/512 | 2/3; 4/512 |
| 48000×2 / 8 / 256 | 1.104 | 3/3; 7/1536 | 3/3; 6/1280 |
| 48000×4 / 8 / 256 | 2.208 | 3/3; 18/1024 | 3/3; 21/1280 |
| 24000×2 / 32 / 256 | 2.208 | 3/3; 15/1536 | 3/3; 110/1280 |
| 48000×4 / 16 / 256 | 4.416 | 3/3; 53/3072 | 3/3; 41/2816 |
| 48000×4 / 32 / 256 | 8.832 | 0/3; — | 0/3; — |

At4.416M (48kHz×4, OSR16), conditional BER is53/3072=**1.725%** on RX1 and41/2816=**1.456%** on RX2. These are12 and11 unique known frames summed over the snapshots, only48/44 reconstructed source samples per channel. At2.208M, 48kHz×4/OSR8 has18/1024 and21/1280 errors;24kHz stereo/OSR32 has15/1536 and110/1280. Codec and link/host-demodulation error remain distinct in the JSON’s received-domain quality arrays. Energy/count pooling is explicit; correlations are not pooled without cross moments.

At8.832M (48kHz×4, OSR32), the source write/CRC contract passes but no matching headers are recovered in3/3 snapshots on either receiver. This is a failure of the tested complete source/receiver/decoder context, not proof of an ultimate RF bandwidth limit. The lower360k/552k cases also have no-header snapshots; their roughly667µs frames occupy much of the1,023.75µs aperture, so unknown boundary phase can exclude a complete frame. No whole-run frame-delivery denominator exists for these sparse apertures.

Framing is original experimental NRZ/TDM-PDM with training/sync/header CRC and no payload CRC/FEC/retransmission. Header-CRC search rejects are overlapping candidates, not independent packet-loss events. A368-symbol frame at2.208/4.416/8.832M nominally occupies166.667/83.333/41.667µs. That arithmetic serialization is **not measured one-way audio latency**. There is no live ADC, continuous on-device demodulator/playout, DAC/I2S output or synchronized TX/RX clock. Short received-domain PCM comparisons do not establish audible fidelity or continuous48kHz multichannel delivery.

## Receiver aperture, software occupancy and memory

Each receiver has a checked64KiB dump aperture and retains16,380 words, giving nominal1,023.75/409.5/204.75µs at16/40/80MS/s. Every retained capture passed CRC/length, hardware-done, canary and register-restoration checks. No USB or allocation occurs during the timed snapshot. The receiver-only reference separates repeated no-export bench loops from single exported captures.

| Nominal MS/s | No-export words / software-loop second, RX1 / RX2 (million) | Nominal aperture / bench-loop occupancy, RX1 / RX2 | Exported loop median ms, RX1 / RX2 |
| --- | --- | --- | --- |
| 16 | 7.406 / 7.405 | 46.29% / 46.28% | 264.720 / 80.479 |
| 40 | 10.266 / 10.266 | 25.67% / 25.67% | 264.676 / 79.546 |
| 80 | 11.789 / 11.788 | 14.74% / 14.74% | 263.649 / 79.477 |

These are software occupancy/turnaround measurements with finite gaps, not continuous RF acquisition rates. No-export bench includes sentinel/canary/owner/check/setup work; exported loops also include CRC/format/USB and host drain. The large endpoint export difference does not isolate a board-brand, ADC or CPU advantage. Only each bench’s last waveform is retained.

Both new bare receivers report257,336 free internal8-bit bytes and188,416 largest-block bytes before snapshots. The older Wi-Fi NULL cohort reported216,016/172,032. The differences (+41,320/+16,384) describe configured systems with different image/init/observer/temperature histories; they are not a controlled estimate of Wi-Fi-driver overhead alone. This headless experiment does not replace Diviner’s combined-load budgets.

## Compatibility, temperature and final state

The12 compatibility windows completed. ASK100k and ASK1M unmasked medians were99,994.990 and994,932.942 writes/s. Requested2,000µs masked Gate/ASK windows each retained498 segment records, with **2,988 records total and zero dropped**. Maximum mask brackets were2,013.638µs Gate and2,012.363µs ASK; maximum intersegment gaps were0.183/0.208µs. These cycle brackets include entry/exit and possible IRQ immediately after restore; they do not exclude higher-priority interrupts. The checked serializer retained complete results/segments/terminal proof in this cohort; the old truncated masked capture remains failed. This is no general zero-error USB guarantee.

The [temperature ledger](metal-fast-temperatures.json) ([CSV](metal-fast-temperatures.csv), [manifest](metal-fast-temperatures-manifest.json)) contains **576 before/after driver readings**, counting shared TX once. All invoked sensor lifecycle/read statuses pass. These are outside-window indications, not continuous thermal stress samples or independently calibrated temperatures.

| Phase | Readings per endpoint | TX driver °C | RX1 driver °C | RX2 driver °C |
| --- | ---: | --- | --- | --- |
| smoke | 6 | 36.9–56.9 | 44.1–47.1 | 37.0–41.0 |
| rate | 84 | 39.9–95.9 | 55.1–61.1 | 48.0–53.0 |
| framed | 42 | 74.9–98.9 | 61.1–62.1 | 53.0–54.0 |
| compat | 24 | 59.9–96.9 | 63.1–64.1 | 53.0–54.0 |
| reference | 36 | 47.9–50.9 | 64.1–65.1 | 54.0–54.0 |

TX requests sensor range10–80°C but records values up to98.9°C; readings above that requested range have unverified accuracy. No calibrated thermal margin, throttling behavior or cause for the high-rate decode failure is established. This is not an S3≥360s near-full-CPU stress test. The prior classic365s stress evidence remains separate.

The [final restoration receipt](metal-final-audio-restore-receipt.json) verifies COM22/23 restored to the approved `ad9cbfd…` audio/kernel image; COM13 retains the requested e8b metal TX, idle, with checked cleanup. Ports are closed. No ordinary Diviner renderer/audio optimization was undertaken.

## Reproduction and immutable provenance

- [Raw capture archive](PHYSICAL-CAPTURES-METAL-FAST.zip),13,949,797 bytes, SHA256 `38ab510e0fd0fe1039f5fca01d9c7e7e9f5cc3ba90579f9161aecf8dc4c4b463`; [receipt](metal-fast-archive-receipt.json). It retains all five closed directories and both receiver bodies.
- [Derived analyses](DERIVED-ANALYSES-METAL-FAST.zip),26,822,910 bytes, SHA256 `e65fedfac3532f7935a32daf89ee38118a1afcbbd4bb26833e6b6bf60ed05ef2`; [receipt](derived-analyses-metal-fast-receipt.json) and [extraction guide](DERIVED-ANALYSES-METAL-FAST.md). It preserves22 complete analysis/envelope/demod trees, including spectra/edges and selected-frame PCM.
- [Image/source bundle](UNAPPROVED-SDR-METAL-FAST-IMAGES.zip) and [receipt](metal-fast-bundle-receipt.json), with exact source, GPL receiver provenance, resolved configuration, ELF/maps and the owner-reviewed plans.
- [Native capture method](../tools/perf/SDR-METAL.md), [dual-exposure method](../tools/perf/SDR-METAL-DUAL.md), [envelope method](../tools/perf/SDR-METAL-ENVELOPE.md) and [frozen demodulation method](../tools/perf/SDR-METAL-DEMOD.md).

Extract archives at their original paths before verifying nested manifests. Existing tools independently verify raw hashes/CRC and preserve each source/image/receiver context. The overview uses matching-case count sums, arithmetic medians/ranges of three repetitions, and received-domain squared-error/reference-energy pooling. It adds no hardware measurements or causal attribution. The PR remains Draft/HOLD.
