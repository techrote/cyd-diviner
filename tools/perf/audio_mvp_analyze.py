#!/usr/bin/env python3
"""Analyze finite digital packet-stream evidence offline; never access COM ports."""
from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import os
import pathlib

from audio_mvp_capture import AUDIO_CONFIG_FIELDS, validate_plan, validate_run
from capture import percentile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def numeric_range(values):
    return {"n": len(values), "median": percentile(values, 50), "min": min(values) if values else None,
            "max": max(values) if values else None}


def context(result, run):
    identities = {name: {key: endpoint.get("identity", {}).get(key) for key in
        ("source_sha", "source_digest", "firmware_sha256", "profile", "board")} for name, endpoint in result["endpoints"].items()}
    actual = {role: {key: run.get(role + "_summary", {}).get(key) for key in AUDIO_CONFIG_FIELDS} for role in ("sender", "receiver")}
    scope = {"settings": run["settings"], "duration_ms": result["plan"].get("duration_ms", 3000),
             "seed": result["plan"].get("seed", 23063), "identities": identities, "actual_config": actual,
             "metadata_policy": run.get("metadata_policy", result.get("metadata_policy")), "local_context": run.get("local_context")}
    return hashlib.sha256(json.dumps(scope, sort_keys=True, separators=(",", ":")).encode()).hexdigest()[:12], scope


def analyze(result):
    if result.get("schema") != "cyd-audio-mvp-capture/1" or result.get("evidence_class") != "physical_measured":
        raise ValueError("Expected physical audio packet-stream capture, not offline model evidence")
    validate_plan(result["plan"])
    rows, groups = [], collections.defaultdict(list)
    for run in result["runs"]:
        scope_id, scope = context(result, run)
        row = {"run_id": run["run_id"], "workload": run["workload"], "status": run["status"],
               "analysis_status": run["status"], "scope": scope_id, "repetition": run["repetition"], "settings": run["settings"],
               "error": run.get("error"), "sender_summary": run.get("sender_summary"), "receiver_summary": run.get("receiver_summary")}
        row["outside_window_temperature"] = run.get("outside_window_temperature", {"status": "NOT MEASURED"})
        row["metadata_policy"] = run.get("metadata_policy", result.get("metadata_policy"))
        row["deferred_start_proof"] = run.get("deferred_start_proof")
        if run["status"] == "MEASURED":
            try:
                row["derived"] = validate_run(run, result["plan"].get("duration_ms", 3000), result["plan"].get("seed", 23063))
                groups[scope_id].append(row)
            except (ValueError, RuntimeError, KeyError, TypeError) as error:
                row.update(analysis_status="INVALID", validation_error=str(error))
        row["context"] = scope
        rows.append(row)
    aggregated = []
    for scope_id, repetitions in groups.items():
        fields = ("source_rendered_fraction", "source_nonconcealed_rendered_fraction", "observed_concealed_fraction",
                  "accepted_not_admitted_fraction", "nonconcealed_pcm_payload_bytes_per_s", "completed_encoded_payload_bytes_per_s")
        aggregated.append({"scope": scope_id, "workload": repetitions[0]["workload"], "repetitions": len(repetitions),
            "context": repetitions[0]["context"], **{key: numeric_range([row["derived"][key] for row in repetitions]) for key in fields}})
    planned = len(result["plan"]["workloads"]) * result["plan"].get("repetitions", 3)
    return {"schema": "cyd-audio-mvp-analysis/1", "evidence_class": "physical_measured", "capture_status": result["status"],
            "planned_repetitions": planned, "attempted_repetitions": len(rows), "not_attempted_repetitions": max(0, planned - len(rows)),
            "run_status_counts": dict(collections.Counter(row["status"] for row in rows)),
            "analysis_status_counts": dict(collections.Counter(row["analysis_status"] for row in rows)),
            "source_harness_sha256": result.get("harness_sha256"), "endpoints": result["endpoints"],
            "metadata_policy": result.get("metadata_policy"),
            "runs": rows, "groups": aggregated, "limitations": result.get("limitations", [])}


def display(value, decimals=3, scale=1):
    return "—" if value is None else f"{value / scale:.{decimals}f}"


def markdown(summary, source_link, input_hash):
    lines = ["# Finite audio packet-stream evidence", "",
        f"Capture status **{summary['capture_status']}**; {summary['attempted_repetitions']}/{summary['planned_repetitions']} attempted repetitions. "
        f"Statuses `{summary['run_status_counts']}`; analysis validation `{summary['analysis_status_counts']}`.", "",
        f"[Raw structured source]({source_link}), SHA256 `{input_hash}`. Per-board redacted RX/target/event logs remain separate raw evidence.", ""]
    if (summary.get("metadata_policy") or {}).get("mode") == "deferred_radio_metadata/1":
        lines += ["This is the explicit deferred-metadata cohort. Matching successful start ACKs are required, with only the documented post-ACK formatter failure permitted. "
            "Both radios stop before export. Active channel/power-save readback is **NOT MEASURED**; checked SDK setup is inferred from the ACK/source and later PHY API status. "
            "Post-stop info describes released radios, and last_error/operation describe post-stop command status. Packet/PLC/error counters retain the finite workload observations.", ""]
    for name, endpoint in summary["endpoints"].items():
        identity = endpoint.get("identity", {})
        lines += [f"- **{name}**: `{identity.get('profile')}`; source `{identity.get('source_sha')}`; "
                  f"tree `{identity.get('source_digest')}`; image `{identity.get('firmware_sha256')}`."]
    lines += ["", "These are synthetic-source digital reconstruction measurements over ESP-NOW broadcast packets. "
        "Packetized PDM, when selected, is an optional comparison and does not implement the requested proprietary SDR TX/RX. "
        "There is no I2S/analog-output or synchronized one-way latency claim. PHY values are requests accepted by the firmware API, not over-air rate readback.", "",
        "| Run | Direction / codec / Hz / channels / frames / PHY request | capture / analysis | source rendered / nonconcealed % | PLC of rendered % | accepted TX / admitted RX | nonconcealed PCM B/s |",
        "| --- | --- | --- | --- | --- | --- | --- |"]
    for row in summary["runs"]:
        case, derived = row["settings"], row.get("derived", {})
        label = f"{case['sender']}→{case['receiver']} {case['codec']} {case['sample_hz']} {case['channels']}ch {case['frames_per_packet']}f {case['rate_mbps']}M"
        percent = lambda key: display(100 * derived[key] if key in derived else None, 2)
        lines.append(f"| {row['run_id']} | {label} | {row['status']} / {row['analysis_status']} | "
            f"{percent('source_rendered_fraction')} / {percent('source_nonconcealed_rendered_fraction')} | {percent('observed_concealed_fraction')} | "
            f"{derived.get('sender_api_accepted_packets', '—')} / {derived.get('receiver_accepted_packets', '—')} | {display(derived.get('nonconcealed_pcm_payload_bytes_per_s'), 1)} |")
    lines += ["", "Coverage uses the complete scheduled source block count. Quality sums cover rendered blocks, including PLC; "
        "source blocks skipped before the receiver anchor or during obsolete-work discard remain coverage loss, not silent zero-error samples. "
        "TX API acceptance minus admitted RX fragments includes radio, callback queue, age/deadline and stale/future-window effects; it is not isolated RF loss.", "",
        "| Run | source skipped / expired prep | RX queue drops / expired callback | stale / future / CRC | PLC blocks / skipped playout | max callback age / slot dwell / render lateness µs |",
        "| --- | --- | --- | --- | --- | --- |"]
    for row in summary["runs"]:
        tx, rx = row.get("sender_summary") or {}, row.get("receiver_summary") or {}
        field = lambda record, key: record.get(key, "—")
        lines.append(f"| {row['run_id']} | {field(tx,'skipped_source_blocks')} / {field(tx,'expired_prepared_blocks')} | "
            f"{field(rx,'rx_callback_queue_drops')} / {field(rx,'rx_expired_callback_packets')} | "
            f"{field(rx,'rx_stale_packets')} / {field(rx,'rx_future_packets')} / {field(rx,'rx_crc_errors')} | "
            f"{field(rx,'concealed_quality_blocks')} / {field(rx,'rx_skipped_playout_blocks')} | "
            f"{field(rx,'rx_callback_age_max_us')} / {field(rx,'slot_dwell_max_us')} / {field(rx,'rx_render_lateness_max_us')} |")
    lines += ["", "The age/dwell/lateness values above use receiver-local scheduling. Elapsed encode/receive/render/send and quality-observer costs include preemption; "
        "their maxima are observed samples, not worst-case execution-time guarantees. Exact call counts, sums, callback outcomes, in-flight state and active-window heap minima remain in JSON/CSV.", "",
        "| Run / channel | observed samples | unaligned digital error RMS / max (PCM16 LSB) | SNR dB | correlation |",
        "| --- | --- | --- | --- | --- |"]
    for row in summary["runs"]:
        for quality in row.get("derived", {}).get("digital_quality", []):
            lines.append(f"| {row['run_id']} / {quality['channel']} | {quality['samples']} | "
                f"{display(quality['error_rms_pcm16'])} / {quality['error_abs_max_pcm16']} | {display(quality['snr_db'])} | — |")
    lines += ["", "Zero observed error has unbounded mathematical SNR and is left unavailable rather than replaced by a finite number. "
        "Correlation is unavailable because the target exports energy/error sums without cross-moments. Reference comparison is unshifted and sequence-aligned; no fitting removes packet/PLC errors.", "",
        "Only successful, validated repetitions with matching requested settings, actual codec/queue/PHY configuration and both source/image identities enter grouped min/median/max summaries. "
        "Failed sessions and unattempted repetitions retain their status. The offline continuous-PDM model has its own evidence class and artifacts.", ""]
    failures = [row for row in summary["runs"] if row["analysis_status"] != "MEASURED"]
    for row in failures:
        lines.append(f"- `{row['run_id']}`: {row.get('validation_error') or row.get('error') or row['analysis_status']}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    output = args.output or args.results.parent
    output.mkdir(parents=True, exist_ok=True)
    summary = analyze(json.loads(args.results.read_text(encoding="utf-8-sig")))
    source_hash = digest(args.results)
    summary["source_results_sha256"] = source_hash
    (output / "audio-summary.json").write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    rows = [{"run_id": row["run_id"], "workload": row["workload"], "status": row["status"], "analysis_status": row["analysis_status"],
             "scope": row["scope"], **row["settings"], **{key: value for key, value in row.get("derived", {}).items() if not isinstance(value, (list, dict))}}
            for row in summary["runs"]]
    fields = list(dict.fromkeys(key for row in rows for key in row)) or ["run_id", "status"]
    with (output / "audio-summary.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    link = pathlib.Path(os.path.relpath(args.results, output)).as_posix()
    (output / "AUDIO-REPORT.md").write_text(markdown(summary, link, source_hash), encoding="utf-8")
    names = ("audio-summary.json", "audio-summary.csv", "AUDIO-REPORT.md")
    manifest = {"schema": "cyd-audio-mvp-analysis-manifest/1", "capture_status": summary["capture_status"],
                "inputs": {str(args.results): source_hash}, "tool_sha256": digest(pathlib.Path(__file__)),
                "capture_validator_sha256": digest(pathlib.Path(__file__).with_name("audio_mvp_capture.py")),
                "outputs": {name: digest(output / name) for name in names}}
    (output / "audio-analysis-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return 2 if summary["analysis_status_counts"].get("INVALID") else 0


if __name__ == "__main__":
    raise SystemExit(main())
