# Retained temperature indications

The [per-record CSV](TEMPERATURE-SAMPLES.csv) preserves readings from valid
physical workload windows, including valid local profiler windows in failed
wireless captures. The [per-campaign ranges](TEMPERATURE-SUMMARY.csv) retain
source/image identities through the detailed CSV and never replace a missing
reading with zero. Replay with:

```powershell
python tools/perf/temperature_overview.py evidence/physical evidence
```

Extract both raw capture archives into `evidence/` first. The extractor excludes
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
