#!/usr/bin/env python3
"""Export outside-window sensor records from terminal, validated PCM captures.

Explicit input paths avoid touching an in-progress capture. This never opens a
serial port, modifies raw data or mixes these readings with steady-window data.
"""
import argparse
import collections
import csv
import hashlib
import json
import math
import pathlib

from audio_mvp_analyze import analyze


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def number(value):
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) else None


def generate(paths, output):
    rows, captures = [], []
    for path in paths:
        raw = json.loads(path.read_text(encoding="utf-8-sig"))
        if raw.get("status") not in ("FAILED", "CAPTURE COMPLETE") or raw.get("execution_complete") is not True:
            raise ValueError(f"Capture is not terminal/execution-complete: {path.parent.name}")
        summary = analyze(raw)
        source_hash = digest(path)
        count_before = len(rows)
        for run in summary["runs"]:
            if run["analysis_status"] != "MEASURED":
                continue
            observation = run.get("outside_window_temperature", {})
            for endpoint, exported in observation.get("endpoints", {}).items():
                identity = raw["endpoints"][endpoint]["identity"]
                for record in exported.get("records", []):
                    values = {key: number(record.get(key)) for key in
                              ("temperature_raw", "temperature_c_uncalibrated", "temperature_c_indication")}
                    if not any(value is not None for value in values.values()):
                        continue
                    rows.append({"capture": path.parent.name, "capture_status": raw["status"], "run_id": run["run_id"],
                                 "workload": run["workload"], "endpoint": endpoint, "board": identity.get("board"),
                                 "port_as_recorded": identity.get("port"), "profile": record.get("profile", identity.get("profile")),
                                 "source_sha": record.get("source_sha", identity.get("source_sha")),
                                 "firmware_sha256": identity.get("firmware_sha256"), "input_results_sha256": source_hash,
                                 "record_type": record.get("type"), "method_as_reported": record.get("temperature_method"),
                                 "target_uptime_us": record.get("uptime_us"),
                                 "host_received_elapsed_s": record.get("host_received_elapsed_s"),
                                 "observation_scope": observation.get("scope", "outside finite window; exact point unavailable"), **values})
        captures.append({"capture": path.parent.name, "capture_status": raw["status"],
                         "analysis_status_counts": summary["analysis_status_counts"], "retained_sensor_records": len(rows)-count_before,
                         "input_results_sha256": source_hash})
    grouped = collections.defaultdict(list)
    for row in rows:
        key = tuple(row[field] for field in ("capture", "capture_status", "endpoint", "board", "method_as_reported", "source_sha", "firmware_sha256"))
        grouped[key].append(row)
    groups = []
    for key, samples in grouped.items():
        group = dict(zip(("capture", "capture_status", "endpoint", "board", "method_as_reported", "source_sha", "firmware_sha256"), key))
        group["retained_records"] = len(samples)
        for field in ("temperature_raw", "temperature_c_uncalibrated", "temperature_c_indication"):
            values = [row[field] for row in samples if row[field] is not None]
            group.update({field+"_count": len(values), field+"_min": min(values) if values else None,
                          field+"_max": max(values) if values else None,
                          field+"_first": values[0] if values else None, field+"_last": values[-1] if values else None})
        groups.append(group)
    output.mkdir(parents=True, exist_ok=True)
    fields = ["capture", "capture_status", "run_id", "workload", "endpoint", "board", "port_as_recorded", "profile", "source_sha",
              "firmware_sha256", "input_results_sha256", "record_type", "method_as_reported", "target_uptime_us", "host_received_elapsed_s",
              "observation_scope", "temperature_raw", "temperature_c_uncalibrated", "temperature_c_indication"]
    for name, values, columns in (("PCM-TEMPERATURE-SAMPLES.csv", rows, fields),
                                  ("PCM-TEMPERATURE-SUMMARY.csv", groups, list(groups[0]) if groups else ["capture", "retained_records"])):
        with (output/name).open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=columns)
            writer.writeheader();writer.writerows(values)
    manifest = {"schema": "cyd-pcm-temperature-publication/1", "scope": "outside-window endpoint readings in terminal validated PCM runs only",
                "captures": captures, "tool_sha256": digest(pathlib.Path(__file__)),
                "outputs": {name: digest(output/name) for name in ("PCM-TEMPERATURE-SAMPLES.csv", "PCM-TEMPERATURE-SUMMARY.csv")},
                "limitations": ["Not steady-window or stress sensor sampling", "Driver Celsius and classic raw conversion remain distinct methods",
                                "No external accuracy calibration", "Missing/unavailable readings are not synthesized as zero"]}
    (output/"PCM-TEMPERATURE-MANIFEST.json").write_text(json.dumps(manifest, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=pathlib.Path, nargs="+")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    print(json.dumps(generate(args.results, args.output), indent=2))


if __name__ == "__main__":
    main()
