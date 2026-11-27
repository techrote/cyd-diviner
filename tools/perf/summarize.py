#!/usr/bin/env python3
"""Aggregate real captures across repetitions without inventing unmeasured links."""
from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import pathlib

from capture import COLOR_SCOPE_FIELDS, distribution, validate_color_run


def write_csv(path: pathlib.Path, rows: list[dict]) -> None:
    keys = list(dict.fromkeys(key for row in rows for key in row)) or ["workload", "status"]
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def histogram_bound(bins: list[int], percent: int) -> int | None:
    count = sum(bins)
    if not count or percent == 99 and count < 100:
        return None
    target = (count * percent + 99) // 100
    cumulative = 0
    for index, value in enumerate(bins):
        cumulative += value
        if cumulative >= target:
            return 0xFFFFFFFF if index == 31 else 1 << index
    return None


def settings_scope(run: dict) -> str:
    """Pool only identical requested scopes and actual allocation/settings receipts."""
    colors = [{key: record[key] for key in COLOR_SCOPE_FIELDS if key in record}
              for record in run.get("target", []) if record.get("type") == "color_run"]
    actual = [{key: value for key, value in record.items() if key not in ("host_received_elapsed_s", "run_id")}
              for record in run.get("setup_target", []) if record.get("type") == "scene_settings"
              and not (colors and record.get("scene") == "color")]
    ends = [record for record in run.get("target", []) if record.get("type") == "end"]
    end = ends[0] if ends else {}
    scope = {"settings": run.get("settings", {}), "duration_ms": run.get("duration_ms_requested"),
             "warmup_ms": run.get("warmup_ms_requested"), "actual_settings": actual,
             "histograms_enabled": end.get("histograms_enabled", end.get("instrumentation"))}
    if colors:
        scope["actual_color_settings"] = colors
    return hashlib.sha256(json.dumps(scope, sort_keys=True, separators=(",", ":")).encode()).hexdigest()[:12]


def validate_measured_run(run: dict) -> None:
    records = run.get("target", [])
    validate_color_run(records, run.get("run_id"), run.get("settings", {}).get("expected_color"),
                       run.get("duration_ms_requested", 0))
    ends = [record for record in records if record.get("type") == "end"]
    if ends:
        if len(ends) != 1 or ends[0].get("complete") is not True or ends[0].get("elapsed_us", 0) <= 0:
            raise ValueError("Measured run has invalid completion evidence: " + run["workload"])
    for record in records:
        if record.get("type") == "metric":
            bins = record.get("histogram", [])
            if len(bins) != 32 or any(not isinstance(value, int) or value < 0 for value in bins) or sum(bins) != record.get("count"):
                raise ValueError("Invalid histogram evidence: " + run["workload"])
        elif record.get("type") in ("kernel", "spi"):
            samples = record.get("samples_us", [])
            if not samples or any(not isinstance(value, int) or value < 0 for value in samples):
                raise ValueError("Missing/invalid raw timing samples: " + run["workload"])
            if record["type"] == "kernel" and len(samples) != record.get("samples"):
                raise ValueError("Kernel sample count mismatch: " + run["workload"])


def summarize(result: dict) -> dict:
    info = next((record for record in result.get("target_info", []) if record.get("type") == "info"), {})
    temperature_method = info.get("temperature_method", "UNKNOWN")
    temperature_note = ("Legacy ESP32_ROM tag is a recorded label, not proof of mask-ROM implementation; approved a4549f8 image links SDK librtc.a rtc_analog.o. Raw indication is uncalibrated and sensor accuracy unsupported."
                        if "ESP32_ROM" in temperature_method else "Method label retained verbatim. Raw sensor indication and converted values are uncalibrated; temperature accuracy is not independently established.")
    grouped = collections.defaultdict(list)
    missing = []
    for run in result["runs"]:
        if run["status"] == "MEASURED":
            validate_measured_run(run)
            grouped[(run["workload"], settings_scope(run))].append(run)
        else:
            missing.append({"workload": run["workload"], "status": run["status"], "reason": run.get("reason")})
    scenes, metrics, kernels, spi = [], [], [], []
    for (workload, scope), runs in grouped.items():
        ends = [record for run in runs for record in run.get("target", []) if record.get("type") == "end"]
        if ends:
            elapsed = sum(record["elapsed_us"] for record in ends)
            frames = sum(record["frames"] for record in ends)
            sample_values = [record for run in runs for record in run.get("target", []) if record.get("type") == "sample"]
            temperatures = [record["temperature_raw"] for record in sample_values if isinstance(record.get("temperature_raw"), (float, int))]
            row = {"workload": workload, "scope": scope, "repetitions": len(ends), "frames": frames,
                   "elapsed_s": elapsed / 1e6, "completed_frames_per_s": frames * 1e6 / elapsed if elapsed else None,
                   "temperature_raw_min": min(temperatures) if temperatures else None,
                   "temperature_raw_max": max(temperatures) if temperatures else None,
                   "temperature_method": temperature_method, "temperature_method_note": temperature_note}
            for field, name in (("heap_free_bytes", "heap_free_min_bytes"), ("heap_largest_bytes", "heap_largest_min_bytes"),
                                ("heap_min_bytes", "heap_boot_low_water_bytes"), ("audio_stack_min_bytes", "audio_stack_low_water_bytes"),
                                ("loop_stack_min_bytes", "loop_stack_low_water_bytes")):
                values = [record[field] for record in sample_values if isinstance(record.get(field), (float, int))]
                row[name] = min(values) if values else None
            for core in range(2):
                supported = [record for record in sample_values if record.get(f"core{core}_busy_pct") is not None]
                total = sum(record[f"cpu{core}_total_ticks"] for record in supported)
                busy = sum(record[f"cpu{core}_busy_ticks"] for record in supported)
                row[f"core{core}_weighted_busy_pct"] = 100 * busy / total if total else None
            scenes.append(row)
        metric_groups = collections.defaultdict(list)
        for run in runs:
            for record in run.get("target", []):
                if record.get("type") == "metric":
                    metric_groups[record["name"]].append(record)
                elif record.get("type") == "kernel":
                    metric_groups["__kernel"].append(record)
                elif record.get("type") == "spi":
                    metric_groups["__spi"].append(record)
        for name, records in metric_groups.items():
            if name == "__kernel":
                actual = [sample for record in records for sample in record["samples_us"]]
                times = distribution(actual)
                first = records[0]
                output_bytes = first["output_bytes"]
                kernels.append({"workload": workload, "scope": scope, "kernel": first["kernel"], "large": first["large"],
                    "repetitions": len(records), "samples": times["samples"], "median_us": times["median"],
                    "p95_us": times["p95"], "p99_us": times["p99"], "max_us": times["max"],
                    "working_bytes": first["working_bytes"], "output_bytes": output_bytes,
                    "median_compute_us_per_output_byte": times["median"] / output_bytes if output_bytes else None,
                    "checksum_consistent_across_repetitions": len({record["checksum_fnv1a"] for record in records}) == 1,
                    "offload_status": "CANDIDATE ONLY: input size, remote compute, actual goodput and RTT not measured"})
            elif name == "__spi":
                actual = [sample for record in records for sample in record["samples_us"]]
                times = distribution(actual)
                spi.append({"workload": workload, "scope": scope, "repetitions": len(records), "samples": times["samples"],
                    "bytes_per_sample": records[0]["bytes_per_sample"], "median_us": times["median"],
                    "p95_us": times["p95"], "p99_us": times["p99"], "max_us": times["max"],
                    "median_goodput_bytes_per_s": records[0]["bytes_per_sample"] * 1e6 / times["median"]
                    if times["median"] else None})
            else:
                bins = [sum(record["histogram"][index] for record in records) for index in range(32)]
                count = sum(bins)
                observed = [record for record in records if record["count"] > 0]
                metrics.append({"workload": workload, "scope": scope, "name": name, "unit": records[0]["unit"],
                    "repetitions": len(records), "count": count, "mean": sum(record["sum"] for record in records) / count if count else None,
                    "sum": sum(record["sum"] for record in records),
                    "min": min(record["min"] for record in observed) if observed else None,
                    "max": max(record["max"] for record in observed) if observed else None,
                    "p50_upper": histogram_bound(bins, 50), "p95_upper": histogram_bound(bins, 95),
                    "p99_upper": histogram_bound(bins, 99), "percentile_method": "pooled_ceil_log2_histogram_upper_bounds"})
    return {"schema": "cyd-perf-summary/1", "source_sha": result["source_sha"],
            "firmware_sha256": result["firmware_sha256"], "profile": result["profile"],
            "evidence_class": result["evidence_class"], "scenes": scenes, "metrics": metrics,
            "kernels": kernels, "spi": spi, "missing": missing,
            "offload_break_even": "local_compute_us > remote_compute_us + pack_unpack_us + measured_matched_input_output_transport_us + remote_queue_us",
            "offload_transport_model": "A model may use separately estimated fixed message overhead + input_bytes/up_goodput + output_bytes/down_goodput. Do not add payload transmission again to a full matched-payload RTT.",
            "offload_limitation": "Kernel output-only ratios do not include input/state upload or remote computation and cannot establish wireless benefit alone."}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=pathlib.Path)
    args = parser.parse_args()
    result = json.loads(args.results.read_text(encoding="utf-8"))
    if result.get("schema") != "cyd-perf-capture/1":
        raise ValueError("Unsupported capture schema")
    summary = summarize(result)
    output = args.results.parent
    (output / "summary.json").write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    for category in ("scenes", "metrics", "kernels", "spi", "missing"):
        write_csv(output / f"summary-{category}.csv", summary[category])
    print(output / "summary.json")


if __name__ == "__main__":
    main()
