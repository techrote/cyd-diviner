# Fan-repeat detailed analysis archive

[DERIVED-ANALYSES-METAL-FAN.zip](DERIVED-ANALYSES-METAL-FAN.zip) preserves **93 original files / 707,648 bytes** from the two terminal-complete `metal-fan-framed-rx-demod` and `metal-fan-framed-rx2-demod` trees. All 12 root-level JSON/CSV/manifests remain live; 81 nested conversion/trial/PCM/frame/log files are archived for publication. Every file was included without extension filtering. No WAVs were present.

Archive size: **353,840 bytes**. SHA256:

`f41e5db392d0c47849a9f5327f975a8362683b2baa8d0c9ad102444da029c980`

The embedded `DERIVED-ANALYSES-MANIFEST.json` SHA256 is `3e86e1711c7ffbae6973ccbc6db435133b64c840e3a413941b8deb34e6362006`. Both original manifests, every member hash/length, ZIP CRC and a fresh extraction pass. All original files remain byte-exact on disk. Earlier raw/derived archives and metadata are untouched.

The separate [raw archive](PHYSICAL-CAPTURES-METAL-FAN.zip) contains all 41 parent/child raw files for six shared exposures with 1s requested timedTX loops and 12 native bodies. Its SHA256 is `4cbc191f89108681fec2678620458e82b494623e457c7cf9dcff2a9e899a582e`; [raw receipt](metal-fan-archive-receipt.json) records 74 nested parent/child hash checks. Owner fan/board movement is uncontrolled context, not evidence of thermal causality. The independently authored FAN-FRAMED-REPEAT report is outside these archive trees.

Inspect the [derived receipt](derived-analyses-metal-fan-receipt.json), [live layout](derived-metal-fan-publication-layout.json) and [applied staging audit](derived-metal-fan-publication-applied.json). Original acquisition/analysis statuses and evidence classes are preserved.

## Extract

Use a NEW destination outside the checkout. Extraction restores `evidence/<original-directory>/...`, including all original relative data paths. Original manifests verify against that complete extracted tree, not a pruned loose publication.

```powershell
Get-FileHash evidence/DERIVED-ANALYSES-METAL-FAN.zip -Algorithm SHA256
Expand-Archive -LiteralPath evidence/DERIVED-ANALYSES-METAL-FAN.zip -DestinationPath ../cyd-metal-fan-derived-review
```

For file-by-file verification use the [standard recipe](DERIVED-ANALYSES.md#extract-and-verify), substituting this ZIP filename, SHA256 and embedded manifest SHA256. The archive also includes its packaging source under `DERIVED-PACKAGING/build_derived_archives.py`. The fixed ZIP timestamp is reproducibility metadata, not a measurement timestamp.
