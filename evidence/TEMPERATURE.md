# Retained temperature indications

The [per-record CSV](TEMPERATURE-SAMPLES.csv) preserves readings from valid
original profiler/kernel/wireless workload windows, including valid local profiler windows in failed
wireless captures. The [per-campaign ranges](TEMPERATURE-SUMMARY.csv) retain
source/image identities through the detailed CSV and never replace a missing
reading with zero. Replay with:

```powershell
python tools/perf/temperature_overview.py evidence/physical evidence
```

Extract the baseline and supplementary raw capture archives into `evidence/`
first for those CSVs. The extractor excludes
unfinished or malformed windows. It includes separately valid local profile
windows even if their peer traffic failed. Snapshot readings around kernels
are distinct from 1 Hz steady-window observations.

| CYD stress capture | Duration | Retained 1 Hz samples / span | Weighted core 0 / 1 busy | Raw codes |
| --- | ---: | ---: | ---: | ---: |
| Original continuation | 365 s | 355 / 362.939 s | 98.9947% / 99.9602% | 128 throughout |
| I2S-event supplement | 365 s | 356 / 363.605 s | 98.9871% / 99.9515% | 139–146; first 139, last 142 |

These are non-idle RTOS tick observations, not cycle-exact arithmetic
utilization. Both exceed the requested 360 seconds of near-full observed CPU
occupancy. Neither stress window reset or dropped retained samples.

Across the closed original workload captures, valid classic-CYD indications
ranged from raw 128 to 151. Later wireless cohorts contained raw 144–165; the
highest retained raw 165 corresponds to **73.889 °C only under the legacy
arithmetic conversion** `(raw - 32) * 5 / 9`. That conversion is not calibration.
The fresh phone-AP cohort ranged 147–156. Changing workloads, radio history,
elapsed time and boot context prevent attributing these differences to one
cause. The first constant-code stress result must not be presented as evidence
that the board stayed at a constant physical temperature.

The classic reading comes from an undocumented RTC-library routine. Older raw
records retain a legacy ROM-method label; the actual linked implementation and
interpretation are documented in [method notes](method-notes.md). Accurate die
temperature, ambient temperature, thermal rise, thermal margin and throttling
thresholds are **not established** by this sensor experiment.

The completed S3 kernel matrix retained 265 state indications ranging
**37.1–42.1 °C** from its Arduino/IDF temperature driver. It uses a different
method, has no independent accuracy verification, and is not a 360-second S3
stress test. Do not compare these numbers directly with the CYD's converted
raw code as a calibrated cross-chip thermal result.

## PCM prototype: outside-window endpoint readings

The separate [PCM record CSV](pcm-temperature/PCM-TEMPERATURE-SAMPLES.csv),
[per-endpoint ranges/counts](pcm-temperature/PCM-TEMPERATURE-SUMMARY.csv) and
[manifest](pcm-temperature/PCM-TEMPERATURE-MANIFEST.json) retain **100 readings**
from validated runs in four closed PCM cohorts. These are queries **after
finite streaming export and radio stop**, not 1 Hz in-window samples or a
sustained-load stress test. They are kept separate from the older profiler CSVs.
The [PCM findings](PCM-FINDINGS.md) preserve the different live and deferred
metadata policies and all failed attempts.

| Closed PCM cohort | Physical endpoint | Retained post-stop readings | Indication range |
| --- | --- | ---: | --- |
| CYD deferred matrix | Classic CYD, COM21 | 2 | Raw 128 throughout |
| CYD deferred matrix | S3, COM22 | 2 | Driver 47.1–51.1 °C |
| Fresh CYD deferred single | Classic CYD, COM21 | 1 | Raw 128 |
| Fresh CYD deferred single | S3, COM22 | 1 | Driver 46.1 °C |
| S3 pair, live metadata | Second S3, COM23 (logical `cyd`) | 22 | Driver 43.0–61.0 °C |
| S3 pair, live metadata | S3, COM22 (`peer`) | 22 | Driver 54.1–68.1 °C |
| S3 pair, deferred metadata | Second S3, COM23 (logical `cyd`) | 25 | Driver 47.0–55.0 °C |
| S3 pair, deferred metadata | S3, COM22 (`peer`) | 25 | Driver 52.1–68.1 °C |

The three classic values use the same undocumented RTC-library raw method and
do not establish physical constancy or a valid failure sentinel. The other 97
values use the Arduino/IDF Celsius-indication driver, explicitly reported as
externally unverified. Neither board-to-board range differences nor live versus
deferred ranges isolate thermal response: boot history, radio settings, elapsed
time and selection of successful runs differ. Active power-save readback is
unknown in the deferred cohorts; no thermal A/B is inferred. These readings do
not alter either 365-second classic-CYD stress result above.

Reproduce only from explicitly selected terminal captures:

```powershell
python tools/perf/pcm_temperature.py `
  evidence/physical/audio-bidirectional-deferred-01/results.json `
  evidence/physical/audio-deferred-fresh-01/results.json `
  evidence/physical/audio-s3-pair-02/results.json `
  evidence/physical/audio-s3-pair-deferred-01/results.json `
  --output evidence/pcm-temperature
```

The extractor validates the PCM runs, preserves endpoint source/image identity
and input hashes, and rejects unfinished captures. Missing observations are
never filled with zero. Later SDR-metal observations use the separate scope
below.

## Closed fast SDR campaign: outside-window S3 readings

[The ledger CSV](metal-fast-temperatures.csv),
[structured summary](metal-fast-temperatures.json) and
[hash manifest](metal-fast-temperatures-manifest.json) retain **576** readings
from five closed `e8b4c8a` parent captures: 78 finite TX windows and 18
receiver-only reference windows. Each before/after set counts the shared TX
once and each independent receiver once. Every sensor installation, enable,
read, disable and uninstall status is zero. These readings are explicitly
outside the timed RF/snapshot loops. The nine initial preflight readings and
later restored-audio readings are not pooled into the ledger.

| Closed scope | Readings per endpoint | TX driver °C range | COM22 RX driver °C range | COM23 RX driver °C range |
| --- | ---: | ---: | ---: | ---: |
| Smoke, 3 windows | 6 | 36.9–56.9 | 44.1–47.1 | 37.0–41.0 |
| Rate sweep, 42 windows | 84 | 39.9–95.9 | 55.1–61.1 | 48.0–53.0 |
| Framed replay, 21 windows | 42 | 74.9–98.9 | 61.1–62.1 | 53.0–54.0 |
| ASK/masked compatibility, 12 windows | 24 | 59.9–96.9 | 63.1–64.1 | 53.0–54.0 |
| Receiver-only reference, 18 windows | 36 | 47.9–50.9 | 64.1–65.1 | 54.0–54.0 |

The **98.9 °C TX indication is retained**, including its successful driver
status. The frozen transmitter requests a **10–80 °C** sensor range; values
above that requested range have no independently established accuracy. No
external die/ambient measurement was made. The ranges do not establish a
calibrated thermal margin or a thermal cause for any decoding outcome.

These are separate one-second payload bursts with setup, deferred readout and
idle intervals, plus receiver-only acquisition. They are not another
360-second continuous CPU stress experiment. Both earlier **365-second
classic-CYD near-full-CPU tests** and their raw RTC sensor observations remain
reported above without conversion to calibrated Celsius. Native TX/RX clocks
are independent; RX query timestamps are target-local and the TX sensor record
does not report a target query timestamp.
