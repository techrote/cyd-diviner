#!/usr/bin/env python3
"""Extract retained sensor indications from valid physical workload windows."""
import csv
import hashlib
import json
import pathlib
import sys


def generate(root, output):
    rows, summaries = [], []
    for path in sorted(root.glob("*/results.json")):
        payload = path.read_bytes()
        input_hash = hashlib.sha256(payload).hexdigest()
        result = json.loads(payload)
        if result.get("evidence_class") != "physical_measured":
            continue
        identity = result.get("endpoints", {}).get("cyd", {}).get("identity", result)
        info = result.get("target_info", result.get("endpoints", {}).get("cyd", {}).get("target_info", []))
        reported = next((r.get("temperature_method") for r in info if r.get("temperature_method")), None)
        records = []
        for run in result.get("runs", []):
            if run.get("status") != "MEASURED" and run.get("profile_status") != "MEASURED":
                continue
            for record in run.get("target", run.get("cyd_profile", [])):
                if not any(record.get(k) is not None for k in ("temperature_raw", "temperature_c_uncalibrated", "temperature_c_indication")):
                    continue
                raw = record.get("temperature_raw")
                driver = record.get("temperature_c_indication")
                row = {"capture": path.parent.name, "capture_status": result["status"], "run_id": run["run_id"],
                       "workload": run["workload"], "run_status": run["status"], "record_type": record["type"],
                       "source_sha": identity.get("source_sha"), "firmware_sha256": identity.get("firmware_sha256"),
                       "input_sha256": input_hash,
                       "target_elapsed_ms": record.get("elapsed_ms"), "host_received_elapsed_s": record.get("host_received_elapsed_s"),
                       "method_as_reported": record.get("temperature_method", reported), "temperature_raw": raw,
                       "temperature_c_uncalibrated": record.get("temperature_c_uncalibrated"), "temperature_c_indication": driver,
                       "interpretation": "S3 driver Celsius indication; external accuracy unverified" if driver is not None else "Classic RTC-library raw code; undocumented and uncalibrated"}
                records.append(row)
        if not records:
            continue
        rows.extend(records)
        summary = {"capture": path.parent.name, "capture_status": result["status"], "retained_records_in_valid_windows": len(records)}
        for field in ("temperature_raw", "temperature_c_uncalibrated", "temperature_c_indication"):
            values = [r[field] for r in records if r[field] is not None]
            summary[field + "_min"] = min(values) if values else None
            summary[field + "_max"] = max(values) if values else None
        summaries.append(summary)
    for name, data in (("TEMPERATURE-SAMPLES.csv", rows), ("TEMPERATURE-SUMMARY.csv", summaries)):
        with (output / name).open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=list(data[0])); writer.writeheader(); writer.writerows(data)
    return summaries


if __name__ == "__main__":
    print(json.dumps(generate(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])), indent=2))
