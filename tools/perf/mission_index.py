#!/usr/bin/env python3
"""Index terminal physical capture results without promoting failed experiments."""
from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import pathlib
from datetime import datetime, timezone

SCHEMAS = {
    "cyd-perf-capture/1", "cyd-perf-link-capture/1", "cyd-audio-mvp-capture/1",
    "cyd-sdr-capture/1", "cyd-sdr-metal-capture/1", "cyd-sdr-metal-dual-capture/1",
    "cyd-metal-gain-setup-diagnostic/1", "cyd-metal-readback-recovery/1",
}
ACTIVE = {"IN PROGRESS", "IN_PROGRESS", "RUNNING"}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def identity(entry):
    expected = entry.get("expected_info", {})
    return {key: expected.get(key, entry.get(key)) for key in
            ("board", "port", "profile", "source_sha", "source_digest", "tx_source_digest", "firmware_sha256", "mac")}


def identities(result):
    if result.get("identities"):
        return {name: identity(entry) for name, entry in result["identities"].items() if name != "schema"}
    if result.get("endpoints"):
        return {name: identity(entry["identity"]) for name, entry in result["endpoints"].items()}
    if result.get("devices"):
        return {name: identity(entry["identity"]) for name, entry in result["devices"].items()}
    if result.get("identity"):
        return {"target": identity(result["identity"])}
    return {"target": identity(result)} if result.get("source_sha") else {}


def build_index(captures, metadata, utc):
    summaries, rows, inputs = [], [], {}
    for path in sorted(captures.glob("*/results.json")):
        result = json.loads(path.read_text(encoding="utf-8-sig"))
        schema, status, name = result.get("schema"), result.get("status"), path.parent.name
        if schema not in SCHEMAS or status in ACTIVE or not isinstance(status, str):
            raise ValueError(f"Unknown or unfinished capture: {name}: {schema}/{status}")
        source_hash = digest(path)
        inputs[name + "/results.json"] = source_hash
        endpoints = identities(result)
        counts = collections.Counter(run.get("status") for run in result.get("runs", []))
        summary = {"name": name, "schema": schema, "status": status,
                   "results_sha256": source_hash, "runs": dict(counts),
                   "endpoint_identities": endpoints, "error": result.get("error"),
                   "diagnostic_only": schema in {"cyd-metal-gain-setup-diagnostic/1", "cyd-metal-readback-recovery/1"},
                   "complete_shared_sdr_tx_exposures": 0,
                   "independently_valid_receiver_bodies": 0}
        summaries.append(summary)
        for ordinal, run in enumerate(result.get("runs", []), 1):
            state, settings = run.get("status"), run.get("settings", {})
            complete = state in ("MEASURED", "ACQUIRED")
            receiver_states = {key: value.get("status") for key, value in run.get("receivers", {}).items()}
            receiver_count = sum(value == "ACQUIRED" for value in receiver_states.values())
            shared = complete and (
                schema == "cyd-sdr-capture/1" or
                schema in {"cyd-sdr-metal-capture/1", "cyd-sdr-metal-dual-capture/1"} and settings.get("tx_enabled") is True)
            summary["complete_shared_sdr_tx_exposures"] += bool(shared)
            summary["independently_valid_receiver_bodies"] += receiver_count
            row = {"capture": name, "capture_status": status, "capture_schema": schema,
                   "ordinal": ordinal, "run_id": run.get("run_id"), "workload": run.get("workload"),
                   "repetition": run.get("repetition"), "status": state,
                   "profile_status": run.get("profile_status"), "error": run.get("error") or run.get("failures"),
                   "source_sha": result.get("source_sha"), "firmware_sha256": result.get("firmware_sha256"),
                   "cyd_source_sha": endpoints.get("cyd", {}).get("source_sha"),
                   "cyd_firmware_sha256": endpoints.get("cyd", {}).get("firmware_sha256"),
                   "peer_source_sha": endpoints.get("peer", {}).get("source_sha"),
                   "peer_firmware_sha256": endpoints.get("peer", {}).get("firmware_sha256"),
                   "complete_workload_window": complete,
                   "separately_complete_local_profile": run.get("profile_status") == "MEASURED",
                   "unfinished_raw_row_in_closed_failed_capture": state in ACTIVE,
                   "complete_shared_sdr_tx_exposure": bool(shared),
                   "independently_valid_receiver_count": receiver_count,
                   "receiver_statuses": receiver_states, "endpoint_identities": endpoints,
                   "metadata_policy": run.get("metadata_policy", result.get("metadata_policy")),
                   "results_sha256": source_hash}
            rows.append(row)
    indexed = {**metadata, "schema": "cyd-perf-mission-status/3", "utc": utc,
               "captures": summaries, "runs": rows,
               "index_scope": "Raw terminal statuses are preserved. MEASURED/ACQUIRED describes the original schema's validated window, not RF provenance, BER or latency. A dual SDR row is one shared exposure; receiver bodies are separate observations, not extra independent transmissions. Failed and unfinished rows never become complete windows. Setup diagnostics and USB re-exports add no RF exposure. Image/source identities still require independent flash receipts."}
    return indexed, inputs


def write_index(output, indexed, inputs, metadata_path, replace=False):
    output.mkdir(parents=True, exist_ok=True)
    names = ("mission-status.json", "measurement-status.csv", "mission-index-manifest.json")
    if not replace and any((output / name).exists() for name in names):
        raise FileExistsError("Existing index: use --replace only for the derived index files")
    (output / names[0]).write_text(json.dumps(indexed, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    fields = list(indexed["runs"][0]) if indexed["runs"] else ["capture", "status"]
    with (output / names[1]).open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields); writer.writeheader()
        writer.writerows({key: json.dumps(value, sort_keys=True, separators=(",", ":")) if isinstance(value, (dict, list)) else value
                          for key, value in row.items()} for row in indexed["runs"])
    manifest = {"schema": "cyd-perf-mission-index-manifest/1", "evidence_class": "DERIVED_CAPTURE_INDEX",
                "metadata_sha256": digest(metadata_path), "tool_sha256": digest(pathlib.Path(__file__)),
                "capture_results_sha256": inputs,
                "outputs": {name: digest(output / name) for name in names[:2]}}
    (output / names[2]).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--captures", type=pathlib.Path, required=True)
    parser.add_argument("--metadata", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--replace", action="store_true")
    args = parser.parse_args()
    metadata = json.loads(args.metadata.read_text(encoding="utf-8-sig"))
    forbidden = {"captures", "runs", "schema", "utc", "index_scope"}
    if forbidden.intersection(metadata): raise ValueError("Metadata must not contain generated index fields")
    indexed, inputs = build_index(args.captures, metadata, datetime.now(timezone.utc).isoformat())
    write_index(args.output, indexed, inputs, args.metadata, args.replace)
    print(json.dumps({"captures": len(indexed["captures"]), "rows": len(indexed["runs"]),
                      "status_counts": dict(collections.Counter(row["status"] for row in indexed["runs"]))}))


if __name__ == "__main__": main()
