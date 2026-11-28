# Native custom-frame decode observations

The original `metal-dual-framed-wifi-01` capture completed 30/30 independent exposure windows, each observed by two receivers. Both original child statuses remain CAPTURE COMPLETE. The frozen v3 host demodulator ran 60 declared trials per receiver: one requested-baud trial for each paced snapshot and three predeclared software-mean multipliers for each unpaced snapshot. Native body/header/hash/CRC/source checks and uploaded-bit/reference associations passed before output generation. No raw acquisition or flashed image changed.

RX/COM22 produced matching proprietary header/config frames in 18/30 windows (38 selected frames); RX2/COM23 did so in 5/30 windows (9 frames). These are finite snapshots, not sustained streaming or independent packet-loss trials. Each native observation is only 204.75–1023.75 µs at its nominal clock and can crop frame beginnings/endings. A missing complete header is not a measured RF loss fraction.

| Source configuration / transport | RX windows /3 | RX selected frames | RX errors / bits | RX2 windows /3 | RX2 selected frames | RX2 errors / bits |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 24000-2ch-osr4-p128 / 360000 baud | 1/3 | 1 | 0 / 128 | 1/3 | 1 | 2 / 128 |
| 48000-2ch-osr4 / 552000 baud | 3/3 | 3 | 4 / 768 | 1/3 | 1 | 3 / 256 |
| 48000-2ch-osr8 / 1104000 baud | 3/3 | 5 | 6 / 1280 | 3/3 | 7 | 39 / 1792 |
| 48000-4ch-osr8 / 2208000 baud | 0/3 | 0 | NOT MEASURED | 0/3 | 0 | NOT MEASURED |
| 24000-2ch-osr32 / 2208000 baud | 0/3 | 0 | NOT MEASURED | 0/3 | 0 | NOT MEASURED |
| 24000-2ch-osr4-p128 / unpaced | 3/3 | 11 | 347 / 1408 | 0/3 | 0 | NOT MEASURED |
| 48000-2ch-osr4 / unpaced | 3/3 | 6 | 534 / 1536 | 0/3 | 0 | NOT MEASURED |
| 48000-2ch-osr8 / unpaced | 3/3 | 7 | 522 / 1792 | 0/3 | 0 | NOT MEASURED |
| 48000-4ch-osr8 / unpaced | 2/3 | 5 | 425 / 1280 | 0/3 | 0 | NOT MEASURED |
| 24000-2ch-osr32 / unpaced | 0/3 | 0 | NOT MEASURED | 0/3 | 0 | NOT MEASURED |

Known-source BER is measured against each decoded header sequence in the uploaded 32-frame waveform ring. Per-window quality uses the first occurrence of a sequence; all-known-frame and unique-sequence totals remain separately available. Payloads deliberately have no CRC and errors remain accepted/countable. Headers/configuration must pass the existing core checks; header-reject counters count overlapping search candidates, not independent RF packets.

The paced 552k and 1.104M cases decode all three RX windows with some exact payloads and some bit errors. These small received domains establish actual custom-header/payload observations and digital reconstruction. They do not establish continuous live DAC output, analog fidelity or a sustainable 48 kHz multichannel link. The 2.208M paced variants yield no matching headers in this cohort, so payload BER and reconstruction quality remain unknown rather than zero.

Unpaced RX receives valid matching headers for four source configurations, but measured payload errors are substantial: about 25–35% over the selected known-bit domains. Merely finding a valid custom header does not imply a useful clean audio stream. The last 32×OSR variant has no matching headers. The multiplier search was fixed before these comparisons: software mean times 1, 1.004, 1.008. Every trial remains inspectable, and selection uses matching-header/config count, then training-fit error; payload BER, confidence and PCM quality do not choose the clock.

Unpaced effective audio rates must not be read from the source header: recovered-clock medians are approximately 765 kHz for the 24 kHz/2ch/OSR4/P128 source, 998 kHz for 48 kHz/2ch/OSR4, 498 kHz for 48 kHz/2ch/OSR8 and 249 kHz for 48 kHz/4ch/OSR8. These describe rapid replay of a finite deterministic waveform ring, using the nominal RX clock, not a new source-generation or calibrated playback rate. Exact per-frame recovered samples/symbol and effective rates remain in CSV/JSON.

The native conversion is deterministic signed I10/−Q10 arithmetic shift 2 into signed I8/−Q8. It does not flip Q, remove DC, resample, stitch bursts or insert erasure zeros. All native and converted hashes are retained. Received-domain source/ideal-boxcar PCM error statistics are per channel, without lag fitting or concealment. Raw PCM is digital concatenation only; no WAV, live DAC, analog output or synchronized one-way latency claim is made. A later parent-led position-swap cohort is separate; do not infer a board-brand cause from these two receiver locations.

Reproduce with `tools/perf/sdr_metal_demod.py` and [its documented fixed-trial policy](../tools/perf/SDR-METAL-DEMOD.md). Inputs and complete logs: [RX results](metal-framed-wifi-rx-demod/results.json), [RX2 results](metal-framed-wifi-rx2-demod/results.json). Every run retains all trials, core stdout/stderr, frame and PCM files, source oracles and output hashes. Summary: [JSON](METAL-FRAMED-WIFI-FINDINGS.json), [CSV](METAL-FRAMED-WIFI-FINDINGS.csv).
