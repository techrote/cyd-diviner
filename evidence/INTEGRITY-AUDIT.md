# Evidence integrity audit

Snapshot `fce57f109ed6f58c0d51b48b8d74478b7817304c`; audit 2026-11-27T23:02:46.249776+00:00 to 2026-11-27T23:02:50.151300+00:00. This is an offline byte-integrity audit, not a new physical measurement.

**FAILURES_FOUND**. Detailed machine-readable checks and pending index items are in [INTEGRITY-AUDIT.json](INTEGRITY-AUDIT.json).

| Check | Count/result |
| --- | --- |
| ZIP archives / file members | 14 / 979 |
| Embedded raw artifact hash entries | 486 |
| Analysis/publication manifests | 40 |
| SHA256/length comparisons | 2120 |
| Temperature CSVs | 4 |
| Local Markdown targets / missing | 350 / 4 |
| Tracked hashed evidence with explicit -text | 243 |
| Tracked pending index update | 0 |
| Untracked hashed outputs pending staging | 323 |

All ZIP members were read to verify CRC; nested source archives were CRC checked. Archive-level SHA256/length were checked against available receipts, and embedded raw artifact manifests were resolved inside their archive. Historical non-raw image bundles without an archive-level receipt remain pinned by their Git blobs and embedded build/source receipts; this audit does not invent a missing receipt.

Published campaign output hashes and newer SDR, PCM, color, model, generated stimulus and endpoint-temperature manifests were checked. Source inputs were checked against retained local raw files or their archive members. Historical tool hashes remain historical provenance. Temperature CSVs were parsed and hashed; this establishes no sensor calibration or temperature accuracy.

All 4,314 main sensor-export rows and 100 PCM endpoint rows were independently matched to the corresponding retained raw run by record type, sensor values and host receipt time. The portable Windows package's 177 checkout source members match its pinned source commit: 160 exact Git blobs and 17 normal Windows CRLF checkout representations. The package manifest separately pins the exact archived bytes. The SDR and metal source members preserve exact bytes.

The JSON lists every tracked hashed file and its raw worktree/index blob. Modified tracked files and new unstaged outputs require the owner’s final staging/index audit. The snapshot does not claim that future commits contain today’s unstaged files.

All future/in-progress metal physical captures and derived analyses are excluded. Private backups, credentials, hardware access and firmware writes were not accessed. Markdown checks cover target existence; anchors and remote URLs were not checked. Links outside this checkout were not followed.

## Findings

- Missing local link in `evidence/campaign-link-ble/LINK-REPORT.md`: `link-analysis-manifest.json`.
- Missing local link in `evidence/campaign-link-fresh-ap/LINK-REPORT.md`: `link-analysis-manifest.json`.
- Missing local link in `evidence/campaign-link-wifi-02/LINK-REPORT.md`: `link-analysis-manifest.json`.
- Missing local link in `evidence/campaign-link-wifi-retry/LINK-REPORT.md`: `link-analysis-manifest.json`.
