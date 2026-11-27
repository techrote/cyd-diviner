#!/usr/bin/env python3
"""Publish compact derived evidence from a completed, already archived capture.

This helper is offline. It never opens a serial port, creates an archive, modifies
the capture folder, or converts an incomplete campaign into completed evidence.
"""
from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import io
import json
import os
import pathlib
import re
import shutil
import tempfile
import zipfile
from urllib.parse import quote

from report import generate
from summarize import summarize

SUMMARY_CATEGORIES = ("scenes", "metrics", "kernels", "spi", "missing")
RAW_REQUIRED = ("results.json", "samples.csv", "metrics.csv", "target.jsonl", "serial-rx.bin",
                "serial-events.jsonl", "serial-text.log", "artifact-hashes.json")
STRESS_FIELDS = ("source_sha", "firmware_sha256", "profile", "run_id", "workload", "repetition",
                 "target_elapsed_ms", "steady_elapsed_ms", "temperature_method", "temperature_raw",
                 "temperature_c_uncalibrated", "cpu0_busy_ticks", "cpu0_total_ticks", "core0_busy_pct",
                 "cpu1_busy_ticks", "cpu1_total_ticks", "core1_busy_pct", "heap_free_bytes", "heap_min_bytes",
                 "heap_largest_bytes", "audio_stack_min_bytes", "loop_stack_min_bytes")


def sha256(path: pathlib.Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            hasher.update(block)
    return hasher.hexdigest()


def file_receipt(path: pathlib.Path) -> dict:
    return {"bytes": path.stat().st_size, "sha256": sha256(path)}


def safe_child(root: pathlib.Path, name: str) -> pathlib.Path:
    child = (root / name).resolve()
    if not child.is_relative_to(root.resolve()) or child == root.resolve():
        raise ValueError("Artifact path leaves its capture folder: " + name)
    return child


def load_complete(capture: pathlib.Path) -> tuple[dict, bytes]:
    payload = (capture / "results.json").read_bytes()
    result = json.loads(payload)
    if result.get("schema") != "cyd-perf-capture/1" or result.get("evidence_class") != "physical_measured":
        raise ValueError("Expected physical cyd-perf-capture/1 evidence")
    if result.get("status") != "CAPTURE COMPLETE" or result.get("parse_errors"):
        raise ValueError("Only CAPTURE COMPLETE without parse errors can be published")
    if not re.fullmatch(r"[0-9a-f]{40}", result.get("source_sha", "")) or not re.fullmatch(r"[0-9a-f]{64}", result.get("firmware_sha256", "")):
        raise ValueError("Missing exact source/firmware identity")
    plan = result.get("plan", {})
    if plan.get("schema") != "cyd-perf-plan/1":
        raise ValueError("Capture plan identity is missing")
    expected = collections.Counter()
    expected_missing = collections.Counter()
    for case in plan.get("workloads", []):
        if case.get("status") == "NOT RUN":
            expected_missing[case["name"]] += 1
        else:
            expected[case["name"]] += case.get("repetitions", plan.get("repetitions", 3))
    measured = collections.Counter(run["workload"] for run in result.get("runs", []) if run.get("status") == "MEASURED")
    missing = collections.Counter(run["workload"] for run in result.get("runs", []) if run.get("status") == "NOT RUN")
    if not measured or measured != expected or missing != expected_missing or any(run.get("status") not in ("MEASURED", "NOT RUN") for run in result.get("runs", [])):
        raise ValueError("Completed status does not match all planned repetitions/NOT RUN entries")
    # This validates completed end markers, raw histogram counts and kernel/SPI
    # sample retention. A status string alone is insufficient.
    summarize(result)
    return result, payload


def verified_raw(capture: pathlib.Path) -> dict[str, dict]:
    hashes = json.loads((capture / "artifact-hashes.json").read_text(encoding="utf-8"))
    if not isinstance(hashes, dict):
        raise ValueError("Invalid capture artifact hash manifest")
    names = set(RAW_REQUIRED) | set(hashes) | {path.name for path in capture.glob("*-pcm.bin")}
    receipts = {}
    for name in sorted(names):
        path = safe_child(capture, name)
        if not path.is_file():
            raise ValueError("Missing raw capture artifact: " + name)
        receipt = file_receipt(path)
        if name in hashes and hashes[name] != receipt["sha256"]:
            raise ValueError("Raw capture artifact hash mismatch: " + name)
        receipts[name] = receipt
    return receipts


def verify_archive(archive: pathlib.Path, prefix: str, receipts: dict[str, dict]) -> dict:
    pure = pathlib.PurePosixPath(prefix)
    if pure.is_absolute() or ".." in pure.parts or not re.fullmatch(r"[a-zA-Z0-9._/-]+", prefix) or str(pure) in ("", "."):
        raise ValueError("--archive-prefix must be a safe relative POSIX member directory")
    receipt = file_receipt(archive)
    members = {}
    with zipfile.ZipFile(archive, "r") as bundle:
        duplicates = {name for name, count in collections.Counter(bundle.namelist()).items() if count > 1}
        for name, expected in receipts.items():
            member = str(pure / pathlib.PurePosixPath(name))
            if member in duplicates:
                raise ValueError("Duplicate archive member: " + member)
            try:
                entry = bundle.getinfo(member)
            except KeyError as error:
                raise ValueError("Missing archive member: " + member) from error
            if entry.file_size != expected["bytes"]:
                raise ValueError("Archive member differs from completed capture: " + member)
            hasher = hashlib.sha256()
            with bundle.open(entry, "r") as stream:
                while block := stream.read(1024 * 1024):
                    hasher.update(block)
            if hasher.hexdigest() != expected["sha256"]:
                raise ValueError("Archive member differs from completed capture: " + member)
            members[member] = {**expected, "compressed_bytes": entry.compress_size}
    return {**receipt, "verified_members": members}


def read_summary(capture: pathlib.Path, result: dict) -> dict[str, dict]:
    path = capture / "summary.json"
    existing = json.loads(path.read_text(encoding="utf-8"))
    expected = summarize(result)
    if existing != expected:
        raise ValueError("Summary is stale or differs from capture; rerun summarize.py first")
    receipts = {"summary.json": file_receipt(path)}
    for category in SUMMARY_CATEGORIES:
        name = f"summary-{category}.csv"
        path = capture / name
        keys = list(dict.fromkeys(key for row in expected[category] for key in row)) or ["workload", "status"]
        reference = io.StringIO(newline="")
        writer = csv.DictWriter(reference, fieldnames=keys)
        writer.writeheader()
        writer.writerows(expected[category])
        if path.read_bytes() != reference.getvalue().encode("utf-8"):
            raise ValueError("Summary CSV is stale or differs from capture: " + name)
        receipts[name] = file_receipt(path)
    return {"data": existing, "receipts": receipts}


def read_figures(capture: pathlib.Path, result: dict, input_hash: str) -> dict:
    directory = capture / "figures"
    manifest_path = directory / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != "cyd-perf-figures/1" or manifest.get("capture_status") != "CAPTURE COMPLETE":
        raise ValueError("Figures require a completed physical capture manifest")
    for field, expected in (("input_sha256", input_hash), ("source_sha", result["source_sha"]),
                            ("firmware_sha256", result["firmware_sha256"]), ("profile", result["profile"])):
        if manifest.get(field) != expected:
            raise ValueError("Figures provenance mismatch: " + field)
    receipts = {"manifest.json": file_receipt(manifest_path)}
    for figure in manifest.get("figures", []):
        for item in figure["files"]:
            name = item["file"]
            path = safe_child(directory, name)
            if path.suffix.lower() not in (".png", ".svg"):
                raise ValueError("Unexpected figure format: " + name)
            receipt = file_receipt(path)
            if receipt["sha256"] != item["sha256"]:
                raise ValueError("Figure hash mismatch: " + name)
            receipts[name] = receipt
    return {"manifest": manifest, "receipts": receipts}


def stress_rows(result: dict) -> list[dict]:
    info = next((record for record in result.get("target_info", []) if record.get("type") == "info"), {})
    rows = []
    for run in result["runs"]:
        if run["status"] != "MEASURED" or not run.get("settings", {}).get("stress_seconds"):
            continue
        end = next(record for record in run["target"] if record.get("type") == "end")
        for sample in run["target"]:
            if sample.get("type") != "sample":
                continue
            row = {"source_sha": result["source_sha"], "firmware_sha256": result["firmware_sha256"],
                   "profile": result["profile"], "run_id": run["run_id"], "workload": run["workload"],
                   "repetition": run.get("repetition"), "target_elapsed_ms": sample["elapsed_ms"],
                   "steady_elapsed_ms": sample["elapsed_ms"] - end["warmup_ms"],
                   "temperature_method": end.get("temperature_method", info.get("temperature_method", "UNKNOWN"))}
            row.update({field: sample.get(field) for field in STRESS_FIELDS if field not in row})
            rows.append(row)
    return rows


def archive_links(markdown: str, archive_href: str, prefix: str, raw_names: set[str]) -> str:
    def replace(match):
        label, target = match.groups()
        if target not in raw_names:
            return match.group(0)
        return f"[{label}]({archive_href}) (archive member `{prefix}/{target}`)"
    return re.sub(r"\[([^\]\n]+)\]\(([^)\n]+)\)", replace, markdown)


def publish(capture: pathlib.Path, output: pathlib.Path, archive: pathlib.Path, prefix: str, label: str) -> pathlib.Path:
    capture, output, archive = capture.resolve(), output.resolve(), archive.resolve()
    if output.exists():
        raise ValueError("Output directory already exists; choose a new publication directory")
    if output.is_relative_to(capture):
        raise ValueError("Publication output must not be inside the raw capture folder")
    result, payload = load_complete(capture)
    input_hash = hashlib.sha256(payload).hexdigest()
    raw_receipts = verified_raw(capture)
    if raw_receipts["results.json"]["sha256"] != input_hash:
        raise ValueError("Capture changed while being read")
    archive_receipt = verify_archive(archive, prefix, raw_receipts)
    compact = read_summary(capture, result)
    figures = read_figures(capture, result, input_hash)
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = pathlib.Path(tempfile.mkdtemp(prefix="." + output.name + "-", dir=output.parent))
    try:
        (stage / "summary.json").write_text(json.dumps(compact["data"], separators=(",", ":"), allow_nan=False) + "\n", encoding="utf-8")
        for category in SUMMARY_CATEGORIES:
            name = f"summary-{category}.csv"
            shutil.copyfile(capture / name, stage / name)
        (stage / "figures").mkdir()
        for name in figures["receipts"]:
            (stage / "figures" / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(capture / "figures" / name, stage / "figures" / name)
        samples = stress_rows(result)
        with (stage / "stress-samples.csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=STRESS_FIELDS)
            writer.writeheader()
            writer.writerows(samples)
        href = quote(pathlib.Path(os.path.relpath(archive, output)).as_posix(), safe="/._-")
        plot_paths = sorted("figures/" + name for name in figures["receipts"] if name.endswith(".png"))
        rendered = generate(result, label, input_hash, plot_paths)
        rendered = archive_links(rendered, href, prefix, set(raw_receipts))
        rendered += ("\n## Publication bundle\n\n"
                     f"This compact folder derives from the completed `{label}` capture. "
                     f"The [raw capture archive]({href}) has SHA256 `{archive_receipt['sha256']}`; "
                     f"its `{prefix}/` members preserve the full JSON, samples, target telemetry, serial RX, PCM and event logs. "
                     "Extract the archive into `evidence/` while retaining its `physical/` prefix before replaying the commands above. "
                     f"[stress-samples.csv](stress-samples.csv) contains {len(samples)} stress-only rows with source/firmware/profile identity, target timestamps, raw ticks, CPU percentages, sensor readings, heap and stack. "
                     "An empty table means that no stress workload was present in this completed capture. "
                     "[manifest.json](manifest.json) records input/output hashes and archive-member verification.\n")
        (stage / "REPORT.md").write_text(rendered, encoding="utf-8")
        input_receipts = {**{name: receipt for name, receipt in raw_receipts.items()},
                          **compact["receipts"], **{"figures/" + name: receipt for name, receipt in figures["receipts"].items()}}
        outputs = {path.relative_to(stage).as_posix(): file_receipt(path) for path in sorted(stage.rglob("*")) if path.is_file()}
        manifest = {"schema": "cyd-perf-publication/1", "label": label, "capture_status": result["status"],
                    "evidence_class": result["evidence_class"], "source_sha": result["source_sha"],
                    "firmware_sha256": result["firmware_sha256"], "profile": result["profile"],
                    "source_tree_state": result.get("source_tree_state"), "input_results_sha256": input_hash,
                    "archive": {"href": href, "member_prefix": prefix, **archive_receipt},
                    "inputs": input_receipts, "outputs": outputs, "stress_rows": len(samples),
                    "tools": {name: file_receipt(pathlib.Path(__file__).with_name(name))
                              for name in ("publish_evidence.py", "report.py", "summarize.py", "capture.py")},
                    "limitations": ["Raw archive membership and source hashes are verified; this tool does not independently attest hardware readings or the flashed image.",
                                    "Report is editable. Subsequent manual edits require refreshing output hashes or regenerating this bundle.",
                                    "Manifest excludes its own digest to avoid a self-referential hash."]}
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        for name, receipt in input_receipts.items():
            if file_receipt(safe_child(capture, name)) != receipt:
                raise ValueError("Capture artifact changed while publishing: " + name)
        if file_receipt(archive) != {key: archive_receipt[key] for key in ("bytes", "sha256")}:
            raise ValueError("Raw archive changed while publishing")
        stage.rename(output)
    except BaseException:
        # Preserve an explicitly named staging folder for inspection; never
        # recursively delete a computed Windows path or publish partial output.
        print("Publication failed; staging retained at " + str(stage), file=__import__("sys").stderr)
        raise
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--archive", type=pathlib.Path, required=True, help="Existing final ZIP, never created by this helper")
    parser.add_argument("--archive-prefix", required=True, help="Exact member directory, e.g. physical/full-01")
    parser.add_argument("--label", required=True, help="Explicit campaign/preflight label")
    args = parser.parse_args()
    print(publish(args.capture, args.output, args.archive, args.archive_prefix.rstrip("/"), args.label))


if __name__ == "__main__":
    main()
