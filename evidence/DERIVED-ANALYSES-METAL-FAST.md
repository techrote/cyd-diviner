# Fast-cohort detailed derived archive

The [complete fast-cohort archive](DERIVED-ANALYSES-METAL-FAST.zip) preserves **759 original files / 70,514,862 bytes** from exactly 22 terminal-frozen derived directories: native analysis/envelope output for smoke, rate, framed, compat, reference and both receivers, plus the two framed demodulation outputs. All root-level JSON, CSV, reports and original manifests remain live inspectable files; 667 nested trial, raw-PCM/IQ conversion, full-spectrum/edge and CLI-log files are archived for publication. No WAVs were present in these trees; no extension filter discarded any file.

Archive size: **26,822,910 bytes**. SHA256:

`e65fedfac3532f7935a32daf89ee38118a1afcbbd4bb26833e6b6bf60ed05ef2`

The embedded `DERIVED-ANALYSES-MANIFEST.json` SHA256 is `1750cef3324eedc96b000e17b1dae024eb6609fceaea1f3b4fa94f59722fb9a5`. Exact membership, every original/packaging payload hash, ZIP CRC and a fresh extraction were verified. All 22 original output manifests pass. Original file bytes and the initial [derived archive](DERIVED-ANALYSES.md) remain unchanged.

Read the [receipt](derived-analyses-metal-fast-receipt.json), [compact layout](derived-metal-fast-publication-layout.json) and [applied staging audit](derived-metal-fast-publication-applied.json). These analyses derive from separately archived physical data in `PHYSICAL-CAPTURES-METAL-FAST.zip` (SHA256 `38ab510e0fd0fe1039f5fca01d9c7e7e9f5cc3ba90579f9161aecf8dc4c4b463`). This derived ZIP does not replace that raw archive and contains no firmware images or new stimulus/plan/source inputs. Top-level METAL-FAST-FINDINGS report/figures are published separately by the analysis owner.

## Extract

Use a NEW directory outside the live checkout, for example `../cyd-metal-fast-derived-review`. Expanding the archive restores `evidence/<original-directory>/...`, so original manifests, trial files and relative source-member paths work against the full tree. Original manifests can reference archived details intentionally absent as loose Git files. Do not reinterpret a pruned live directory as the full original analysis. All statuses and evidence classes remain unchanged.

```powershell
Get-FileHash evidence/DERIVED-ANALYSES-METAL-FAST.zip -Algorithm SHA256
Expand-Archive -LiteralPath evidence/DERIVED-ANALYSES-METAL-FAST.zip -DestinationPath ../cyd-metal-fast-derived-review
```

For a complete file-by-file check, use the verification recipe in the [first archive guide](DERIVED-ANALYSES.md#extract-and-verify), substituting this archive filename, SHA256 and manifest SHA256. Every `files`/`packaging_files` entry specifies byte length and SHA256. The packaging helper is itself included as `DERIVED-PACKAGING/build_derived_archives.py`. Fixed ZIP timestamps are reproducibility metadata, not measurement timestamps; measurement times remain in original content.

## Live and archived layout

| Original directory prefix | Files preserved | Live root files | Nested archive-only files | Original bytes |
| --- | ---: | ---: | ---: | ---: |
| `evidence/metal-fast-compat-rx-analysis/` | 5 | 5 | 0 | 185,229 |
| `evidence/metal-fast-compat-rx-envelope/` | 27 | 3 | 24 | 4,132,907 |
| `evidence/metal-fast-compat-rx2-analysis/` | 5 | 5 | 0 | 185,286 |
| `evidence/metal-fast-compat-rx2-envelope/` | 27 | 3 | 24 | 4,079,766 |
| `evidence/metal-fast-framed-rx-analysis/` | 5 | 5 | 0 | 319,507 |
| `evidence/metal-fast-framed-rx-demod/` | 147 | 6 | 141 | 1,117,606 |
| `evidence/metal-fast-framed-rx-envelope/` | 45 | 3 | 42 | 7,036,424 |
| `evidence/metal-fast-framed-rx2-analysis/` | 5 | 5 | 0 | 319,227 |
| `evidence/metal-fast-framed-rx2-demod/` | 148 | 6 | 142 | 1,118,294 |
| `evidence/metal-fast-framed-rx2-envelope/` | 45 | 3 | 42 | 6,761,012 |
| `evidence/metal-fast-rate-rx-analysis/` | 5 | 5 | 0 | 625,552 |
| `evidence/metal-fast-rate-rx-envelope/` | 87 | 3 | 84 | 14,511,421 |
| `evidence/metal-fast-rate-rx2-analysis/` | 5 | 5 | 0 | 625,149 |
| `evidence/metal-fast-rate-rx2-envelope/` | 87 | 3 | 84 | 14,392,228 |
| `evidence/metal-fast-reference-rx-analysis/` | 5 | 5 | 0 | 181,252 |
| `evidence/metal-fast-reference-rx-envelope/` | 39 | 3 | 36 | 6,280,925 |
| `evidence/metal-fast-reference-rx2-analysis/` | 5 | 5 | 0 | 181,298 |
| `evidence/metal-fast-reference-rx2-envelope/` | 39 | 3 | 36 | 6,279,538 |
| `evidence/metal-fast-smoke-rx-analysis/` | 5 | 5 | 0 | 52,259 |
| `evidence/metal-fast-smoke-rx-envelope/` | 9 | 3 | 6 | 1,029,948 |
| `evidence/metal-fast-smoke-rx2-analysis/` | 5 | 5 | 0 | 52,219 |
| `evidence/metal-fast-smoke-rx2-envelope/` | 9 | 3 | 6 | 1,047,815 |

Publication staging is restricted to these frozen new directories and this archive/receipt/guide/layout/audit/manifest. No original is deleted from disk; no previous derived archive, source, raw capture or active/future input is altered. The earlier approved nested-output ignore rules prevent restaging the 667 detail files while root-level results remain visible.
