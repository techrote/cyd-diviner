# Derived capture index

`mission_index.py` reads terminal `*/results.json` captures from a directory.
It does not open ports, flash firmware, change a raw capture or rerun a test.
Extract the checksummed physical archives into one fresh directory, preserving
their `physical/<capture>` paths, then run:

```powershell
python tools/perf/mission_index.py `
  --captures replay-evidence/physical `
  --metadata evidence/mission-metadata.json `
  --output replay-index
```

The JSON preserves each source schema and terminal/run status. The CSV includes
source/image identities per endpoint, including when a headless S3 occupies the
logical `cyd` role. `MEASURED` and `ACQUIRED` retain their source meanings:
successful acquisition alone does not establish RF provenance, decoded data,
latency or analog quality. A dual SDR row is one shared TX exposure; its two
receiver bodies are separate observations. A failed shared exposure can still
retain a valid receiver body. Setup diagnostics and USB-only re-exports add no
RF exposure. Unfinished rows inside failed captures remain incomplete.

The manifest pins the metadata, index tool, every input results file and both
generated outputs. Unknown schemas and active captures are rejected. Report
metadata is deliberately separate from measured rows, and the generation UTC
will change on a later reproduction. Existing output is refused unless the
operator explicitly supplies `--replace` for these derived index files.
