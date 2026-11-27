#!/usr/bin/env python3
"""Publish offline wireless analysis after verifying its closed raw ZIP members."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import zipfile

from link_analyze import analyze, csv_file, report


def receipt(path):
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def publish(capture, output, archive):
    payload = (capture / "results.json").read_bytes()
    source = json.loads(payload)
    if source.get("status") not in ("CAPTURE COMPLETE", "FAILED", "INTERRUPTED"):
        raise ValueError("Only a terminal capture can be published")
    if output.exists():
        raise ValueError("Choose a new output folder")
    prefix = "physical/" + capture.name
    hashes = json.loads((capture / "artifact-hashes.json").read_text())
    names = set(hashes) | {"results.json", "artifact-hashes.json"}
    verified = {}
    with zipfile.ZipFile(archive) as bundle:
        if len(bundle.namelist()) != len(set(bundle.namelist())):
            raise ValueError("Duplicate archive members")
        for name in sorted(names):
            path = (capture / name).resolve()
            if not path.is_relative_to(capture.resolve()):
                raise ValueError("Unsafe artifact path")
            item = receipt(path)
            if name in hashes and item["sha256"] != hashes[name]:
                raise ValueError("Capture hash mismatch: " + name)
            stored = bundle.read(prefix + "/" + name)
            if len(stored) != item["bytes"] or hashlib.sha256(stored).hexdigest() != item["sha256"]:
                raise ValueError("Archive differs from capture: " + name)
            verified[name] = item
    analysis = analyze(source, capture)
    output.mkdir(parents=True)
    (output / "link-summary.json").write_text(json.dumps(analysis, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    csv_file(output / "link-runs.csv", analysis["runs"])
    csv_file(output / "link-groups.csv", analysis["groups"])
    import os
    href = pathlib.Path(os.path.relpath(archive, output)).as_posix()
    text = report(analysis, hashlib.sha256(payload).hexdigest(), href)
    text = text.replace("[Raw structured source](" + href + ")", "[Raw capture archive](" + href + ") (member `" + prefix + "/results.json`)")
    text += ("\n## Publication provenance\n\nExtract the raw archive into `evidence/`, preserving its `physical/` prefix. "
             "The publication verifies every capture-manifest member against the ZIP and preserves failed statuses. "
             "`manifest.json` pins archive, source and derived-output hashes.\n")
    (output / "LINK-REPORT.md").write_text(text, encoding="utf-8")
    manifest = {"schema": "cyd-perf-link-publication/1", "capture_status": source["status"],
                "archive": {"href": href, "member_prefix": prefix, **receipt(archive)},
                "input_results_sha256": hashlib.sha256(payload).hexdigest(), "verified_raw_members": verified,
                "tools": {name: receipt(pathlib.Path(__file__).with_name(name)) for name in ("publish_link_evidence.py", "link_analyze.py")},
                "outputs": {p.name: receipt(p) for p in sorted(output.iterdir()) if p.is_file()}}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--archive", type=pathlib.Path, required=True)
    args = parser.parse_args()
    print(publish(args.capture, args.output, args.archive))
