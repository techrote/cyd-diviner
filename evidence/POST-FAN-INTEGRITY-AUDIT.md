# Evidence integrity audit

Snapshot `28aa418c4b809a284cfbd298fe07fd35913ceea9`; audit 2026-11-28T00:51:26.663833+00:00 to 2026-11-28T00:51:37.804328+00:00. This is an offline byte-integrity audit, not a new physical measurement.

**PASS_WITH_DECLARED_SCOPE**. Detailed machine-readable checks and pending index items are in [POST-FAN-INTEGRITY-AUDIT.json](POST-FAN-INTEGRITY-AUDIT.json).

| Check | Count/result |
| --- | --- |
| ZIP archives / file members | 21 / 4951 |
| Embedded raw artifact hash entries | 2581 |
| Analysis/publication manifests | 91 |
| SHA256/length comparisons | 7451 |
| Temperature CSVs | 5 |
| Local Markdown targets / missing | 594 / 0 |
| Tracked hashed evidence with explicit -text | 557 |
| Tracked pending index update | 0 |
| Untracked hashed outputs pending staging | 0 |

All ZIP members were read to verify CRC; nested source archives were CRC checked. Archive-level SHA256/length were checked against available receipts, and embedded raw artifact manifests were resolved inside their archive. Historical non-raw image bundles without an archive-level receipt remain pinned by their Git blobs and embedded build/source receipts; this audit does not invent a missing receipt.

Published campaign output hashes and newer SDR, PCM, color, model, generated stimulus and endpoint-temperature manifests were checked. Source inputs were checked against retained local raw files or their archive members. Historical tool hashes remain historical provenance. Temperature CSVs were parsed and hashed; this establishes no sensor calibration or temperature accuracy.

The JSON lists every tracked hashed file and its raw worktree/index blob. Modified tracked files and new unstaged outputs require the owner’s final staging/index audit. The snapshot does not claim that future commits contain today’s unstaged files.

All future/in-progress metal physical captures and derived analyses are excluded. Private backups, credentials, hardware access and firmware writes were not accessed. Markdown checks cover target existence; anchors and remote URLs were not checked. Links outside this checkout were not followed.

## Findings

No checksum, archive CRC, published output, byte-preservation attribute or local target-existence failure was found within this scope.
