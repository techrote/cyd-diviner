#!/usr/bin/env python3
"""Finite, RX-first owned-board audio MVP capture; no flashing or discovery."""
from __future__ import annotations

import argparse
import collections
import csv
import json
import math
import pathlib
import re
import secrets
import sys
import time

from capture import utc_now
from link_capture import Endpoint, SecretGuard, digest, read_identities

AUDIO_CONFIG_FIELDS = ("codec", "channels", "sample_rate_hz", "frames_per_block", "pdm_osr", "phy_mbps_requested",
    "phy_config_status", "channel", "seed", "block_payload_bytes", "fragments_per_block", "packet_header_bytes",
    "max_packet_bytes", "playout_delay_blocks", "rx_slots", "rx_callback_queue_depth", "tx_inflight_limit",
    "worker_core", "worker_priority", "pacing", "transport", "mac_ack_required", "application_retransmission",
    "sink", "i2s_output", "raw_continuous_RF", "clock_scope")
BOUNDED_TELEMETRY_FAILURE = "bounded telemetry allocation or length failure"


def metadata_policy(plan):
    deferred = plan.get("defer_radio_metadata", False)
    return {"mode": "deferred_radio_metadata/1" if deferred else "live_start_metadata/1",
        "start_requirement": "matching successful audio start ACK; missing ACK aborts",
        "active_radio_info_query": not deferred, "audio_run_record_required_live": not deferred,
        "export_order": "stop both radios before export" if deferred else "export before stop",
        "allowed_deferred_diagnostic": BOUNDED_TELEMETRY_FAILURE if deferred else None,
        "radio_channel_ps_query_scope": "NOT MEASURED while active; checked SDK setup inferred from ACK/source" if deferred else "queried after RX start"}


def is_bounded_telemetry_failure(record):
    return record.get("type") == "kernel_error" and record.get("error") == BOUNDED_TELEMETRY_FAILURE


def deferred_ack(endpoint, command, *, started=False, timeout=15):
    """Accept the exact formatter diagnostic only after a validated start ACK.

    This does not relax Endpoint.until or the default path. A missing/failed ACK,
    a prior formatter error, malformed JSON or any other error still fails.
    """
    action, cursor = command.split()[2], len(endpoint.records)
    endpoint.send(command)
    deadline = time.perf_counter() + timeout
    while time.perf_counter() < deadline:
        if endpoint.errors:
            raise RuntimeError("Malformed audio telemetry; deferred mode cannot recover it")
        acknowledgments, permitted = [], started
        recent = endpoint.records[cursor:]
        for record in recent:
            if record["type"] in ("ack", "audio_ack", "link_ack") and record.get("ok") is False:
                raise RuntimeError("Deferred audio setup failed: " + json.dumps(endpoint.guard.clean(record)))
            if record["type"] == "audio_ack" and record.get("action") == action:
                if record.get("ok") is not True:
                    raise RuntimeError("Successful matching audio ACK is required")
                acknowledgments.append(record)
                if action in ("serve", "run"):
                    permitted = True
            if record["type"] in ("error", "audio_error", "link_error", "kernel_error"):
                if not permitted or not is_bounded_telemetry_failure(record):
                    raise RuntimeError("Deferred metadata rejected diagnostic: " + json.dumps(endpoint.guard.clean(record)))
        if acknowledgments:
            if len(acknowledgments) != 1:
                raise RuntimeError("Ambiguous matching audio ACK")
            return recent
        endpoint.read()
    raise TimeoutError("Missing successful matching audio ACK; deferred mode aborted")


def deferred_start_proof(endpoint, cursor, action, run_id, role, duration, expected_blocks):
    records = endpoint.records[cursor:]
    ack_index = next((index for index, record in enumerate(records) if record.get("type") == "audio_ack"
        and record.get("action") == action and record.get("ok") is True), None)
    if ack_index is None:
        raise RuntimeError("Deferred audio start ACK proof is missing")
    diagnostics = []
    for index, record in enumerate(records):
        if record["type"] in ("error", "audio_error", "link_error", "kernel_error"):
            if index <= ack_index or not is_bounded_telemetry_failure(record):
                raise RuntimeError("Unpermitted diagnostic in deferred audio start/window")
            diagnostics.append(record)
    windows = [record for record in records if record["type"] == "audio_run"]
    if len(windows) > 1 or (windows and (windows[0].get("run_id") != run_id or windows[0].get("role") != role
        or windows[0].get("duration_us") != duration * 1000 or windows[0].get("source_total_blocks") != expected_blocks)):
        raise RuntimeError("Deferred audio_run record correlation failed")
    if not windows and not diagnostics:
        raise RuntimeError("Missing audio_run without the documented bounded-telemetry diagnostic")
    return {"start_ack": records[ack_index], "audio_run_records": windows, "telemetry_diagnostics": diagnostics,
            "audio_run_observation": "RECEIVED" if windows else "UNAVAILABLE: documented bounded telemetry failure after valid start ACK"}


class AudioEndpoint(Endpoint):
    """Reuse redacted serial storage, with audio ACK and metadata correlation."""
    def send(self, command):
        if command.startswith("perf audio allow "):
            raise ValueError("Audio session authorization requires a private transaction")
        super().send(command)

    def until(self, kinds, cursor, timeout=15, run_id=None, private=False):
        if private:
            kinds = kinds | {"audio_ack"}
        expected = next(iter(kinds)) if len(kinds) == 1 else None
        recover = (not private and self.identity.get("usb_progress_poll", True)
                   and (self.native_usb or self.identity.get("usb_progress_poll") is True)
                   and expected in ("info", "audio_info", "audio_summary"))
        deadline, last_offset, last_progress = time.perf_counter() + timeout, self.offset, time.perf_counter()
        pokes, last_poke = 0, float("-inf")
        while time.perf_counter() < deadline:
            if self.errors:
                raise RuntimeError("Malformed audio telemetry; redacted raw retained")
            records = self.private_records if private else self.records
            recent = records[cursor:]
            for record in recent:
                if record["type"] in ("error", "audio_error", "link_error", "kernel_error"):
                    raise RuntimeError("Audio endpoint rejected operation: " + json.dumps(self.guard.clean(record)))
                if record["type"] in ("ack", "audio_ack", "link_ack") and record.get("ok") is False:
                    raise RuntimeError("Audio endpoint setup failed: " + json.dumps(self.guard.clean(record)))
            matched = any(record["type"] in kinds and (run_id is None or record.get("run_id") == run_id) for record in recent)
            metadata_replies = sum(record["type"] == "audio_info" for record in recent)
            required_replies = pokes + (1 if expected == "audio_info" else 0)
            if matched and (not recover or metadata_replies >= required_replies):
                return recent
            self.read(private=private)
            at = time.perf_counter()
            if self.offset != last_offset:
                last_offset, last_progress = self.offset, at
            if recover and pokes < 3 and at - last_progress >= .75 and at - last_poke >= 1:
                pending = bytes(self.pending).lstrip()
                allowed = {expected, "audio_info"} if matched else {expected}
                kind = next((kind for kind in allowed if pending.startswith(b"PERF {") and
                             re.search(rb'"type"\s*:\s*"' + kind.encode() + rb'"', pending)), None)
                if kind:
                    pokes += 1
                    self.log("usb_progress_poll", awaited_record=expected, pending_record=kind, attempt=pokes,
                             stalled_for_s=at - last_progress, rx_offset=self.offset, pending_bytes=len(self.pending),
                             command="perf audio info", scope="partial public metadata/export only; never finite streaming window")
                    self.send("perf audio info")
                    last_poke = at
        raise TimeoutError("Audio endpoint acknowledgment/summary timeout; raw evidence retained")

    def audio(self, command, kind="audio_ack", run_id=None, timeout=15):
        records = self.request(command, {kind}, timeout=timeout, run_id=run_id)
        if kind == "audio_ack":
            action = command.split()[2]
            matches = [record for record in records if record["type"] == kind and record.get("action") == action]
            if len(matches) != 1:
                raise RuntimeError("Audio acknowledgment action is missing or ambiguous")
        return records

    def audio_info(self):
        return [record for record in self.audio("perf audio info", "audio_info") if record["type"] == "audio_info"][-1]


def default_plan(reverse=False, include_pdm=False):
    workloads = []
    directions = (("peer", "cyd"), ("cyd", "peer")) if reverse else (("peer", "cyd"),)
    for sender, receiver in directions:
        for rate in (1, 6):
            for sample_hz, frames in ((24000, 8), (24000, 16), (24000, 32), (48000, 16)):
                workloads.append({"name": f"{sender}-to-{receiver}/pcm16/{sample_hz}/2ch/{frames}f/{rate}Mbps",
                    "sender": sender, "receiver": receiver, "codec": "pcm16", "sample_hz": sample_hz,
                    "channels": 2, "frames_per_packet": frames, "osr": 0, "rate_mbps": rate})
            for channels, frames, osr in ((1, 16, 16), (1, 16, 32), (2, 8, 16), (2, 16, 16), (2, 8, 32), (2, 16, 32)):
                if not include_pdm:
                    continue
                workloads.append({"name": f"{sender}-to-{receiver}/pdm/24000/{channels}ch/{frames}f/osr{osr}/{rate}Mbps",
                    "sender": sender, "receiver": receiver, "codec": "pdm", "sample_hz": 24000,
                    "channels": channels, "frames_per_packet": frames, "osr": osr, "rate_mbps": rate})
    return {"schema": "cyd-audio-mvp-plan/1", "repetitions": 3, "duration_ms": 3000, "channel": 6,
            "seed": 23063, "receive_tail_ms": 500, "workloads": workloads}


def validate_plan(plan):
    if plan.get("schema") != "cyd-audio-mvp-plan/1" or not plan.get("workloads"):
        raise ValueError("Expected nonempty cyd-audio-mvp-plan/1 workloads")
    for key, lower, upper in (("repetitions", 1, 20), ("duration_ms", 100, 30000), ("channel", 1, 13), ("receive_tail_ms", 100, 2000)):
        value = plan.get(key, {"repetitions": 3, "duration_ms": 3000, "channel": 6, "receive_tail_ms": 500}[key])
        if type(value) is not int or not lower <= value <= upper:
            raise ValueError("Invalid finite audio plan " + key)
    if plan.get("channel", 6) != 6:
        raise ValueError("This firmware contract uses owned-board channel6; other channels are not configurable")
    if "defer_radio_metadata" in plan and type(plan["defer_radio_metadata"]) is not bool:
        raise ValueError("defer_radio_metadata is an explicit boolean opt-in")
    for case in plan["workloads"]:
        if not isinstance(case.get("name"), str) or not case["name"]:
            raise ValueError("Every audio workload requires a name")
        if case.get("sender") not in ("cyd", "peer") or case.get("receiver") not in ("cyd", "peer") or case["sender"] == case["receiver"]:
            raise ValueError("Explicit distinct owned sender/receiver identities are required")
        for key, values in (("codec", ("pcm16", "pdm")), ("sample_hz", (24000, 48000)), ("channels", (1, 2, 4)),
                            ("frames_per_packet", (8, 16, 32)), ("osr", (0, 16, 32)), ("rate_mbps", (1, 6))):
            if case.get(key) not in values:
                raise ValueError("Unsupported bounded audio configuration " + key)
        if (case["codec"] == "pcm16") != (case["osr"] == 0):
            raise ValueError("PCM16 requires OSR0; packetized PDM requires OSR16|32")


def verify_identity(endpoint):
    records = endpoint.request("perf info", {"info"})
    info = next(record for record in records if record["type"] == "info")
    for field in ("source_sha", "source_digest", "profile"):
        if info.get(field) != endpoint.identity[field]:
            raise RuntimeError("Audio target source/profile differs from the identified image")
    endpoint.native_usb = info.get("native_usb") is True
    return {"identity": endpoint.guard.clean(endpoint.identity), "target_info": records,
            "audio_info": endpoint.audio_info(), "binary_digest_attested_by_target": False}


def save(result, output, guard):
    public = guard.clean(result)
    temporary = output / "results.tmp"
    temporary.write_text(json.dumps(public, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temporary.replace(output / "results.json")
    rows = []
    for run in public["runs"]:
        base = {"run_id": run["run_id"], "workload": run["workload"], "status": run["status"], "repetition": run["repetition"], **run["settings"]}
        for role in ("sender", "receiver"):
            summary = run.get(role + "_summary", {})
            rows.append({**base, "endpoint_role": role, "endpoint": run["settings"][role], **summary})
    fields = list(dict.fromkeys(key for row in rows for key in row)) or ["run_id", "status"]
    with (output / "audio-metrics.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: json.dumps(value, separators=(",", ":")) if isinstance(value, (dict, list)) else value for key, value in row.items()})


def execute_run(run, endpoints, plan, guard, metadata):
    case, run_id = run["settings"], run["run_id"]
    sender, receiver = endpoints[case["sender"]], endpoints[case["receiver"]]
    deferred = plan.get("defer_radio_metadata", False)
    run["metadata_policy"] = metadata_policy(plan)
    run["setup_target"] = {}
    run["local_context"] = {}
    for name, endpoint in endpoints.items():
        setup = endpoint.audio("perf audio stop")
        setup += endpoint.request("perf link stop")
        setup += endpoint.request("perf link mode off 0")
        profile = metadata[name].get("identity", {}).get("profile", endpoint.identity.get("profile"))
        headless = profile == "kernel-s3-n4r2-audio"
        run["local_context"][name] = {"logical_role": name, "profile": profile,
            "display_context": "headless standalone audio; no display/menu" if headless else "Diviner menu" if name == "cyd" else "standalone peer",
            "menu_command": "NOT APPLICABLE" if headless else "perf scene menu" if name == "cyd" else "NOT REQUESTED"}
        if name == "cyd" and not headless:
            setup += endpoint.request("perf scene menu")
        setup += endpoint.audio(f"perf audio config {case['codec']} {case['channels']} {case['sample_hz']} "
                                f"{case['frames_per_packet']} {case['osr']} {case['rate_mbps']} {plan.get('seed', 23063)}")
        run["setup_target"][name] = setup
    session = secrets.token_hex(4)
    guard.add(session)
    guard.add(session.upper())
    for name, endpoint in endpoints.items():
        other = metadata[case["receiver"] if name == case["sender"] else case["sender"]]["audio_info"]
        mac = other.get("sta_mac")
        if not isinstance(mac, str) or not re.fullmatch(r"[0-9a-fA-F]{12}", mac) or int(mac[:2], 16) & 1 or int(mac, 16) == 0:
            raise ValueError("UART-identified owned audio peer MAC is absent/invalid")
        endpoint.private_request(f"perf audio allow {mac} {session}", "audio_known_peer_session")
    duration = plan.get("duration_ms", 3000)
    cursors = {name: len(endpoint.records) for name, endpoint in endpoints.items()}
    # RX activation is acknowledged before TX is allowed to begin. Both data
    # paths remain inside the boards; the host only records serial telemetry.
    if deferred:
        run["receiver_start"] = deferred_ack(receiver, f"perf audio serve {run_id} {duration}")
        run["receiver_ready"] = {"status": "NOT MEASURED", "scope": "active channel/power-save queries deferred to avoid formatter pressure",
            "setup_evidence": "successful matching serve ACK implies checked SDK setup calls succeeded; not an independent state query"}
        run["sender_start"] = deferred_ack(sender, f"perf audio run {run_id} {duration}")
    else:
        run["receiver_start"] = receiver.audio(f"perf audio serve {run_id} {duration}")
        run["receiver_ready"] = receiver.audio_info()
        ready = run["receiver_ready"]
        if (ready.get("active") is not True or ready.get("role") != "rx" or ready.get("radio_initialized") is not True
            or ready.get("actual_channel") != 6 or ready.get("channel_query_status") != 0
            or ready.get("power_save_enum") != 0 or ready.get("power_save_status") != 0
            or ready.get("phy_config_status") != 0):
            raise RuntimeError("Owned receiver did not report active channel6/PS-off/PHY-configured readiness")
        run["sender_start"] = sender.audio(f"perf audio run {run_id} {duration}")
        run["sender_window"] = sender.until({"audio_run"}, cursors[case["sender"]], run_id=run_id)
    run["host_observation_guard_ms"] = 1000 + plan.get("receive_tail_ms", 500)
    deadline = time.perf_counter() + (duration + run["host_observation_guard_ms"]) / 1000
    while time.perf_counter() < deadline:
        for endpoint in endpoints.values():
            endpoint.read()
    # No control queries are sent within the finite streaming interval. Deferred
    # mode stops both radios to free INTERNAL|8BIT heap before large exports;
    # this approved firmware preserves complete/stats/runID across stop.
    if deferred:
        run["post_window_stop_target"] = {name: deferred_ack(endpoint, "perf audio stop", started=True)
            for name, endpoint in endpoints.items()}
        expected_blocks = case["sample_hz"] * duration // (1000 * case["frames_per_packet"])
        run["deferred_start_proof"] = {
            "receiver": deferred_start_proof(receiver, cursors[case["receiver"]], "serve", run_id, "rx", duration, expected_blocks),
            "sender": deferred_start_proof(sender, cursors[case["sender"]], "run", run_id, "tx", duration, expected_blocks)}
        run["last_error_observation_scope"] = "post-stop command status; stop resets last_error/last_operation, RF counters retained"
    for role in ("sender", "receiver"):
        endpoint = endpoints[case[role]]
        records = endpoint.audio("perf audio export", "audio_summary", run_id=run_id, timeout=20)
        summaries = [record for record in records if record["type"] == "audio_summary" and record.get("run_id") == run_id]
        if len(summaries) != 1:
            raise RuntimeError("Audio summary is missing or ambiguous")
        run[role + "_summary"] = summaries[0]
    run["target_record_start_indices"] = cursors
    run["derived"] = validate_run(run, duration, plan.get("seed", 23063))
    if not deferred:
        for endpoint in endpoints.values():
            endpoint.audio("perf audio stop")
    run["post_info"] = {name: endpoint.audio_info() for name, endpoint in endpoints.items()}
    run["post_info_scope"] = "outside finite source/receiver windows after stop; not in-window heap"
    run["outside_window_temperature"] = {"scope": "after finite audio export and stop; target state run_id retained; not an in-window sample",
                                         "endpoints": {}}
    for name, endpoint in endpoints.items():
        try:
            records = endpoint.request("perf sample", {"state"}, timeout=3)
            run["outside_window_temperature"]["endpoints"][name] = {"status": "MEASURED",
                "records": [record for record in records if record["type"] == "state"]}
        except (RuntimeError, TimeoutError, ValueError) as error:
            run["outside_window_temperature"]["endpoints"][name] = {"status": "NOT MEASURED", "reason": guard.clean(str(error))}
    run["status"] = "MEASURED"


def validate_run(run, duration_ms, seed=None):
    """Final schema/counter validation is shared by capture and offline analysis."""
    sender, receiver = run["sender_summary"], run["receiver_summary"]
    case = run["settings"]
    expected = {"codec": case["codec"], "channels": case["channels"], "sample_rate_hz": case["sample_hz"],
        "frames_per_block": case["frames_per_packet"], "pdm_osr": case["osr"], "phy_mbps_requested": case["rate_mbps"],
        "phy_config_status": 0, "channel": 6, "packet_header_bytes": 20, "max_packet_bytes": 250,
        "transport": "ESP_NOW_broadcast_packets", "mac_ack_required": False, "application_retransmission": False,
        "sink": "digital_reconstruction_only", "i2s_output": False, "raw_continuous_RF": False}
    if seed is not None:
        expected["seed"] = seed
    block_bytes = case["frames_per_packet"] * case["channels"] * (2 if case["codec"] == "pcm16" else case["osr"] // 8)
    frame_bytes = case["channels"] * (2 if case["codec"] == "pcm16" else case["osr"] // 8)
    span = (230 // frame_bytes) * frame_bytes
    expected.update(block_payload_bytes=block_bytes, fragments_per_block=(block_bytes + span - 1) // span,
                    playout_delay_blocks=2, rx_slots=8, rx_callback_queue_depth=8, tx_inflight_limit=1)
    expected_blocks = case["sample_hz"] * duration_ms // (1000 * case["frames_per_packet"])
    if (type(sender.get("target_start_us")) is not int or sender["target_start_us"] <= 0
        or type(sender.get("target_end_us")) is not int or sender["target_end_us"] - sender["target_start_us"] != duration_ms * 1000):
        raise RuntimeError("Sender source schedule timestamps do not prove the requested finite window")
    counters = ("source_total_blocks", "source_encoded_blocks", "skipped_source_blocks", "expired_prepared_blocks",
        "encoder_reset_discontinuities", "tx_packet_attempts", "tx_api_accepted_packets", "tx_api_no_mem", "tx_api_other_errors",
        "tx_wire_bytes_accepted", "rx_callback_queue_drops", "rx_wrong_mac", "rx_expired_callback_packets", "rx_callback_age_max_us",
        "rendered_quality_blocks", "concealed_quality_blocks", "first_rendered_sequence", "last_rendered_sequence", "slot_dwell_max_us",
        "rx_accepted_packets", "rx_completed_blocks", "rx_invalid_packets", "rx_crc_errors", "rx_wrong_session", "rx_wrong_config",
        "rx_duplicate_packets", "rx_stale_packets", "rx_future_packets", "rx_concealed_frames", "rx_incomplete_blocks",
        "rx_skipped_playout_blocks", "rx_late_playout_blocks", "rx_render_lateness_max_us")
    for role, record in (("tx", sender), ("rx", receiver)):
        if (record.get("run_id") != run["run_id"] or record.get("role") != role or record.get("complete") is not True
            or record.get("timed_out") is not False):
            raise RuntimeError("Audio session did not naturally complete on " + role)
        if (any(record.get(key) != value for key, value in expected.items())
            or record.get("duration_us") != duration_ms * 1000 or record.get("source_total_blocks") != expected_blocks):
            raise RuntimeError("Audio summary configuration/source-window correlation failed on " + role)
        if any(type(record.get(key)) is not int or record[key] < 0 for key in counters):
            raise RuntimeError("Missing or invalid audio packet/deadline/PLC counters on " + role)
    if any(sender.get(key) != receiver.get(key) for key in AUDIO_CONFIG_FIELDS):
        raise RuntimeError("Audio endpoint configurations or bounded queue policies differ")
    rendered, concealed = receiver["rendered_quality_blocks"], receiver["concealed_quality_blocks"]
    if (not sender["source_encoded_blocks"] or not sender["tx_api_accepted_packets"] or not receiver["rx_accepted_packets"]
        or not receiver["rx_completed_blocks"] or not rendered or concealed > rendered
        or rendered > expected_blocks or receiver["last_rendered_sequence"] >= expected_blocks
        or receiver["first_rendered_sequence"] > receiver["last_rendered_sequence"]
        or receiver["rx_accepted_packets"] > sender["tx_api_accepted_packets"]):
        raise RuntimeError("No valid bounded end-to-end audio reconstruction or inconsistent source/receiver counts")
    quality = receiver.get("quality", [])
    if len(quality) != case["channels"] or {row.get("channel") for row in quality} != set(range(case["channels"])):
        raise RuntimeError("Receiver digital quality channels are missing/ambiguous")
    digital = []
    for row in quality:
        if (any(type(row.get(key)) is not int or row[key] < 0 for key in ("samples", "reference_energy", "error_energy", "error_abs_max"))
            or row["samples"] != rendered * case["frames_per_packet"]):
            raise RuntimeError("Digital quality sample coverage/energy is inconsistent")
        n, reference, error = row["samples"], row["reference_energy"], row["error_energy"]
        digital.append({"channel": row["channel"], "samples": n, "reference_rms_pcm16": math.sqrt(reference / n),
            "error_rms_pcm16": math.sqrt(error / n), "error_abs_max_pcm16": row["error_abs_max"],
            "snr_db": 10 * math.log10(reference / error) if reference and error else None,
            "perfect_observed_sample_match": error == 0, "correlation": None,
            "correlation_limitation": "Target exports energy/error sums, not cross-moments; correlation unavailable."})
    seconds, accepted = duration_ms / 1000, sender["tx_api_accepted_packets"]
    return {"source_blocks": expected_blocks, "source_pcm_frames": expected_blocks * case["frames_per_packet"],
        "sender_api_accepted_packets": accepted, "receiver_accepted_packets": receiver["rx_accepted_packets"],
        "accepted_not_admitted_at_receiver": accepted - receiver["rx_accepted_packets"],
        "accepted_not_admitted_fraction": (accepted - receiver["rx_accepted_packets"]) / accepted,
        "source_rendered_fraction": rendered / expected_blocks,
        "source_nonconcealed_rendered_fraction": (rendered - concealed) / expected_blocks,
        "observed_concealed_fraction": concealed / rendered,
        "nonconcealed_pcm_payload_bytes_per_s": (rendered - concealed) * case["frames_per_packet"] * case["channels"] * 2 / seconds,
        "completed_encoded_payload_bytes_per_s": receiver["rx_completed_blocks"] * receiver["block_payload_bytes"] / seconds,
        "digital_quality": digital, "one_way_latency_us": None,
        "counter_difference_scope": "API acceptance minus receiver admitted unique fragments includes RF, callback queue, deadline, stale/future and observation losses; not isolated RF loss.",
        "quality_scope": "Unshifted deterministic reference over rendered blocks, including PLC; skipped/unanchored source blocks are outside quality sums and remain coverage loss.",
        "latency_limitation": "Board clocks are unsynchronized; callback age and slot dwell use receiver-local time, not one-way audio latency."}


def capture(args):
    plan = json.loads(args.plan.read_text(encoding="utf-8-sig"))
    validate_plan(plan)
    identities, guard = read_identities(args.identities), SecretGuard()
    args.output.mkdir(parents=True, exist_ok=False)
    result = {"schema": "cyd-audio-mvp-capture/1", "evidence_class": "physical_measured", "status": "IN PROGRESS",
              "metadata_policy": metadata_policy(plan),
              "started_utc": utc_now(), "harness_sha256": digest(pathlib.Path(__file__)),
              "serial_module_sha256": digest(pathlib.Path(__file__).with_name("link_capture.py")),
              "plan": plan, "plan_sha256": digest(args.plan), "endpoints": {}, "runs": [],
              "privacy": "Private allow TX and transaction RX omitted; generated session auth values redacted from public records.",
              "limitations": ["Synthetic digital source and receiver reconstruction; no microphone/DAC/analog quality measurement.",
                  "Packetized ESP-NOW broadcast does not implement a continuous RF PDM bitstream; that is a separate offline impairment model.",
                  "No synchronized one-way latency measurement. Receiver queue/deadline ages use its local scheduling domain.",
                  "Local source/image digests require separate owner flash receipts; target source/profile checks are not binary attestation."]}
    endpoints = {}
    try:
        for name in ("cyd", "peer"):
            endpoints[name] = AudioEndpoint(name, identities[name], args.output / name, guard)
            endpoints[name].wait(.2)
            result["endpoints"][name] = verify_identity(endpoints[name])
        save(result, args.output, guard)
        number = 0
        for case in plan["workloads"]:
            for repetition in range(1, plan.get("repetitions", 3) + 1):
                number += 1
                run = {"run_id": f"a{number:03}_{repetition}", "workload": case["name"], "settings": case,
                       "repetition": repetition, "status": "IN PROGRESS"}
                result["runs"].append(run)
                print(run["run_id"], case["name"], flush=True)
                cursors = {name: len(endpoint.records) for name, endpoint in endpoints.items()}
                try:
                    execute_run(run, endpoints, plan, guard, result["endpoints"])
                except (RuntimeError, TimeoutError, ValueError) as error:
                    run.update(status="FAILED", error=guard.clean(str(error)), partial_target={
                        name: endpoint.records[cursors[name]:] for name, endpoint in endpoints.items()})
                    for endpoint in endpoints.values():
                        try:
                            endpoint.audio("perf audio stop", timeout=2)
                        except Exception:
                            pass
                    run["post_failure_export"] = {}
                    for name, endpoint in endpoints.items():
                        try:
                            records = endpoint.audio("perf audio export", "audio_summary", timeout=3)
                            run["post_failure_export"][name] = records
                        except Exception:
                            run["post_failure_export"][name] = {"status": "UNAVAILABLE"}
                    if any(endpoint.errors for endpoint in endpoints.values()):
                        raise RuntimeError("Audio telemetry corruption prevents safe subsequent case correlation")
                save(result, args.output, guard)
        result["execution_complete"] = True
        result["status"] = "CAPTURE COMPLETE" if all(run["status"] == "MEASURED" for run in result["runs"]) else "FAILED"
        result["run_status_counts"] = dict(collections.Counter(run["status"] for run in result["runs"]))
    except BaseException as error:
        result["status"] = "INTERRUPTED" if isinstance(error, KeyboardInterrupt) else "FAILED"
        result["error"] = guard.clean(str(error))
        raise
    finally:
        for endpoint in endpoints.values():
            try:
                endpoint.audio("perf audio stop", timeout=2)
                endpoint.request("perf link mode off 0", timeout=2)
            except Exception:
                pass
            endpoint.close()
        result["ended_utc"] = utc_now()
        save(result, args.output, guard)
        hashes = {path.relative_to(args.output).as_posix(): digest(path) for path in sorted(args.output.rglob("*")) if path.is_file()}
        (args.output / "artifact-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n", encoding="utf-8")
    return 0 if result["status"] == "CAPTURE COMPLETE" else 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    child = commands.add_parser("plan")
    child.add_argument("--output", type=pathlib.Path, required=True)
    child.add_argument("--reverse", action="store_true", help="Add CYD-to-peer after the default peer-to-CYD matrix")
    child.add_argument("--include-pdm", action="store_true", help="Add optional packetized-PDM comparisons; not a proprietary SDR TX/RX prototype")
    child.add_argument("--defer-radio-metadata", action="store_true", help="Explicit opt-in: require start ACK, defer radio-on metadata and stop before export")
    child = commands.add_parser("capture")
    child.add_argument("--identities", type=pathlib.Path, required=True)
    child.add_argument("--plan", type=pathlib.Path, required=True)
    child.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    if args.command == "plan":
        plan = default_plan(args.reverse, args.include_pdm)
        if args.defer_radio_metadata:
            plan["defer_radio_metadata"] = True
        validate_plan(plan)
        args.output.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
        return 0
    try:
        return capture(args)
    except KeyboardInterrupt:
        print("Audio capture interrupted; redacted partial evidence retained.", file=sys.stderr)
        return 130
    except Exception:
        print("Audio capture failed; inspect redacted partial evidence or validate identified images.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
