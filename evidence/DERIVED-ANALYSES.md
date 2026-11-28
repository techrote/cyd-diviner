# Detailed derived-analysis archive

The [complete archive](DERIVED-ANALYSES.zip) preserves **1,880 original on-disk files / 50,809,558 bytes** from 34 derived-analysis/model directories. This includes **79 WAVs and 82 Git-ignored files**, original PCM/IQ conversions, every CLI trial/log/frame file, full per-snapshot spectra/edges and byte-exact original manifests. No raw physical capture, existing ZIP, frozen source, plan or live result was modified. New fast-stimulus inputs are outside the selection.

Archive: **21,617,463 bytes**. SHA256:

`166c58d13a6168ccf4653c9f852d8be6234912d7cf9e9087c4a053086ec79850`

The embedded `DERIVED-ANALYSES-MANIFEST.json` records every original member path, byte length, SHA256, Git-ignore/index snapshot and proposed live disposition. Its SHA256 is `40c728a0a5b1045e3f4fdedbda76d43eb299069c9ff917b4bb40b57e762f6109`. ZIP CRC, exact membership, every payload hash and a fresh complete extraction were verified; the original source trees were rechecked unchanged. All 31 recognized original output manifests passed their own output hashes. All other files/manifests are also byte-exact archived members.

The [receipt](derived-analyses-receipt.json) and [compact layout](derived-publication-layout.json) remain inspectable. Original root-level JSON, CSV, reports, manifests and figures remain byte-exact live files. Detailed nested spectral CSVs, trials, PCM/IQ/log/frame files and WAVs are proposed archive-only publication assets; the approved application has **unstaged only 1,562 new added detail paths**, while deleting no on-disk files. This preserves working result/report links without replacing original results with pointer stubs. The link scan found no required rewrites or missing local Markdown links at packaging time.

Original manifests describe the **complete extracted tree**. They can intentionally reference a detailed member that is no longer a loose published file after pruning. Verify those manifests against the extracted archive, not against a compact/pruned publication. File-local source paths in detailed JSON likewise refer to that original layout. Capture/analysis statuses, failed cohorts, error records and evidence classes are unchanged; generated/offline-model audio is not relabelled as physical RF or analog output.

## Extract and verify

Work from the repository root and use a NEW destination. Extraction restores `../cyd-derived-review/evidence/<original-directory>/...`; it does not require overwriting the live repository or accessing hardware. The packaging source is also included as `DERIVED-PACKAGING/build_derived_archives.py`. ZIP timestamps use a fixed reproducibility stamp; original measurement/provenance timestamps remain in unchanged file contents.

```powershell
@'
import hashlib, json, pathlib, zipfile
archive = pathlib.Path("evidence/DERIVED-ANALYSES.zip")
assert hashlib.sha256(archive.read_bytes()).hexdigest() == "166c58d13a6168ccf4653c9f852d8be6234912d7cf9e9087c4a053086ec79850"
destination = pathlib.Path("../cyd-derived-review")
destination.mkdir(exist_ok=False)
with zipfile.ZipFile(archive) as source:
    assert source.testzip() is None
    encoded = source.read("DERIVED-ANALYSES-MANIFEST.json")
    assert hashlib.sha256(encoded).hexdigest() == "40c728a0a5b1045e3f4fdedbda76d43eb299069c9ff917b4bb40b57e762f6109"
    manifest = json.loads(encoded)
    records = {**manifest["files"], **manifest["packaging_files"]}
    assert set(source.namelist()) == set(records) | {"DERIVED-ANALYSES-MANIFEST.json"}
    for name in source.namelist():
        path = pathlib.PurePosixPath(name)
        assert not path.is_absolute() and ".." not in path.parts and ":" not in path.parts[0]
    source.extractall(destination)
for name, record in records.items():
    payload = (destination / name).read_bytes()
    assert len(payload) == record["bytes"]
    assert hashlib.sha256(payload).hexdigest() == record["sha256"]
print("Verified complete derived-analysis extraction")
'@ | python -
```

The packaging helper is non-destructive and has a hardware-free `--self-test`. It only creates a NEW archive/proposal; it never applies Git-index or deletion operations. Rebuilding after pruning cannot recover omitted originals; retain this immutable archive or extract it before any later repackaging. Physical/raw archives and owner firmware/backup artifacts remain separate.

## Original directory layout

Each listed prefix is relative to the extraction destination. Full JSON/manifests and the summary CSVs also remain available as loose live files.

| Original prefix | All original files | Actual paths retained in index | WAVs archived | Original bytes |
| --- | ---: | ---: | ---: | ---: |
| `evidence/audio-bidirectional-deferred-analysis/` | 4 | 4 | 0 | 337,674 |
| `evidence/audio-deferred-fresh-analysis/` | 4 | 4 | 0 | 46,676 |
| `evidence/audio-offline-osr16/` | 38 | 23 | 15 | 5,800,399 |
| `evidence/audio-offline-osr32/` | 38 | 23 | 15 | 5,800,331 |
| `evidence/audio-s3-pair-02-analysis/` | 4 | 4 | 0 | 771,167 |
| `evidence/audio-s3-pair-deferred-analysis/` | 4 | 4 | 0 | 807,252 |
| `evidence/host-kernels/` | 16 | 16 | 0 | 51,115 |
| `evidence/metal-framed-wifi-rx-demod/` | 324 | 6 | 0 | 1,696,457 |
| `evidence/metal-framed-wifi-rx2-demod/` | 311 | 6 | 0 | 1,461,783 |
| `evidence/metal-framed-wifi-swapped-rx-demod/` | 319 | 6 | 0 | 1,573,852 |
| `evidence/metal-framed-wifi-swapped-rx2-demod/` | 322 | 6 | 0 | 1,633,027 |
| `evidence/metal-position-swap-rx-analysis/` | 5 | 5 | 0 | 122,356 |
| `evidence/metal-position-swap-rx-envelope/` | 3 | 3 | 0 | 76,206 |
| `evidence/metal-position-swap-rx2-analysis/` | 5 | 5 | 0 | 122,381 |
| `evidence/metal-position-swap-rx2-envelope/` | 3 | 3 | 0 | 93,343 |
| `evidence/metal-rate-wifi-rx-analysis/` | 5 | 5 | 0 | 505,838 |
| `evidence/metal-rate-wifi-rx-envelope/` | 81 | 3 | 0 | 13,466,662 |
| `evidence/metal-rate-wifi-rx2-analysis/` | 5 | 5 | 0 | 505,253 |
| `evidence/metal-rate-wifi-rx2-envelope/` | 81 | 3 | 0 | 13,628,911 |
| `evidence/metal-smoke-wifi-rx-analysis/` | 5 | 5 | 0 | 145,315 |
| `evidence/metal-smoke-wifi-rx2-analysis/` | 5 | 5 | 0 | 145,198 |
| `evidence/native-replay/` | 11 | 7 | 1 | 550,545 |
| `evidence/sdr-framed-23k-analysis/` | 25 | 4 | 6 | 151,029 |
| `evidence/sdr-framed-four-analysis/` | 25 | 4 | 6 | 159,506 |
| `evidence/sdr-framed-low-analysis/` | 53 | 4 | 14 | 356,592 |
| `evidence/sdr-framed-low-analysis-v2/` | 53 | 4 | 14 | 354,994 |
| `evidence/sdr-framed-stimuli/` | 34 | 26 | 8 | 51,250 |
| `evidence/sdr-metal-stimuli/` | 25 | 25 | 0 | 49,302 |
| `evidence/sdr-signals-calibration-01/` | 12 | 3 | 0 | 58,852 |
| `evidence/sdr-signals-offset-minus80/` | 6 | 3 | 0 | 20,666 |
| `evidence/sdr-signals-offset-plus80/` | 6 | 3 | 0 | 20,778 |
| `evidence/sdr-signals-ram-125k-01/` | 15 | 3 | 0 | 75,033 |
| `evidence/sdr-signals-ram-250k-v2-01/` | 15 | 3 | 0 | 77,233 |
| `evidence/sdr-signals-ram-centered-01/` | 18 | 3 | 0 | 92,582 |

## Publication coordination

The original full proposal (not applied verbatim) is `.pio/derived-publication-01/publication-plan.json`, with `keep-original-paths.txt`, `archive-only-proposed-paths.txt` and `git-index-removal-candidates.txt`. Of 1,602 original candidates, **1,562 new added detail paths were approved and unstaged**. All **40 paths already in HEAD** (the two offline audio models’ PCM/log/stdout details) and all protected generated-input index paths were retained. All 1,880 archived originals remain byte-exact on disk, including the retained double copies. No on-disk deletion occurred. See [applied audit](derived-publication-applied.json) for actual index and membership counts.

The same exact archive-only list is reproducible from the public embedded manifest by selecting `files` entries whose `live_disposition` is `ARCHIVE_ONLY_PROPOSED`. The Git snapshot flags explain their packaging-time status; they do not claim future index state. Root controls the final evidence commit; no commit was performed by this helper.
