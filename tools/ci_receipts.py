#!/usr/bin/env python3
"""Expose small evidence receipts in CI logs; complete artifacts remain retained."""
import argparse
import base64
import hashlib
import io
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("directory", type=Path)
p.add_argument("--previews", action="store_true")
a = p.parse_args()
if not a.directory.is_dir():
    print("DIVINER_RECEIPT unavailable: output directory was not created")
    raise SystemExit(0)
for name in ("report.json", "touch-tour-results.json"):
    path = a.directory / name
    if path.is_file():
        report = json.loads(path.read_text())
        print("DIVINER_REPORT " + json.dumps(report, separators=(",", ":")))
for path in sorted(a.directory.rglob("*.log")):
    if path.name in ("instrument-kernels.log", "radio-instrument-kernels.log",
                     "numeric-24000.log", "numeric-48000.log", "legacy-regression.log",
                     "lifecycle.log", "run.log"):
        text = path.read_text(errors="replace")
        for line in text.splitlines():
            if line.startswith(("HOST ", "PASS ", "NUMERIC ", "numeric ", "rate=")):
                print("DIVINER_MEASUREMENT " + str(path.relative_to(a.directory)) + " " + line)
if a.previews:
    from PIL import Image
    for path in sorted(a.directory.glob("*.ppm")):
        with Image.open(path) as image:
            if image.width * image.height > 320 * 320:
                raise ValueError("Unexpected preview size")
            buffer = io.BytesIO()
            image.save(buffer, format="PNG")
            data = buffer.getvalue()
        target = path.with_suffix(".png")
        target.write_bytes(data)
        print("DIVINER_PREVIEW " + json.dumps({"name": target.name,
            "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
            "base64": base64.b64encode(data).decode()}, separators=(",", ":")))
