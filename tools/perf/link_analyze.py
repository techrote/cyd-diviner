#!/usr/bin/env python3
"""Offline analysis of redacted, owned-board wireless capture records."""
from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import os
import pathlib
import re
import statistics

from capture import percentile, validate_end
from link_capture import derive_link


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def value_range(values):
    values = [value for value in values if isinstance(value, (int, float))]
    return {"n": len(values), "median": statistics.median(values) if values else None, "min": min(values) if values else None,
            "max": max(values) if values else None}


def clean_receipt(record):
    return {key: value for key, value in record.items() if key not in ("host_received_elapsed_s", "run_id", "schema", "type")}


def scope(result, run):
    case = run["settings"]
    endpoints = {}
    for name, endpoint in result["endpoints"].items():
        identity = endpoint.get("identity", {})
        info = next((record for record in endpoint.get("target_info", []) if record.get("type") == "info"), {})
        topology = run.get("topology_info", {}).get(name, {})
        post = run.get("post_info", {}).get(name, topology)
        connection = run.get("connection_info", {}).get(name, {})
        endpoints[name] = {"source_sha": identity.get("source_sha"), "source_digest": identity.get("source_digest"),
            "firmware_sha256": identity.get("firmware_sha256"), "profile": identity.get("profile"),
            "chip": info.get("chip"), "cpu_mhz": info.get("cpu_mhz"), "flash_hz": info.get("flash_hz"),
            "psram_bytes": info.get("psram_bytes"),
            "channel_before": topology.get("actual_channel"), "channel_after": post.get("actual_channel"),
            "power_save_enum": topology.get("power_save_enum"),
            "ble_parameters": {key: connection.get(key) for key in ("ble_mtu", "ble_actual_payload_limit",
                "ble_actual_interval_units_1_25ms", "ble_actual_latency", "ble_actual_timeout_units_10ms", "ble_conn_params_valid")}}
    base = {"topology": case["topology"], "local_workload": case["local_workload"], "endpoints": endpoints,
            "warmup_ms": result["plan"].get("warmup_ms", 2000), "duration_ms": result["plan"].get("duration_ms", 10000),
            "reply_drain_ms": result["plan"].get("reply_drain_ms", 700), "seed": result["plan"].get("seed"),
            "actual_scene_settings": [clean_receipt(record) for record in run.get("setup_target", []) if record.get("type") == "scene_settings"]}
    full = {**base, "transport": case["transport"], "pattern": case["pattern"], "payload_bytes": case["payload_bytes"],
            "packets_per_second": case["packets_per_second"], "mode": case["mode"]}
    fingerprint = lambda payload: hashlib.sha256(json.dumps(payload, sort_keys=True, separators=(",", ":")).encode()).hexdigest()[:12]
    return fingerprint(full), fingerprint(base), full


def histogram(records, count_field, bins_field, min_field, max_field, sum_field):
    bins = [0] * 32
    count = total = 0
    maxima, minima = [], []
    for record in records:
        n, values = record.get(count_field, 0), record.get(bins_field, [])
        if len(values) != 32 or sum(values) != n:
            raise ValueError("Histogram observation count mismatch")
        count += n
        total += record.get(sum_field, 0)
        bins = [left + right for left, right in zip(bins, values)]
        if n:
            minima.append(record[min_field])
            maxima.append(record[max_field])
    maximum = max(maxima) if maxima else None
    answer = {"n": count, "mean_us": total / count if count else None, "min_us": min(minima) if minima else None,
              "max_us": maximum, "histogram_counts": bins,
              "percentile_method": "pooled ceil-log2 upper bound capped at exact observed maximum; p99 requires>=100 observations"}
    for pct in (50, 95, 99):
        upper, accumulated = None, 0
        if count and (pct != 99 or count >= 100):
            target = (count * pct + 99) // 100
            for index, observations in enumerate(bins):
                accumulated += observations
                if accumulated >= target:
                    upper = min((1 << index) if index < 31 else 0xFFFFFFFF, maximum)
                    break
        answer[f"p{pct}_us_upper"] = upper
    return answer


def profile_observations(result, run):
    records = run.get("cyd_profile", [])
    if not records:
        return {"status": run.get("profile_status", "NOT MEASURED")}
    end = validate_end(records, run["run_id"], result["plan"].get("duration_ms", 10000))
    samples = [record for record in records if record.get("type") == "sample"]
    metrics = {record["name"]: record for record in records if record.get("type") == "metric"}
    info = next((record for record in result["endpoints"]["cyd"].get("target_info", []) if record.get("type") == "info"), {})
    cores = {}
    for core in (0, 1):
        observed = [record for record in samples if record.get(f"core{core}_busy_pct") is not None]
        busy = sum(record.get(f"cpu{core}_busy_ticks", 0) for record in observed)
        ticks = sum(record.get(f"cpu{core}_total_ticks", 0) for record in observed)
        cores[f"core{core}"] = {"busy_ticks": busy, "total_ticks": ticks, "busy_pct": 100 * busy / ticks if ticks and info.get(f"cpu{core}_hook") is True else None}
    answer = {"status": "MEASURED", "frames": end["frames"], "elapsed_us": end["elapsed_us"],
              "frames_per_second": end["frames"] * 1e6 / end["elapsed_us"], "audio_completed_blocks": end.get("audio_completed_blocks"),
              "audio_status": "OBSERVED COMPLETIONS" if end.get("audio_completed_blocks", 0) else "NO OBSERVED COMPLETIONS",
              "cores": cores, "metrics": metrics,
              "i2s_events": [record for record in records if record.get("type") == "i2s_events"]}
    for field in ("heap_free_bytes", "heap_largest_bytes", "heap_min_bytes", "audio_stack_min_bytes", "loop_stack_min_bytes", "temperature_raw"):
        answer[field] = value_range([record.get(field) for record in samples])
    return answer


def raw_faults(directory):
    observations = []
    if directory is None:
        return observations
    for endpoint in ("cyd", "peer"):
        raw = directory / endpoint / "serial-rx.redacted.bin"
        log = directory / endpoint / "serial-events.redacted.jsonl"
        if not raw.is_file():
            continue
        data = raw.read_bytes()
        events = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()] if log.is_file() else []
        received = [event for event in events if event.get("event") == "rx"]
        for found in re.finditer(rb"Guru Meditation Error:[^\r\n]*|Backtrace:[^\r\n]*|rst:0x[^\r\n]*|Diviner CYD:[^\r\n]*", data):
            line = found.group().decode("ascii", errors="replace")[:512]
            chunk = next((event for event in received if event.get("offset", -1) <= found.start() < event.get("offset", -1) + event.get("bytes", 0)), {})
            observations.append({"endpoint": endpoint, "kind": "panic" if line.startswith("Guru Meditation") else "backtrace" if line.startswith("Backtrace") else "firmware_startup" if line.startswith("Diviner CYD:") else "reset",
                "raw_file": f"{endpoint}/serial-rx.redacted.bin", "raw_byte_offset": found.start(), "line": line,
                "host_rx_chunk_elapsed_s": chunk.get("host_elapsed_s"),
                "time_scope": "host receive-chunk time, not target exception timestamp"})
    return observations


def analyze(result, raw_directory=None):
    if result.get("schema") != "cyd-perf-link-capture/1" or result.get("evidence_class") != "physical_measured":
        raise ValueError("Expected physical cyd-perf-link-capture/1 records")
    rows, groups = [], collections.defaultdict(list)
    for run in result["runs"]:
        group_id, baseline_id, context = scope(result, run)
        row = {"run_id": run["run_id"], "workload": run["workload"], "status": run["status"],
               "profile_status": run.get("profile_status", "NOT MEASURED"), "repetition": run.get("repetition"),
               "group_id": group_id, "baseline_id": baseline_id, "context": context,
               "error": run.get("error"), "profile": profile_observations(result, run)}
        for name, endpoint in result["endpoints"].items():
            identity = endpoint.get("identity", {})
            row.update({name + "_" + field: identity.get(field) for field in ("source_sha", "source_digest", "firmware_sha256", "profile")})
        idle = run["settings"]["pattern"] == "none"
        row["rtt_status"] = "NOT APPLICABLE: idle" if idle else "NO VALID RESPONSES"
        summaries = {name: [record for record in run.get(name + "_link", []) if record.get("type") == "link_summary" and record.get("run_id") == run["run_id"]]
                     for name in ("cyd", "peer")}
        if not idle and all(len(records) == 1 for records in summaries.values()):
            row["cyd_link"], row["peer_link"] = summaries["cyd"][0], summaries["peer"][0]
            try:
                row["network"] = derive_link(row["cyd_link"], row["peer_link"], result["plan"].get("duration_ms", 10000), run["settings"])
                accepted, rtt_n = row["network"]["sender_api_accepted_packets"], row["network"]["rtt_samples"]
                row["network"]["round_trip_not_observed_fraction"] = (accepted - rtt_n) / accepted if run["settings"]["pattern"] == "rtt" and accepted and rtt_n <= accepted else None
                row["rtt_status"] = ("OBSERVED" if rtt_n else "NOT OBSERVED") if run["settings"]["pattern"] == "rtt" else "NOT APPLICABLE: one-way"
                if run["status"] == "MEASURED" and not row["network"]["traffic_established"]:
                    raise ValueError("Claimed measured link has no valid end-to-end traffic")
            except (RuntimeError, ValueError) as error:
                if run["status"] == "MEASURED":
                    raise
                row["network_validation_error"] = str(error)
                row["rtt_status"] = "INVALID / PARTIAL"
        elif not idle and run["status"] == "MEASURED":
            raise ValueError("Measured active link lacks matching per-board run summaries")
        row["counter_limits"] = {"peer_too_old": row.get("peer_link", {}).get("too_old"),
            "peer_callback_drops_session_including_warmup": row.get("peer_link", {}).get("rx_callback_queue_drops_session_including_warmup"),
            "cyd_callback_drops_session_including_warmup": row.get("cyd_link", {}).get("rx_callback_queue_drops_session_including_warmup")}
        rows.append(row)
        groups[group_id].append(row)
    comparisons = []
    for group_id, attempts in groups.items():
        valid = [row for row in attempts if row["status"] == "MEASURED"]
        profiles = [row["profile"] for row in valid if row["profile"]["status"] == "MEASURED"]
        links = [row["cyd_link"] for row in valid if row.get("cyd_link")]
        entry = {"group_id": group_id, "baseline_id": attempts[0]["baseline_id"], "context": attempts[0]["context"],
            "attempted_runs": len(attempts), "measured_runs": len(valid), "run_status_counts": dict(collections.Counter(row["status"] for row in attempts)),
            "profile_valid_runs_including_failed_links": sum(row["profile"]["status"] == "MEASURED" for row in attempts),
            "received_payload_bytes_per_second": value_range([row["network"]["receiver_payload_goodput_bytes_per_s"] for row in valid if row.get("network")]),
            "fps_successful_cases": value_range([profile["frames_per_second"] for profile in profiles]),
            "rtt": histogram(links, "rtt_count", "rtt_histogram_counts", "rtt_min_us", "rtt_max_us", "rtt_sum_us") if links else None,
            "peer_cpu_utilization": None,
            "heap_free_min_bytes": min((p["heap_free_bytes"]["min"] for p in profiles if p["heap_free_bytes"]["min"] is not None), default=None),
            "heap_largest_min_bytes": min((p["heap_largest_bytes"]["min"] for p in profiles if p["heap_largest_bytes"]["min"] is not None), default=None)}
        for core in (0, 1):
            sampled = [profile["cores"][f"core{core}"] for profile in profiles if profile["cores"][f"core{core}"]["busy_pct"] is not None]
            ticks = sum(value["total_ticks"] for value in sampled)
            busy = sum(value["busy_ticks"] for value in sampled)
            entry[f"cyd_core{core}_busy_pct"] = 100 * busy / ticks if ticks else None
        for metric in ("audio_callback_us", "audio_lock_us", "audio_mix_us", "audio_enqueue_us", "audio_period_us"):
            records = [profile["metrics"][metric] for profile in profiles]
            entry[metric] = histogram(records, "count", "histogram", "min", "max", "sum") if records else None
        accepted = sum(row.get("network", {}).get("sender_api_accepted_packets", 0) for row in valid)
        received = sum(row.get("network", {}).get("receiver_unique_packets", 0) for row in valid)
        entry["accepted_packets"], entry["receiver_unique_packets"] = accepted, received
        entry["accepted_not_observed_fraction"] = (accepted - received) / accepted if accepted and received <= accepted else None
        entry["too_old_packets"] = sum(row.get("peer_link", {}).get("too_old", 0) for row in valid)
        adjacent_n = sum(record.get("rtt_adjacent_abs_difference_count", 0) for record in links)
        entry["adjacent_rtt_absolute_difference"] = {"n": adjacent_n,
            "mean_us": sum(record.get("rtt_adjacent_abs_difference_sum_us", 0) for record in links) / adjacent_n if adjacent_n else None,
            "max_us": max((record.get("rtt_adjacent_abs_difference_max_us", 0) for record in links), default=None)}
        events = [record for profile in profiles for record in profile["i2s_events"]]
        observed = [record for record in events if record.get("enabled") is True and record.get("queue_available") is True and record.get("observed") is True]
        entry["i2s_events"] = {"observed_windows": len(observed), "tx_q_ovf_indications": sum(record.get("tx_underflow_indications", 0) for record in observed) if observed else None,
            "dma_descriptor_faults": sum(record.get("dma_descriptor_faults", 0) for record in observed) if observed else None,
            "queue_full_observations": sum(record.get("queue_full_observations", 0) for record in observed) if observed else None}
        comparisons.append(entry)
    controls = {entry["baseline_id"]: entry for entry in comparisons if entry["context"]["pattern"] == "none" and entry["measured_runs"]}
    for entry in comparisons:
        baseline = controls.get(entry["baseline_id"])
        median = entry["fps_successful_cases"]["median"]
        control_fps = baseline["fps_successful_cases"]["median"] if baseline else None
        entry["baseline_group_id"] = baseline["group_id"] if baseline else None
        entry["fps_difference_from_matched_idle"] = median - control_fps if median is not None and control_fps is not None else None
    faults = raw_faults(raw_directory)
    mode_required = sum("mode_required" in str(row.get("error")) for row in rows)
    restarted = any(later["endpoint"] == fault["endpoint"] and later["kind"] in ("reset", "firmware_startup") and
        later["raw_byte_offset"] > fault["raw_byte_offset"] for fault in faults if fault["kind"] == "panic" for later in faults)
    return {"schema": "cyd-perf-link-analysis/1", "capture_status": result["status"],
        "execution_complete": result.get("execution_complete", False), "endpoints": result["endpoints"],
        "raw_runtime_diagnostics": faults,
        "failure_context": {"panic_observed": any(record["kind"] == "panic" for record in faults), "startup_or_reset_after_panic": restarted, "mode_required_rejections": mode_required,
            "interpretation": "A runtime panic/reboot can clear volatile topology/session state. Later mode-required rejections must not be counted as independent hardware-capacity failures. No unsupported-hardware conclusion follows from this sequence."},
        "run_status_counts": dict(collections.Counter(row["status"] for row in rows)), "runs": rows, "groups": comparisons,
        "limitations": ["FAILED/BLOCKED cases and separately valid profiler windows retain their own statuses. Only MEASURED link cases enter successful-link aggregates.",
            "Idle has no RTT/goodput/loss observation; missing values remain null. Per-repetition goodput uses median/range, not an unsupported p99 from three repetitions.",
            "RTT percentiles pool bounded histogram bins across matching sessions, are upper bounds capped at exact observed max, and require>=100 samples for p99.",
            "Adjacent absolute RTT differences are not one-way delay jitter; cross-repetition boundaries are not joined.",
            "RTT histograms are conditional on valid replies received within the bounded tail. Missing responses never enter latency bins; sender/receiver/RTT counts must accompany the tails.",
            "Receiver counts exclude packets outside the 64-sequence deduplication horizon. too_old can include uncounted unique packets, making observed unique goodput a lower bound and the not-observed fraction an overestimate of loss.",
            "API acceptance is not peer receipt. Not-observed fraction includes software queues, horizon and tail effects; it is not isolated RF packet loss.",
            "TCP frames created in the steady window can finish API acceptance in the defined reply tail. Sender and receiver rates therefore share marked-window-plus-tail semantics.",
            "Peer CPU utilization is not measured by this image; CYD tick utilization is an estimate. Audio enqueue includes blocking DMA waits; driver events do not prove analog continuity.",
            "Only matching source/image, topology, local settings/allocation receipts, channel/power/connection parameters, payload, rate and timing scopes are pooled. Idle comparison includes transport resource/connection setup differences stated by its mode."]}


def csv_file(path, rows):
    fields = list(dict.fromkeys(key for row in rows for key in row)) or ["status"]
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: json.dumps(value, separators=(",", ":")) if isinstance(value, (list, dict)) else value for key, value in row.items()})


def report(analysis, input_hash, raw_href):
    f = lambda value, places=2: "—" if value is None else f"{value:.{places}f}" if isinstance(value, float) else str(value)
    lines = ["# Owned-board wireless evidence", "", f"Source capture status: **{analysis['capture_status']}**. "
        f"Execution reached plan end: **{analysis['execution_complete']}**. Run statuses: `{analysis['run_status_counts']}`. "
        f"Input SHA256 `{input_hash}`.", "", f"[Raw structured source]({raw_href}); redacted per-board RX/target/event logs and image/flash receipts remain separate evidence.", ""]
    for name, endpoint in analysis["endpoints"].items():
        identity = endpoint.get("identity", {})
        lines += [f"* **{name}**: `{identity.get('profile')}`, source `{identity.get('source_sha')}`, "
                  f"source digest `{identity.get('source_digest')}`, binary SHA256 `{identity.get('firmware_sha256')}`."]
    lines += ["", "Only matching settings/source/image/allocation/connection contexts are aggregated. Goodput is unique application payload per initiator target second, excluding the 40-byte header and PHY overhead. "
        "RTT p50/p95/p99 values below are histogram upper bounds capped at the exact observed maximum; p99 is absent below 100 observations. "
        "The displayed not-observed fraction is an end-to-end counter difference, not isolated RF loss. A 64-sequence horizon, callback queues and the receive tail limit attribution.", "",
        "## Per-repetition observations", "", "| Run | Workload | link/profile status | received B/s | API/peer unique | not seen fwd/RTT % | RTT n / p95≤ / max µs | CYD FPS |", "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    for row in analysis["runs"]:
        network, profile = row.get("network", {}), row["profile"]
        fraction = network.get("accepted_packet_loss_fraction")
        roundtrip = network.get("round_trip_not_observed_fraction")
        lines.append(f"| {row['run_id']} | {row['workload']} | {row['status']}/{profile['status']} | {f(network.get('receiver_payload_goodput_bytes_per_s'))} | "
            f"{f(network.get('sender_api_accepted_packets'))}/{f(network.get('receiver_unique_packets'))} | {f(fraction * 100 if fraction is not None else None)}/{f(roundtrip * 100 if roundtrip is not None else None)} | "
            f"{f(network.get('rtt_samples'))}/{f(network.get('rtt_p95_us_upper'))}/{f(network.get('rtt_max_us'))} | {f(profile.get('frames_per_second'))} |")
    lines += ["", "Idle cells are intentionally unavailable for RTT/goodput/loss. FAILED/BLOCKED traffic cases do not enter the successful-link aggregates; "
        "a completed local profiler window remains separately inspectable when peer traffic fails.", "",
        "| Run | sender API errors | receiver dup/reordered/too-old | CRC CYD/peer | callback drops CYD/peer (session incl warmup) |",
        "| --- | --- | --- | --- | --- |"]
    for row in analysis["runs"]:
        client, peer = row.get("cyd_link", {}), row.get("peer_link", {})
        lines.append(f"| {row['run_id']} | {f(client.get('send_errors'))} | {f(peer.get('duplicates'))}/{f(peer.get('reordered'))}/{f(peer.get('too_old'))} | "
                     f"{f(client.get('crc_errors'))}/{f(peer.get('crc_errors'))} | {f(client.get('rx_callback_queue_drops_session_including_warmup'))}/{f(peer.get('rx_callback_queue_drops_session_including_warmup'))} |")
    lines += ["", "## Matching-context envelopes", "",
        "| Group / workload | valid/attempts | goodput median [min,max] B/s | RTT n / p50≤ / p95≤ / p99≤ / max µs | FPS / Δ idle | CPU0/1 % | min heap/largest B | callback p95≤/max µs | period max µs | TX_Q_OVF/DMA_ERROR |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    for group in analysis["groups"]:
        context, rtt, goodput = group["context"], group.get("rtt") or {}, group["received_payload_bytes_per_second"]
        callback, period = group.get("audio_callback_us") or {}, group.get("audio_period_us") or {}
        events = group["i2s_events"]
        lines.append(f"| {group['group_id']} {context['topology']}/{context['local_workload']}/{context['mode']} {context['payload_bytes']}B@{context['packets_per_second']}pps {context['duration_ms']}ms | {group['measured_runs']}/{group['attempted_runs']} | "
            f"{f(goodput['median'])} [{f(goodput['min'])},{f(goodput['max'])}] | {f(rtt.get('n'))}/{f(rtt.get('p50_us_upper'))}/{f(rtt.get('p95_us_upper'))}/{f(rtt.get('p99_us_upper'))}/{f(rtt.get('max_us'))} | "
            f"{f(group['fps_successful_cases']['median'])}/{f(group['fps_difference_from_matched_idle'])} | {f(group.get('cyd_core0_busy_pct'))}/{f(group.get('cyd_core1_busy_pct'))} | "
            f"{f(group['heap_free_min_bytes'])}/{f(group['heap_largest_min_bytes'])} | {f(callback.get('p95_us_upper'))}/{f(callback.get('max_us'))} | "
            f"{f(period.get('max_us'))} | {f(events['tx_q_ovf_indications'])}/{f(events['dma_descriptor_faults'])} |")
    lines += ["", "The baseline delta uses a matching topology/local/source/allocation/connection control, when present; transport resource differences remain part of the comparison. "
        "CYD utilization uses non-idle tick samples. Peer CPU is unmeasured. Enqueue duration includes DMA blocking; independent timing maxima/percentiles must not be added as one block. "
        "TX_Q_OVF counts the legacy driver's free-TX-buffer-queue-full notification, not a measured audible gap. Queue limits/drain timing can hide events; analog continuity remains unknown.", "",
        "RTT jitter is the mean/max absolute difference between adjacent received RTTs within each run; repetitions are not joined. "
        "The JSON/CSV retains counters, jitter, callback/lock/mix/enqueue/period histograms, BLE negotiated parameters, CRC/duplicates/reordering/too-old/queue-drop counts and actual allocation receipts.", "",
        "## Failures and measurement limits", ""]
    if analysis.get("failure_context", {}).get("panic_observed"):
        lines += ["**A runtime panic is present in the redacted raw stream.** This is firmware/runtime failure evidence, not a BLE hardware-unsupported result. "
            f"The capture also records {analysis['failure_context']['mode_required_rejections']} mode-required rejections. A reboot invalidates volatile setup; "
            "these later rejections must not be presented as independent hardware-capacity tests. Abort/re-provision after a detected runtime reset before claiming independent repetitions.", ""]
    for fault in analysis.get("raw_runtime_diagnostics", []):
        lines += [f"* Raw `{fault['endpoint']}` {fault['kind']} at byte {fault['raw_byte_offset']} in `{fault['raw_file']}` "
            f"(host RX-chunk elapsed {f(fault['host_rx_chunk_elapsed_s'])} s): `{fault['line']}`."]
    for row in analysis["runs"]:
        if row["status"] != "MEASURED":
            lines += [f"* `{row['run_id']}` {row['workload']}: **{row['status']}** — {row.get('error', 'no completed transport result')}."]
    lines += ["", *["* " + note for note in analysis["limitations"]], "", "## Offload interpretation", "",
        "Compare local compute with remote compute plus packing/unpacking, queueing and measured transport for the actual input/output message shapes. "
        "A full matched-payload RTT includes its payload transfer; do not add that transmission again. "
        "A separate fixed-overhead/throughput model needs independently estimated fixed overhead and directional byte-transfer costs. "
        "The 64-byte echo and paced one-way throughput are not a measured job RPC for arbitrary larger inputs/outputs. "
        "A throughput average or finite maximum gives no hard real-time guarantee; these links do not by themselves justify moving the local DAC deadline loop off-board.", "",
        "Artifacts: [link-summary.json](link-summary.json), [per-run CSV](link-runs.csv), [group CSV](link-groups.csv), "
        "[analysis manifest](link-analysis-manifest.json).", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    payload = args.results.read_bytes()
    analysis = analyze(json.loads(payload), args.results.parent)
    output = args.output or args.results.parent
    output.mkdir(parents=True, exist_ok=True)
    input_hash = hashlib.sha256(payload).hexdigest()
    (output / "link-summary.json").write_text(json.dumps(analysis, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    csv_file(output / "link-runs.csv", analysis["runs"])
    csv_file(output / "link-groups.csv", analysis["groups"])
    raw_href = pathlib.Path(os.path.relpath(args.results, output)).as_posix()
    (output / "LINK-REPORT.md").write_text(report(analysis, input_hash, raw_href), encoding="utf-8")
    names = ("link-summary.json", "link-runs.csv", "link-groups.csv", "LINK-REPORT.md")
    manifest = {"schema": "cyd-perf-link-analysis-manifest/1", "input_sha256": input_hash,
        "capture_status": analysis["capture_status"], "endpoints": {name: endpoint.get("identity", {}) for name, endpoint in analysis["endpoints"].items()},
        "tools": {name: digest(pathlib.Path(__file__).with_name(name)) for name in ("link_analyze.py", "link_capture.py")},
        "outputs": {name: {"bytes": (output / name).stat().st_size, "sha256": digest(output / name)} for name in names}}
    (output / "link-analysis-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(output / "LINK-REPORT.md")


if __name__ == "__main__":
    main()
