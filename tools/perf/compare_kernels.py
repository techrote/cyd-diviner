#!/usr/bin/env python3
"""Compare independently captured portable kernels, preserving board contexts."""
import argparse
import collections
import csv
import hashlib
import json
import pathlib

from capture import percentile

MATCH_FIELDS = ("algorithm", "precision", "work_unit", "seed", "large", "warmup", "samples",
                "working_bytes", "output_bytes", "work_units_per_step")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(path):
    source = json.loads(path.read_text(encoding="utf-8"))
    if source.get("schema") != "cyd-perf-capture/1" or source.get("evidence_class") != "physical_measured":
        raise ValueError("Expected physical portable capture records")
    if source["status"] == "IN PROGRESS":
        raise ValueError("Do not compare an open capture")
    groups = collections.defaultdict(list)
    for run in source["runs"]:
        if run.get("status") != "MEASURED":
            continue
        records = [record for record in run.get("target", []) if record.get("type") == "kernel"]
        if len(records) != 1:
            continue
        record = records[0]
        samples = record.get("samples_us", [])
        if len(samples) != 256 or record.get("samples") != 256 or record.get("finite") is not True:
            raise ValueError("Invalid retained kernel samples/output")
        region = record.get("memory_region", run.get("settings", {}).get("memory_region", "UNKNOWN"))
        groups[(record["kernel"], record["large"], region)].append({"run_id": run["run_id"], "settings": run.get("settings", {}), "record": record})
    observed = []
    for (kernel, large, region), records in groups.items():
        first = records[0]["record"]
        if any(any(item["record"].get(field) != first.get(field) for field in MATCH_FIELDS) for item in records):
            raise ValueError("Workload metadata varies within one kernel/region")
        timings = [sample for item in records for sample in item["record"]["samples_us"]]
        observed.append({"kernel": kernel, "large": large, "memory_region": region, "repetitions": len(records), "n": len(timings),
            "median_us": percentile(timings, 50), "p95_us": percentile(timings, 95), "p99_us": percentile(timings, 99), "max_us": max(timings),
            "workload": {field: first.get(field) for field in MATCH_FIELDS},
            "workspace_allocated_bytes": first.get("workspace_allocated_bytes"),
            "memory_receipts": [{"run_id": item["run_id"], "memory_region_method": item["settings"].get("memory_region_method"),
                "memory_caps": item["record"].get("memory_caps"), "memory_requested": item["record"].get("memory_requested")} for item in records],
            "outputs": [{key: item["record"].get(key) for key in ("checksum_fnv1a", "output_min", "output_max", "output_sum", "finite")} for item in records]})
    info = next((record for record in source.get("target_info", []) if record.get("type") == "info"), {})
    metadata = {key: source.get(key) for key in ("status", "capture_kind", "source_sha", "source_digest", "source_tree_state", "firmware_sha256", "profile", "board", "limitations", "error", "harness_sha256", "capture_module_sha256", "usb_progress_poll", "parse_errors")}
    states = [record for run in source["runs"] if run.get("status") == "MEASURED" for record in run.get("target", []) if record.get("type") == "state"]
    counters = {}
    for field in ("telemetry_write_failures", "telemetry_short_writes"):
        values = [record[field] for record in states if isinstance(record.get(field), int)]
        counters[field] = {"first": values[0] if values else None, "last": values[-1] if values else None,
                           "delta": values[-1] - values[0] if values else None, "observations": len(values)}
    recovery_events = []
    event_path = path.parent / "serial-events.jsonl"
    if event_path.is_file():
        events = [json.loads(line) for line in event_path.read_text(encoding="utf-8").splitlines()]
        for index, event in enumerate(events):
            if event.get("event") != "usb_progress_poll":
                continue
            following = next((value for value in events[index + 1:] if value.get("event") == "rx"), None)
            recovery_events.append({**event, "first_subsequent_rx_bytes": following.get("bytes") if following else None,
                "host_seconds_to_first_subsequent_rx": following["host_elapsed_s"] - event["host_elapsed_s"] if following else None})
    metadata.update(input_sha256=digest(path), input_path=path.as_posix(), target_info=info,
                    measured_cases=sum(run.get("status") == "MEASURED" for run in source["runs"]),
                    link_baseline=source.get("link_baseline"), telemetry_counter_observations=counters,
                    usb_progress_recoveries=recovery_events, usb_progress_recovery_count=len(recovery_events))
    return {"metadata": metadata, "kernels": observed}


def compare(left, right):
    left_index = {(entry["kernel"], entry["large"]): entry for entry in left["kernels"] if entry["memory_region"] == "internal"}
    comparisons = []
    for target in right["kernels"]:
        baseline = left_index.get((target["kernel"], target["large"]))
        row = {"kernel": target["kernel"], "large": target["large"], "s3_memory_region": target["memory_region"],
               "cyd": baseline, "s3": target, "status": "NO MATCHING CYD CASE"}
        if baseline:
            mismatches = [field for field in MATCH_FIELDS if baseline["workload"][field] != target["workload"][field]]
            row["metadata_mismatches"] = mismatches
            row["status"] = "MATCHED WORKLOAD" if not mismatches else "NOT COMPARABLE: workload differs"
            if not mismatches:
                row["cyd_median_div_s3_median"] = baseline["median_us"] / target["median_us"] if target["median_us"] else None
                row["cyd_p99_div_s3_p99"] = baseline["p99_us"] / target["p99_us"] if target["p99_us"] else None
                hashes = lambda item: sorted({output["checksum_fnv1a"] for output in item["outputs"]})
                row["cyd_output_fingerprints"], row["s3_output_fingerprints"] = hashes(baseline), hashes(target)
                row["repetition_fingerprint_stable"] = len(hashes(baseline)) == len(hashes(target)) == 1
                row["fingerprints_match"] = hashes(baseline) == hashes(target)
                row["numeric_summary_differences"] = {}
                for field in ("output_min", "output_max", "output_sum"):
                    left_values = {output[field] for output in baseline["outputs"]}
                    right_values = {output[field] for output in target["outputs"]}
                    if len(left_values) != 1 or len(right_values) != 1:
                        row["numeric_summary_differences"][field] = {"status": "INCONSISTENT ACROSS REPETITIONS"}
                    else:
                        a, b = next(iter(left_values)), next(iter(right_values))
                        row["numeric_summary_differences"][field] = {"cyd": a, "s3": b, "absolute_difference": abs(b - a),
                            "relative_to_abs_cyd": abs(b - a) / abs(a) if a else 0 if b == 0 else None}
        comparisons.append(row)
    return comparisons


def markdown(document):
    f = lambda value: "—" if value is None else f"{value:.3f}" if isinstance(value, float) else str(value)
    left, right = document["cyd"]["metadata"], document["s3"]["metadata"]
    lines = ["# Physical S3 and classic CYD kernel comparison", "",
        f"CYD capture: **{left['status']}**, {left['measured_cases']} valid cases. S3 capture: **{right['status']}**, {right['measured_cases']} valid cases. "
        "Only completed, metadata-matched workload cases enter timing ratios.", ""]
    for label, source in (("CYD", left), ("S3", right)):
        info = source["target_info"]
        lines += [f"* **{label}** `{source['profile']}`: source `{source['source_sha']}`, firmware `{source['firmware_sha256']}`, "
            f"raw input SHA256 `{source['input_sha256']}`; CPU {info.get('cpu_mhz')} MHz, flash {info.get('flash_hz')} Hz, initialized PSRAM {info.get('psram_bytes')} bytes."]
    lines += ["", "Both CPUs were configured at 240 MHz, but flash is 40 MHz on the CYD and 80 MHz on this S3 profile. "
        "The CYD is the Diviner profiling image with its background DAC/audio task and observer arrays; its frame loop pauses during each kernel command. "
        "The S3 is standalone, without the ordinary audio/display workers, with the idle peer worker/USB buffer present. "
        "These are observed configured-system comparisons, not a controlled isolation of CPU/FPU or architectural advantage. "
        "Internal and PSRAM results remain separate, with exact capability versus inferred-zero-PSRAM receipts in JSON.", "",
        "Each completed repetition has three warmups, reinitialization from seed, and 256 exact timed steps. "
        "The table pools exact timings across matching repetitions. Initialization, allocation, yields and output inspection are excluded; RTOS preemption inside a step is included. "
        "A ratio above 1 means the S3's observed median is lower in these contexts, not a promised application speedup.", "",
        "| Kernel / size | S3 region | reps CYD/S3 | samples CYD/S3 | median CYD/S3 µs | p99 CYD/S3 µs | observed median ratio | fingerprints match |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    for row in document["comparisons"]:
        a, b = row.get("cyd") or {}, row["s3"]
        lines.append(f"| {row['kernel']}/{'large' if row['large'] else 'realistic'} | {row['s3_memory_region']} | "
            f"{f(a.get('repetitions'))}/{b['repetitions']} | {f(a.get('n'))}/{b['n']} | {f(a.get('median_us'))}/{b['median_us']} | "
            f"{f(a.get('p99_us'))}/{b['p99_us']} | {f(row.get('cyd_median_div_s3_median'))} | {row.get('fingerprints_match', row['status'])} |")
    lines += ["", "## Independent output checks", "",
        "The comparison independently checks algorithm/precision/seed, size, work units, warmup and sample count before producing a ratio. "
        "It also compares every repetition's final fingerprint and output min/max/sum, reporting absolute and relative numeric-summary differences in JSON/CSV. "
        "A 32-bit fingerprint is not a cryptographic proof of complete numerical equivalence. Matching summaries do not bound every sample's error. "
        "Cross-chip float differences require the separate host double-precision/DFT correctness evidence; no tolerance is invented here.", "",
        "## USB output recovery and observer scope", "",
        f"The completed S3 capture records **{right['usb_progress_recovery_count']}** bounded post-record progress queries and "
        f"**{len(right.get('parse_errors') or [])}** parse errors. Exact host tool digests: harness `{right.get('harness_sha256')}`, "
        f"shared capture module `{right.get('capture_module_sha256')}`.", ""]
    for field, observations in right["telemetry_counter_observations"].items():
        lines += [f"* `{field}`: first {observations['first']}, last {observations['last']}, observed delta {observations['delta']} across {observations['observations']} state records."]
    for event in right["usb_progress_recoveries"]:
        lines += [f"* Progress query while awaiting `{event.get('awaited_record')}`: RX offset {event.get('rx_offset')}, partial record {event.get('pending_bytes')} bytes, "
            f"no-RX interval {event.get('stalled_for_s'):.3f} s; subsequent RX {event.get('first_subsequent_rx_bytes')} bytes after "
            f"{event.get('host_seconds_to_first_subsequent_rx'):.3f} host seconds."]
    lines += ["", "The optional query is sent only after the expected record's type tag has appeared. Kernel timing steps have finished before that record is emitted. "
        "No query is sent while a kernel is still computing with an empty pending buffer. Raw partial bytes are retained; malformed JSON fails rather than being discarded. "
        "This successful recovery is evidence for the bounded workaround in this capture, not proof of the underlying native-USB fault or a general firmware fix. "
        "Lifetime counters may already be nonzero at the first snapshot; their observed delta must not be substituted for an all-time zero-error claim.", "",
        "## Earlier captures and memory identity", ""]
    for earlier in document["earlier"]:
        metadata = earlier["metadata"]
        lines += [f"* `{metadata['input_path']}`: **{metadata['status']}**, {metadata['measured_cases']} valid cases; "
            f"recorded failure `{metadata.get('error')}`. Original JSON/hash and status remain unchanged."]
    lines += ["", "The first peer-image attempt used a prior profile's initialized-PSRAM heap total (2,095,103 bytes). "
        "The new peer profile reports 2,095,087 bytes. The profile-specific receipt records that API total while retaining the physically identified 2 MiB nominal PSRAM package. "
        "That 16-byte heap-accounting difference is not evidence of a changed physical memory capacity. "
        "The rejected identity-only attempt supplies no performance result. The earlier USB-partial run remains a separate failed snapshot.", "",
        "## Limits for VIBEWARE", "",
        "Use the complete row's measured tails and actual memory placement when selecting local kernels. Do not transfer these step timings directly to fullscreen rendering or a 128-sample DAC deadline. "
        "No wireless speedup is established here: input/state transfer, actual link RTT/loss/goodput and remote scheduling must also fit the intended cadence. "
        "Temperature indications use different chip APIs and are not calibrated cross-chip thermometry. Physical C3 execution remains unmeasured.", "",
        "Artifacts: [structured comparison](S3-COMPARISON.json), [flat numeric table](S3-COMPARISON.csv). "
        "Exact source/image/profile, timing samples, allocation capability receipts and failed streams remain in the separately archived physical captures.", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cyd", type=pathlib.Path, required=True)
    parser.add_argument("--s3", type=pathlib.Path, required=True)
    parser.add_argument("--earlier", action="append", type=pathlib.Path, default=[])
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    left, right = inventory(args.cyd), inventory(args.s3)
    if right["metadata"]["status"] != "CAPTURE COMPLETE":
        raise ValueError("This comparison requires the requested completed S3 matrix")
    document = {"schema": "cyd-perf-s3-comparison/1", "evidence_class": "physical_measured", "cyd": left, "s3": right,
        "earlier": [inventory(path) for path in args.earlier], "comparisons": compare(left, right),
        "tool_sha256": digest(pathlib.Path(__file__))}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "S3-COMPARISON.json").write_text(json.dumps(document, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    rows = []
    for row in document["comparisons"]:
        first, second = row.get("cyd") or {}, row["s3"]
        flat = {"kernel": row["kernel"], "large": row["large"], "s3_memory_region": row["s3_memory_region"], "status": row["status"],
            "cyd_source_sha": left["metadata"]["source_sha"], "cyd_firmware_sha256": left["metadata"]["firmware_sha256"],
            "s3_source_sha": right["metadata"]["source_sha"], "s3_firmware_sha256": right["metadata"]["firmware_sha256"],
            "median_ratio_cyd_div_s3": row.get("cyd_median_div_s3_median"), "fingerprints_match": row.get("fingerprints_match")}
        for prefix, sample in (("cyd", first), ("s3", second)):
            flat.update({prefix + "_" + key: sample.get(key) for key in ("repetitions", "n", "median_us", "p95_us", "p99_us", "max_us")})
        for field, difference in row.get("numeric_summary_differences", {}).items():
            flat[field + "_absolute_difference"] = difference.get("absolute_difference")
            flat[field + "_relative_difference"] = difference.get("relative_to_abs_cyd")
        rows.append(flat)
    with (args.output / "S3-COMPARISON.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(dict.fromkeys(key for row in rows for key in row)))
        writer.writeheader()
        writer.writerows(rows)
    (args.output / "S3-COMPARISON.md").write_text(markdown(document), encoding="utf-8")
    print(args.output / "S3-COMPARISON.md")


if __name__ == "__main__":
    main()
