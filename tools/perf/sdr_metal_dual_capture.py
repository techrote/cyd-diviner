#!/usr/bin/env python3
"""One finite metal TX exposure, two independent native snapshot receivers."""
from __future__ import annotations

import argparse
import binascii
import copy
import hashlib
import json
import math
import pathlib
import re
import secrets
import time

from capture import utc_now
from link_capture import SecretGuard
from sdr_capture import SDRPort, digest
from sdr_metal_capture import (MetalRX, default_plan, validate_plan, validate_snapshot,
                               validate_tx_cleanup, validate_sensor_cleanup,
                               tx_output_transport_info, validate_tx_transport, require_fast_capability,
                               require_source_integrity_capability, SourceIntegrityError)

RECEIVERS = ("rx", "rx2")


def read_identities(path):
    identities = json.loads(path.read_text(encoding="utf-8-sig"))
    if identities.get("schema") != "cyd-sdr-metal-dual-identities/1":
        raise ValueError("Expected cyd-sdr-metal-dual-identities/1 with explicit tx/rx/rx2 identities")
    for name in ("tx", *RECEIVERS):
        entry = identities.get(name, {})
        expected = entry.get("expected_info", {})
        profiles = ("classic-sdr-metal-tx", "s3-sdr-metal-tx") if name == "tx" else ("s3-metal-snapshot",)
        if (expected.get("profile") not in profiles
            or not re.fullmatch(r"[0-9a-f]{40}", expected.get("source_sha", ""))
            or not re.fullmatch(r"[0-9a-f]{64}", expected.get("tx_source_digest" if name == "tx" else "source_digest", ""))
            or not re.fullmatch(r"[0-9a-fA-F]{12}", expected.get("mac", ""))
            or not re.fullmatch(r"[0-9a-f]{64}", entry.get("firmware_sha256", ""))
            or not re.fullmatch(r"COM[1-9][0-9]*", entry.get("port", ""), re.I)
            or type(entry.get("baud")) is not int or not 115200 <= entry["baud"] <= 3000000):
            raise ValueError("Each board requires exact profile/source/digest/MAC, image SHA256 and confirmed Windows port")
        image = pathlib.Path(entry["firmware_file"])
        if not image.is_absolute(): image = path.parent / image
        if not image.is_file() or digest(image) != entry["firmware_sha256"]:
            raise ValueError("Metal image differs from the owner-identified file on " + name)
        entry["firmware_file"] = str(image.resolve())
    if (len({identities[n]["port"].upper() for n in ("tx", *RECEIVERS)}) != 3
        or len({identities[n]["expected_info"]["mac"].lower() for n in ("tx", *RECEIVERS)}) != 3):
        raise ValueError("Three distinct owner-identified ports and MACs required")
    return identities


def dual_plan():
    plan = default_plan(with_tx=True)
    plan["transmitter"]["trace_capacity"] = 1024
    for case in plan["workloads"]:
        case["arm_delay_us"] = 500000
        if case["tx_enabled"]: case["duration_us"] = 1000000
    return plan


def temperature(endpoint, receiver=False, enabled=True):
    if not enabled: return {"status": "NOT MEASURED", "reason": "disabled by plan"}
    is_s3 = receiver or endpoint.identity["expected_info"]["profile"] == "s3-sdr-metal-tx"
    record = endpoint.request("metal temperature" if receiver else "temperature" if is_s3 else "info",
        lambda r: r.get("type") == ("temperature" if is_s3 else "info"), records=True)
    if is_s3: validate_sensor_cleanup(record)
    value = record.get("temperature_c_driver" if is_s3 else "temperature_raw")
    return {"status": "RECORDED" if type(value) in (int, float) and math.isfinite(value) else "NOT MEASURED",
            "scope": "outside all timed RF/snapshot loops", "target": record}


def validate_tx(records, case, plan):
    results = [r for r in records if r.get("type") == "tx_result"]
    ends = [r for r in records if r.get("type") == "tx_end"]
    traces = [r for r in records if r.get("type") == "tx_trace"]
    segments = [r for r in records if r.get("type") == "tx_segment"]
    if len(results) != 1 or len(ends) != 1:
        raise RuntimeError("Shared TX result/terminal boundary missing or ambiguous")
    result = results[0]
    expected = {"backend_available": True, "mode": case["tx_mode"], "requested_baud": case["baud"],
        "requested_duration_us": case["duration_us"], "requested_mask_segment_us": case["mask_segment_us"],
        "backoff0_quarter_db": case["backoff0_quarter_db"], "backoff1_quarter_db": case["backoff1_quarter_db"],
        "unpaced": case["baud"] == 0, "trace_stride": plan["transmitter"]["trace_stride"],
        "init_path": "experimental_direct_PHY" if plan["transmitter"]["init_path"] == "phy" else "WiFi_NULL",
        "pattern_bits": case["bit_count"], "trace_count": len(traces), "segment_records": len(segments)}
    if (any(result.get(k) != v for k, v in expected.items()) or not result.get("processed_bits")
        or result.get("restored_word_readback") != result.get("restored_word_requested")
        or ends[0].get("trace_records") != len(traces) or ends[0].get("segment_records") != len(segments)):
        raise RuntimeError("Shared TX plan/trace/register restoration correlation failed")
    validate_tx_cleanup(result)
    return result


def execute_dual_run(run, ports, plan, plan_path, folders, guard):
    """Both arm commands precede TX; drain all three endpoints until finite boundaries."""
    case, run_id, tx = run["settings"], run["run_id"], ports["tx"]
    if case["arm_delay_us"] != 500000:
        raise ValueError("Dual-receiver experiment requires explicit 500,000us arm delay")
    cursors = {name: len(port.records) for name, port in ports.items()}
    output_before = tx_output_transport_info(tx.records)
    if case.get("tx_enabled") and case.get("tx_mode") == "fast": require_fast_capability(output_before)
    if case.get("tx_enabled") and (output_before or {}).get("source_integrity_supported") is True: require_source_integrity_capability(output_before)
    snapshot_cursors = {name: len(ports[name].decoder.snapshots) for name in RECEIVERS}
    received, failures = {}, {}
    run["receivers"] = {name: {"status": "IN PROGRESS", "shared_exposure_id": run_id} for name in RECEIVERS}
    for name in RECEIVERS:
        folders[name].mkdir()
        def save(receipt, payload, name=name):
            path = folders[name] / f"snapshot-{len(ports[name].decoder.snapshots) - snapshot_cursors[name]:04}.native32le"
            path.write_bytes(payload); receipt.update(file=path.name, sha256=digest(path))
        ports[name].decoder.sink = save
    run["temperature_before"] = {n: temperature(p, n != "tx", plan.get("temperature_queries", True)) for n, p in ports.items()}
    permit = None
    if case["tx_enabled"]:
        if case.get("pattern") == "alternating": packed = b"\x55" * ((case["bit_count"] + 7) // 8)
        else:
            source = pathlib.Path(case["bits_file"])
            if not source.is_absolute(): source = plan_path.parent / source
            packed = source.read_bytes()
        if len(packed) != (case["bit_count"] + 7) // 8: raise ValueError("Packed bit count differs")
        run["uploaded_bits_sha256"] = hashlib.sha256(packed).hexdigest()
        for folder in folders.values(): (folder / "uploaded-bits.bin").write_bytes(packed)
        tx.request(f"load {case['bit_count']} {binascii.crc32(packed) & 0xffffffff:08x}", lambda r: r.get("type") == "ready", records=True)
        cursor = len(tx.records); tx.log("upload", bytes=len(packed), bit_count=case["bit_count"], packed_MSB_first=True)
        tx.write(packed)
        loaded = tx.until(lambda r: r.get("type") == "loaded", cursor, records=True)
        if loaded.get("bits") != case["bit_count"]: raise RuntimeError("Loaded pattern receipt differs")
        permit = secrets.token_hex(4); guard.add(permit); guard.add(permit.upper())
        tx.request("permit " + permit, lambda r: r.get("type") == "permit" and r.get("ok") is True, records=True, private=True)
    # Never wait on one receiver while the other is already in its finite arm delay.
    host_arm_sent = {}
    for name in RECEIVERS:
        host_arm_sent[name] = time.perf_counter()
        ports[name].send(f"metal {case['rx_mode']} {run_id} {case['rate_hz']} {case['samples']} {case['rx_repeats']} "
                         f"{case['arm_delay_us']} {case['gap_us']} {case['timeout_us']}")
    arm_deadline = min(host_arm_sent.values()) + .45
    sent_tx = False
    def pump():
        progress = False
        for name, port in ports.items():
            try:
                if name in failures and name != "tx":
                    # A failed binary boundary stays raw-only; no recovery/query is injected.
                    progress = bool(SDRPort.read(port, binary=True, nonblocking=True)) or progress
                else: progress = bool(port.read(nonblocking=True)) or progress
            except (ValueError, RuntimeError) as error:
                failures[name] = str(error)
        for name, port in ports.items():
            for record in port.records[cursors[name]:]:
                if record.get("type") in ("error", "radio_error"):
                    failures[name] = "Metal target error: " + json.dumps(record)
        if not progress: time.sleep(.001)
    while time.perf_counter() < arm_deadline:
        pump()
        for name in RECEIVERS:
            matches = [r for r in ports[name].records[cursors[name]:] if r.get("type") == "armed" and r.get("id") == run_id]
            if matches:
                armed = matches[-1]
                expected = {"mode": case["rx_mode"], "sample_count": case["samples"], "nominal_rate_hz": case["rate_hz"],
                    "repeats_requested": case["rx_repeats"], "arm_delay_us": case["arm_delay_us"],
                    "inter_snapshot_gap_us": case["gap_us"], "timeout_us": case["timeout_us"]}
                if any(armed.get(k) != v for k, v in expected.items()): failures[name] = "Armed receipt differs from plan"
                received[name] = armed
        if len(received) == 2 or failures: break
    if len(received) == 2 and not failures and time.perf_counter() < arm_deadline:
        if case["tx_enabled"]:
            tx.send(f"tx {case['tx_mode']} {case['baud']} {case['backoff0_quarter_db']} {case['backoff1_quarter_db']} "
                    f"{case['duration_us']} {case['mask_segment_us']} {permit}", private=True)
            sent_tx = True
    else:
        for name in RECEIVERS:
            if name not in received: failures.setdefault(name, "Matching armed ACK missing before the 450ms host dispatch bound")
        if case["tx_enabled"]:
            failures["tx"] = "TX NOT RUN: both matching armed ACKs were not received within the 450ms host dispatch bound"
    run["arm_host_timing"] = {"scope": "host monotonic dispatch only; independent board clocks are not synchronized",
        "arm_send_separation_us": abs(host_arm_sent["rx2"] - host_arm_sent["rx"]) * 1e6,
        "both_ack_wait_to_TX_dispatch_us": (time.perf_counter() - min(host_arm_sent.values())) * 1e6,
        "TX_dispatched_once": sent_tx, "arm_receipts": received}
    duration = (case["arm_delay_us"] + case["rx_repeats"] * (case["timeout_us"] + case["gap_us"])) / 1e6
    deadline = time.perf_counter() + duration + (case["rx_repeats"] * 5 if case["rx_mode"] == "capture" else 0) + 15
    ends = {}
    while time.perf_counter() < deadline:
        pump()
        for name in RECEIVERS:
            found = [r for r in ports[name].records[cursors[name]:] if r.get("type") == "run_end" and r.get("id") == run_id]
            if found: ends[name] = found[-1]
        tx_done = not sent_tx or any(r.get("type") == "tx_end" for r in tx.records[cursors["tx"]:])
        if all(n in ends or n in failures for n in RECEIVERS) and (tx_done or "tx" in failures): break
    else:
        for name in RECEIVERS:
            if name not in ends: failures.setdefault(name, "Missing finite receiver terminal boundary; no recovery command injected")
        if sent_tx and not any(r.get("type") == "tx_end" for r in tx.records[cursors["tx"]:]): failures["tx"] = "Missing TX terminal boundary"
    if sent_tx and "tx" not in failures:
        try:
            run["transmitter_result"] = validate_tx(tx.records[cursors["tx"]:], case, plan)
            output_after = None
            if (output_before or {}).get("output_transport") is not None:
                # All finite endpoints have ended, and legacy cleanup passed.
                # Only this fresh outside-window info can attest tx_end's flush.
                run["transmitter_output_before"] = output_before
                output_after = tx.request("info", lambda r: r.get("type") == "info", records=True)
                run["transmitter_output_after"] = output_after
            run["transmitter_output_validation"] = validate_tx_transport(tx.records[cursors["tx"]:], output_before, output_after, packed)
        except (RuntimeError, TimeoutError, ValueError, OSError) as error:
            if isinstance(error, SourceIntegrityError): run["transmitter_source_validation"] = error.validation
            failures["tx"] = str(error)
    # A terminal receiver may be read even if the other receiver failed.
    for name in RECEIVERS:
        port, child = ports[name], run["receivers"][name]
        try:
            if name in failures: raise RuntimeError(failures[name])
            end = ends.get(name, {})
            if (end.get("ok") is not True or end.get("captures_completed") != case["rx_repeats"]
                or end.get("sample_count_each") != case["samples"] or end.get("nominal_rate_hz") != case["rate_hz"]
                or end.get("last_owner_restored") is not True or end.get("last_canary_ok") is not True
                or end.get("transfer_failures_delta") != 0): raise RuntimeError("RX terminal count/canary/restoration/transfer proof failed")
            if case["rx_mode"] == "bench":
                cursor = len(port.decoder.snapshots); read_id = run_id + "_last"
                port.send("metal read " + read_id)
                deadline = time.perf_counter() + 15
                while len(port.decoder.snapshots) == cursor and time.perf_counter() < deadline: pump()
                if len(port.decoder.snapshots) != cursor + 1: raise TimeoutError("Deferred native snapshot read incomplete")
                header = port.decoder.snapshots[-1]["header"]
                if header.get("id") != read_id or header.get("repetition") != 0: raise RuntimeError("Deferred read ID differs")
            snapshots = port.decoder.snapshots[snapshot_cursors[name]:]
            if len(snapshots) != (1 if case["rx_mode"] == "bench" else case["rx_repeats"]): raise RuntimeError("Snapshot count differs")
            for index, receipt in enumerate(snapshots, 1):
                validate_snapshot(receipt, run_id, case, case["rx_mode"] == "bench")
                if case["rx_mode"] == "capture" and receipt["header"].get("repetition") != index:
                    raise RuntimeError("Receiver snapshot repetitions are duplicate or out of order")
            child.update(status="ACQUIRED", receiver_run_end=end, snapshots=snapshots, armed=received.get(name), physical_rf_verified=None)
        except (RuntimeError, ValueError, TimeoutError) as error:
            failures[name] = str(error); child.update(status="FAILED", error=str(error))
        child["snapshots"] = port.decoder.snapshots[snapshot_cursors[name]:]
        child["receiver_run_end"] = ends.get(name)
        child["armed"] = received.get(name)
        child["rx_target"] = port.records[cursors[name]:]
        port.decoder.sink = None
    run["tx_target"] = tx.records[cursors["tx"]:]
    # Queries are safe only after each endpoint's own complete terminal boundary.
    run["temperature_after"] = {}
    for name, port in ports.items():
        safe = (name == "tx" and name not in failures and (not sent_tx or any(r.get("type") == "tx_end" for r in run["tx_target"]))) or (name in ends and name not in failures)
        run["temperature_after"][name] = temperature(port, name != "tx", plan.get("temperature_queries", True)) if safe else {
            "status": "NOT MEASURED", "reason": "uncertain or failed terminal boundary; no query injected"}
    run["failures"] = failures
    run["status"] = "ACQUIRED" if not failures else "FAILED"
    run["physical_rf_verified"] = None
    run["snapshot_scope"] = "Independent native bursts, each resets sample index0; one shared TX exposure; no clock synchronization"
    return not failures


def capture(args):
    if not args.rf_authorized: raise ValueError("Explicit --rf-authorized required following owner approval")
    identities = read_identities(args.identities)
    plan = json.loads(args.plan.read_text(encoding="utf-8-sig")); validate_plan(plan)
    if any(c["arm_delay_us"] != 500000 for c in plan["workloads"]): raise ValueError("Set every dual case arm_delay_us to 500000")
    args.output.mkdir(parents=True, exist_ok=False)
    ports, guard, children = {}, SecretGuard(), {}
    result = {"schema": "cyd-sdr-metal-dual-capture/1", "evidence_class": "physical_dual_snapshot_acquisition",
        "status": "IN PROGRESS", "started_utc": utc_now(), "identities": identities, "plan": plan,
        "plan_sha256": digest(args.plan), "harness_sha256": digest(pathlib.Path(__file__)),
        "modules_sha256": {n: digest(pathlib.Path(__file__).with_name(n)) for n in ("sdr_metal_capture.py", "sdr_capture.py")},
        "operator_rf_authorization_flag": True, "runs": [], "limitations": [
            "One TX exposure is shared; receiver observations are not separate RF trials.",
            "Arming/host dispatch is bounded but clocks are independent; IQ must establish actual RF presence/overlap.",
            "Per-receiver CRC proves delivered USB payload integrity, not RF packet reception or loss.",
            "Native clocks and software loop completion are not independently calibrated RF rates or edges."]}
    def write_results():
        (args.output / "results.json").write_text(json.dumps(guard.clean(result), indent=2) + "\n", encoding="utf-8")
        for name, child in children.items():
            (args.output / name / "results.json").write_text(json.dumps(guard.clean(child), indent=2) + "\n", encoding="utf-8")
    try:
        ports["tx"] = SDRPort("tx", identities["tx"], args.output / "tx", guard)
        for name in RECEIVERS:
            root = args.output / name; root.mkdir()
            ports[name] = MetalRX(name, identities[name], root / "rx", guard)
            children[name] = {"schema": "cyd-sdr-metal-capture/1", "evidence_class": "physical_snapshot_acquisition",
                "status": "IN PROGRESS", "started_utc": result["started_utc"], "identities": {"tx": identities["tx"], "rx": identities[name]},
                "plan": plan, "plan_sha256": result["plan_sha256"], "harness_sha256": result["harness_sha256"], "runs": [],
                "shared_exposure": {"dual_manifest": "../results.json", "receiver_name": name, "tx_raw_folder": "../tx", "independent_RF_trials": False}}
        for name, port in ports.items():
            info = port.request("info" if name == "tx" else "metal info", lambda r: r.get("type") == "info", records=True)
            if any(info.get(k) != v for k, v in identities[name]["expected_info"].items()): raise RuntimeError("Runtime source/profile/digest/MAC differs on " + name)
            result.setdefault("target_info", {})[name] = info
        config = plan["receiver"]
        for name in RECEIVERS:
            port = ports[name]; init_path = config.get("init_path", "phy")
            initialized = port.request("metal init " + init_path, lambda r: r.get("type") == "init", records=True)
            if (initialized.get("ok") is not True or initialized.get("requested") != init_path
                or initialized.get("actual_mode") != ("direct_PHY" if init_path == "phy" else "WiFi_NULL") or initialized.get("error_code") != 0):
                raise RuntimeError("RX init proof failed on " + name)
            tune = port.request(f"metal tune {config['frequency_mhz']} {config['offset_khz']} {config['manual_gain_index']}", lambda r: r.get("type") == "tune", records=True)
            if tune.get("ok") is not True or any(tune.get(k) != config[k] for k in ("frequency_mhz", "offset_khz", "manual_gain_index")): raise RuntimeError("RX tuning differs on " + name)
            children[name].update(target_info={"tx": result["target_info"]["tx"], "rx": result["target_info"][name]}, receiver_init=initialized, receiver_tune=tune)
        if any(c["tx_enabled"] for c in plan["workloads"]):
            config = plan["transmitter"]
            for command, kind in ((f"reserve {config['symbol_capacity']} {config['trace_capacity']}", "reserved"),
                                  (f"observe {config['trace_stride']}", "observe"), ("init " + config["init_path"], "init_path")):
                ports["tx"].request(command, lambda r, k=kind: r.get("type") == k and r.get("ok") is True, records=True)
            info = ports["tx"].request("info", lambda r: r.get("type") == "info", records=True)
            if any(info.get(k) != v for k, v in (("capacity_symbols", config["symbol_capacity"]), ("trace_capacity", config["trace_capacity"]), ("trace_stride", config["trace_stride"]))): raise RuntimeError("Actual TX preallocation/observer differs")
            result["transmitter_setup_info"] = info
        for case in plan["workloads"]:
            for rep in range(1, plan.get("repetitions", 1) + 1):
                run_id = f"d{len(result['runs']) + 1:03}_{rep}"
                run = {"run_id": run_id, "workload": case["name"], "repetition": rep, "settings": case, "status": "IN PROGRESS"}
                result["runs"].append(run)
                try:
                    ok = execute_dual_run(run, ports, plan, args.plan, {n: args.output / n / run_id for n in RECEIVERS}, guard)
                except BaseException as error:
                    run.update(status="FAILED", error=guard.clean(str(error)), partial_target={n: p.records for n, p in ports.items()})
                    raise
                finally:
                    for name in RECEIVERS:
                        own = run.get("receivers", {}).get(name, {"status": "FAILED", "error": "dual execution did not complete"})
                        if own.get("status") == "IN PROGRESS":
                            own = {**own, "status": "FAILED", "error": run.get("error", "receiver validation did not complete")}
                        child_run = {k: copy.deepcopy(v) for k, v in run.items() if k not in ("receivers", "temperature_before", "temperature_after")}
                        child_run.update(copy.deepcopy(own))
                        for key in ("temperature_before", "temperature_after"):
                            child_run[key] = {"tx": run.get(key, {}).get("tx"), "rx": run.get(key, {}).get(name)}
                        # TX failure invalidates coupled exposure status, while preserving good receiver-native data.
                        if run.get("failures", {}).get("tx"): child_run.update(status="FAILED", error=run["failures"]["tx"])
                        children[name]["runs"].append(child_run)
                    write_results()
                if not ok: raise RuntimeError("Dual case failed; raw and independent receiver results retained; campaign stopped")
        result["status"] = "CAPTURE COMPLETE"
    except BaseException as error:
        result.update(status="INTERRUPTED" if isinstance(error, KeyboardInterrupt) else "FAILED", error=guard.clean(str(error)))
        raise
    finally:
        for port in ports.values(): port.close()
        result["ended_utc"] = utc_now()
        for name, child in children.items():
            complete = len(child["runs"]) == len(plan["workloads"]) * plan.get("repetitions", 1) and all(r["status"] == "ACQUIRED" for r in child["runs"])
            child.update(status="CAPTURE COMPLETE" if complete else result["status"] if result["status"] != "CAPTURE COMPLETE" else "FAILED", ended_utc=result["ended_utc"])
            child["shared_tx_artifacts_sha256"] = {p.relative_to(args.output).as_posix(): digest(p)
                for p in sorted((args.output / "tx").rglob("*")) if p.is_file()}
        write_results()
        for root in [*(args.output / n for n in RECEIVERS), args.output]:
            if not root.is_dir(): continue
            hashes = {p.relative_to(root).as_posix(): digest(p) for p in sorted(root.rglob("*")) if p.is_file() and p != root / "artifact-hashes.json"}
            (root / "artifact-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n", encoding="utf-8")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("plan"); p.add_argument("--output", type=pathlib.Path, required=True)
    p = sub.add_parser("capture"); p.add_argument("--identities", type=pathlib.Path, required=True)
    p.add_argument("--plan", type=pathlib.Path, required=True); p.add_argument("--output", type=pathlib.Path, required=True)
    p.add_argument("--rf-authorized", action="store_true")
    args = parser.parse_args()
    if args.command == "plan":
        plan = dual_plan(); validate_plan(plan)
        args.output.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8"); return 0
    return capture(args)


if __name__ == "__main__": raise SystemExit(main())
