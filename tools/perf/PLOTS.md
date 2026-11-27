# Offline figures

`plot_results.py` reads capture JSON and writes standalone PNG, SVG and a
versioned manifest. It never opens a serial port or modifies the input. Install
the optional plotting dependency separately from the capture environment:

```powershell
python -m pip install -r tools/perf/analysis-requirements.txt
python tools/perf/plot_results.py evidence/physical/full-01/results.json --output evidence/physical/full-01/figures --label 'Full campaign 01'
```

Use an explicit preflight/partial label when plotting incomplete campaigns.
Only completed `MEASURED` runs are plotted. Missing kernel or stress data cause
the corresponding figure to be omitted, with a reason in `manifest.json`.

Figures preserve these measurement distinctions:

- FPS comes from completed physical presentation counts and target elapsed
  steady-state time. Repetition dots, range and median remain visible. Matching
  actual allocation/configuration receipts define a repetition scope; differing
  scopes get separate labelled rows rather than one combined range.
- Audio triangles show the largest **per-run histogram p99 upper bound**;
  lines span repetitions. Crosses show exact observed maxima. Bounds can exceed
  exact maxima. Blocking I2S time includes waiting and cannot prove underruns.
- Kernel median/p99 are recomputed from exact chronological timing samples;
  each dot is one repetition statistic. Float/fixed precision differences and
  strip-generation exclusion of SPI remain explicit. Differing scopes for an
  otherwise identical kernel/size are rejected instead of silently pooled.
- Stress plots use retained target timestamps and tick-sampled utilization.
  Temperature is displayed as raw codes from the undocumented RTC-library
  sensor function. Neither these codes nor their Fahrenheit-derived conversion
  establish calibrated die temperature. Primary firmware's legacy
  `ESP32_ROM_raw_uncalibrated` label remains unchanged in raw logs; the linked
  function resides in the SDK RTC library rather than establishing mask-ROM
  provenance.

Each figure records the source/firmware identity and observation counts. The
manifest contains the exact input digest, plotting script digest, package
versions, command, plotted observations and output hashes. The original raw
measurement logs and structured capture JSON remain the evidence source.
