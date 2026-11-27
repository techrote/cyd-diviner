#!/usr/bin/env python3
"""Owned-board finite SDR-metal experiments with deferred native snapshot RX."""
from __future__ import annotations

import argparse
import binascii
import csv
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

RAW_FORMAT = "LE32_I10_Q10_metadata12_native"
STATUS_NOT_CALLED = -2147483648


def validate_sensor_cleanup(record):
    keys = ("install_status", "enable_status", "read_status", "disable_status", "uninstall_status")
    if any(type(record.get(key)) is not int for key in keys):
        raise RuntimeError("Temperature query lacks sensor lifecycle proof; stop/recover before continuing")
    if (record["disable_status"] not in (0, STATUS_NOT_CALLED) or record["uninstall_status"] not in (0, STATUS_NOT_CALLED)
        or (record["install_status"] == 0 and record["uninstall_status"] != 0)
        or (record["enable_status"] == 0 and record["disable_status"] != 0)):
        raise RuntimeError("Temperature sensor disable/uninstall failed; stop/recover before continuing")


class SnapshotDecoder:
    """METAL JSON, exact raw length and LF; no delimiter search in payloads."""
    def __init__(self, sink=None):
        self.pending, self.records, self.snapshots, self.text = bytearray(), [], [], []
        self.binary_header, self.sink = None, sink
        self.offset = 0

    def feed(self, data):
        self.pending.extend(data)
        while self.pending:
            if self.binary_header is not None:
                header = self.binary_header
                count = header["raw_bytes"]
                if len(self.pending) < count + 1:
                    return
                payload, following = bytes(self.pending[:count]), self.pending[count]
                actual = f"{binascii.crc32(payload) & 0xffffffff:08x}"
                receipt = {"header": header, "actual_crc32": actual, "crc_ok": actual == header.get("crc32"),
                           "following_lf_ok": following == 10, "payload_offset": self.offset}
                self.snapshots.append(receipt)
                if self.sink:
                    self.sink(receipt, payload)
                del self.pending[:count + 1]
                self.offset += count + 1
                self.binary_header = None
                if not receipt["following_lf_ok"]:
                    raise ValueError("Snapshot binary boundary has no following LF; raw retained")
                if not receipt["crc_ok"]:
                    raise ValueError("Snapshot CRC32 mismatch; raw retained")
                continue
            newline = self.pending.find(b"\n")
            if newline < 0:
                if len(self.pending) > 16384:
                    raise ValueError("Unterminated METAL JSON exceeds bounded header")
                return
            line = bytes(self.pending[:newline]).strip()
            del self.pending[:newline + 1]
            self.offset += newline + 1
            if not line.startswith(b"METAL "):
                self.text.append(line.decode("utf-8", errors="replace"))
                continue
            def reject_constant(value):
                raise ValueError("Nonfinite METAL JSON constant: " + value)
            record = json.loads(line[6:].decode(), parse_constant=reject_constant)
            if not isinstance(record, dict) or record.get("schema") != 1 or not isinstance(record.get("type"), str):
                raise ValueError("Invalid METAL schema/type")
            self.records.append(record)
            if record["type"] == "snapshot":
                n, size = record.get("sample_count"), record.get("raw_bytes")
                if (type(n) is not int or not 1 <= n <= 16380 or type(size) is not int or size != n * 4
                    or record.get("raw_format") != RAW_FORMAT or not re.fullmatch(r"[0-9a-f]{8}", record.get("crc32", ""))):
                    raise ValueError("Invalid native snapshot length/format/CRC metadata")
                self.binary_header = record


class MetalRX(SDRPort):
    def __init__(self, name, identity, folder, guard):
        super().__init__(name, identity, folder, guard)
        self.decoder = SnapshotDecoder()
        self.records = self.decoder.records
        self.target = (folder / "target.jsonl").open("w", encoding="utf-8", buffering=1)

    def read(self, binary=False, nonblocking=False):
        data = super().read(binary=True, nonblocking=nonblocking)
        cursor = len(self.records)
        try:
            if data:
                self.decoder.feed(data)
        finally:
            for record in self.records[cursor:]:
                self.target.write(json.dumps(record) + "\n")
        return data

    def close(self):
        self.target.close()
        super().close()


def read_identities(path):
    config = json.loads(path.read_text(encoding="utf-8-sig"))
    if config.get("schema") != "cyd-sdr-metal-identities/1":
        raise ValueError("Expected cyd-sdr-metal-identities/1 with explicit tx/rx source/image identities")
    for name, profiles, digest_key in (("tx", ("classic-sdr-metal-tx", "s3-sdr-metal-tx"), "tx_source_digest"), ("rx", ("s3-metal-snapshot",), "source_digest")):
        entry, expected = config.get(name, {}), config.get(name, {}).get("expected_info", {})
        if (expected.get("profile") not in profiles or not re.fullmatch(r"[0-9a-f]{40}", expected.get("source_sha", ""))
            or not re.fullmatch(r"[0-9a-f]{64}", expected.get(digest_key, ""))
            or not re.fullmatch(r"[0-9a-fA-F]{12}", expected.get("mac", ""))
            or not re.fullmatch(r"[0-9a-f]{64}", entry.get("firmware_sha256", ""))
            or not re.fullmatch(r"COM[1-9][0-9]*", entry.get("port", ""), re.I)
            or type(entry.get("baud")) is not int or not 115200 <= entry["baud"] <= 3000000):
            raise ValueError("Exact metal profile/source/digest/MAC, image hash and confirmed Windows port required")
        image = pathlib.Path(entry["firmware_file"])
        if not image.is_absolute(): image = path.parent / image
        if not image.is_file() or digest(image) != entry["firmware_sha256"]:
            raise ValueError("Metal firmware file differs from owner-identified image")
        entry["firmware_file"] = str(image.resolve())
    if config["tx"]["port"].upper() == config["rx"]["port"].upper():
        raise ValueError("Distinct owner-identified TX/RX ports required")
    return config


def default_plan(with_tx=False):
    cases = [{"name": f"rx-{mode}/{rate}", "rx_mode": mode, "rate_hz": rate, "samples": 16380,
              "rx_repeats": 20 if mode == "bench" else 1, "arm_delay_us": 100000,
              "gap_us": 0, "timeout_us": 20000, "tx_enabled": False}
             for mode in ("bench", "capture") for rate in (16000000, 40000000, 80000000)]
    if with_tx:
        for mode, baud, mask in (("api", 1000, 0), ("ram", 100000, 0), ("gate", 1000000, 0),
                                 ("gate", 0, 0), ("gate", 0, 1000)):
            cases.append({"name": f"tx-{mode}/{baud}/mask{mask}", "rx_mode": "capture", "rate_hz": 16000000,
                "samples": 16380, "rx_repeats": 1, "arm_delay_us": 100000, "gap_us": 0, "timeout_us": 20000,
                "tx_enabled": True, "tx_mode": mode, "baud": baud, "backoff0_quarter_db": 96,
                "backoff1_quarter_db": 112, "duration_us": 200000, "mask_segment_us": mask,
                "bit_count": 64, "pattern": "alternating"})
    return {"schema": "cyd-sdr-metal-plan/1", "repetitions": 1,
        "receiver": {"init_path": "phy", "frequency_mhz": 2433, "offset_khz": -80, "manual_gain_index": 40},
        "transmitter": {"init_path": "phy", "symbol_capacity": 64, "trace_capacity": 512, "trace_stride": 0},
        "temperature_queries": True,
        "rf_overlap_scope": "RX arm/TX setup use independent clocks; requested overlap is not synchronized proof; inspect native IQ",
        "workloads": cases}


def validate_plan(plan):
    if plan.get("schema") != "cyd-sdr-metal-plan/1" or not plan.get("workloads") or not 1 <= plan.get("repetitions", 1) <= 20:
        raise ValueError("Expected finite cyd-sdr-metal-plan/1")
    rx, tx = plan.get("receiver", {}), plan.get("transmitter", {})
    if (rx.get("init_path", "phy") not in ("phy", "wifi") or not 1 <= rx.get("frequency_mhz", 0) <= 65535 or not -1000 <= rx.get("offset_khz", 1001) <= 1000
        or not 0 <= rx.get("manual_gain_index", -1) <= 127 or tx.get("init_path") not in ("phy", "wifi")
        or not 1 <= tx.get("symbol_capacity", 0) <= 1000000 or not 0 <= tx.get("trace_capacity", -1) <= 100000
        or not 0 <= tx.get("trace_stride", -1) <= 0xffffffff):
        raise ValueError("Explicit tuning/preallocation/observer settings required")
    if "temperature_queries" in plan and type(plan["temperature_queries"]) is not bool:
        raise ValueError("temperature_queries is an explicit boolean")
    for case in plan["workloads"]:
        if (not case.get("name") or case.get("rx_mode") not in ("capture", "bench")
            or case.get("rate_hz") not in (16000000, 40000000, 80000000) or not 1 <= case.get("samples", 0) <= 16380
            or not 1 <= case.get("rx_repeats", 0) <= 10000 or not 0 <= case.get("arm_delay_us", -1) <= 5000000
            or not 0 <= case.get("gap_us", -1) <= 1000000 or not 1 <= case.get("timeout_us", 0) <= 1000000
            or type(case.get("tx_enabled")) is not bool):
            raise ValueError("Invalid finite RX snapshot/bench settings")
        if case["tx_enabled"]:
            if (case.get("tx_mode") not in ("api", "ram", "gate", "ask") or not 0 <= case.get("baud", -1) <= 240000000
                or not 32 <= case.get("backoff0_quarter_db", 0) <= 120 or not 32 <= case.get("backoff1_quarter_db", 0) <= 120
                or not 1 <= case.get("duration_us", 0) <= 2000000 or not 0 <= case.get("mask_segment_us", -1) <= 20000
                or not 1 <= case.get("bit_count", 0) <= tx["symbol_capacity"]
                or (case["mask_segment_us"] and case["tx_mode"] not in ("gate", "ask"))):
                raise ValueError("Invalid TX finite mode/rate/backoff/IRQ mask/preloaded pattern")
            if case["mask_segment_us"] and math.ceil(case["duration_us"] / case["mask_segment_us"]) + 1 > tx["trace_capacity"]:
                raise ValueError("Mask segment records exceed explicit preallocated capacity")
            if case.get("pattern") != "alternating" and not case.get("bits_file"):
                raise ValueError("TX requires explicit finite preloaded pattern")


def validate_snapshot(receipt, run_id, case, bench=False):
    header = receipt["header"]
    if (not receipt["crc_ok"] or not receipt["following_lf_ok"] or header.get("source_id") != run_id
        or header.get("sample_count") != case["samples"] or header.get("nominal_rate_hz") != case["rate_hz"]
        or any(header.get(key) is not True for key in ("complete", "hardware_done", "registers_restored", "tail_canary_ok"))
        or header.get("unchanged_sentinel_words") != 0):
        raise RuntimeError("Native snapshot CRC/canary/restore/source/count proof failed")
    expected_rep = case["rx_repeats"] if bench else header.get("repetition")
    if not 1 <= expected_rep <= case["rx_repeats"] or header.get("source_repetition") != expected_rep:
        raise RuntimeError("Snapshot source repetition correlation failed")
    if header.get("owner_before") != header.get("owner_after") or header.get("lanes_before") != header.get("lanes_after"):
        raise RuntimeError("Snapshot owner/lane readback mismatch")


def validate_tx_cleanup(result):
    if (result.get("teardown_status") != 0 or result.get("wifi_driver_owned_after") is not False
        or result.get("default_event_loop_owned_after") is not False
        or result.get("direct_power_domain_owned_after") is not False):
        raise RuntimeError("Metal TX teardown failed or driver/event-loop/direct-power-domain ownership remains or lacks proof")
    if result.get("radio_stop_api") == "WiFi_stop_deinit":
        # Pinned SDK ESP_ERR_WIFI_NOT_STARTED is0x3002. The firmware explicitly
        # accepts that already-stopped condition before successful deinit.
        if result.get("wifi_stop_status") not in (0, 0x3002) or result.get("wifi_deinit_status") != 0:
            raise RuntimeError("Metal Wi-Fi stop/deinit reported failure")


def execute_run(run, tx, rx, plan, plan_path, folder, guard):
    folder.mkdir()
    case, run_id = run["settings"], run["run_id"]
    rx_cursor, tx_cursor, snapshot_cursor = len(rx.records), len(tx.records), len(rx.decoder.snapshots)
    def save_snapshot(receipt, payload):
        filename = f"snapshot-{len(rx.decoder.snapshots) - snapshot_cursor:04}.native32le"
        path = folder / filename
        path.write_bytes(payload)
        receipt["file"] = filename
        receipt["sha256"] = digest(path)
    rx.decoder.sink = save_snapshot
    def temperature(endpoint, rx_sensor=False):
        if not plan.get("temperature_queries", True): return {"status": "NOT MEASURED", "reason": "disabled by plan"}
        is_s3 = rx_sensor or endpoint.identity["expected_info"]["profile"] == "s3-sdr-metal-tx"
        command = "metal temperature" if rx_sensor else "temperature" if is_s3 else "info"
        record = endpoint.request(command, lambda r: r.get("type") == ("temperature" if is_s3 else "info"), records=True)
        if is_s3: validate_sensor_cleanup(record)
        value = record.get("temperature_c_driver" if is_s3 else "temperature_raw")
        observed = type(value) in (int, float) and math.isfinite(value)
        return {"status": "RECORDED" if observed else "NOT MEASURED", "scope": "outside timed RF/snapshot loop", "target": record}
    run["temperature_before"] = {"tx": temperature(tx), "rx": temperature(rx, True)}
    if case["tx_enabled"]:
        if case.get("pattern") == "alternating":
            packed = b"\x55" * ((case["bit_count"] + 7) // 8)
        else:
            path = pathlib.Path(case["bits_file"])
            if not path.is_absolute(): path = plan_path.parent / path
            packed = path.read_bytes()
        if len(packed) != (case["bit_count"] + 7) // 8:
            raise ValueError("Preloaded packed pattern length differs from requested bit count")
        (folder / "uploaded-bits.bin").write_bytes(packed)
        run["uploaded_bits_sha256"] = hashlib.sha256(packed).hexdigest()
        tx.request(f"load {case['bit_count']} {binascii.crc32(packed) & 0xffffffff:08x}", lambda r: r.get("type") == "ready", records=True)
        loaded_cursor = len(tx.records)
        tx.log("upload", bytes=len(packed), bit_count=case["bit_count"], packed_MSB_first=True)
        tx.write(packed)
        loaded = tx.until(lambda r: r.get("type") == "loaded", loaded_cursor, records=True)
        if loaded.get("bits") != case["bit_count"]: raise RuntimeError("Metal loaded-bit receipt differs")
        permit = secrets.token_hex(4)
        guard.add(permit); guard.add(permit.upper())
        tx.request("permit " + permit, lambda r: r.get("type") == "permit" and r.get("ok") is True, records=True, private=True)
    command = (f"metal {case['rx_mode']} {run_id} {case['rate_hz']} {case['samples']} {case['rx_repeats']} "
               f"{case['arm_delay_us']} {case['gap_us']} {case['timeout_us']}")
    armed = rx.request(command, lambda r: r.get("type") == "armed" and r.get("id") == run_id, records=True)
    if any(armed.get(field) != value for field, value in (("mode", case["rx_mode"]), ("sample_count", case["samples"]),
        ("nominal_rate_hz", case["rate_hz"]), ("repeats_requested", case["rx_repeats"]), ("arm_delay_us", case["arm_delay_us"]),
        ("inter_snapshot_gap_us", case["gap_us"]), ("timeout_us", case["timeout_us"]))):
        raise RuntimeError("RX armed receipt differs from explicit plan")
    run["armed"] = armed
    if case["tx_enabled"]:
        tx.send(f"tx {case['tx_mode']} {case['baud']} {case['backoff0_quarter_db']} {case['backoff1_quarter_db']} "
                f"{case['duration_us']} {case['mask_segment_us']} {permit}", private=True)
    expected_duration = (case["arm_delay_us"] + case["rx_repeats"] * (case["timeout_us"] + case["gap_us"])) / 1e6
    if case["rx_mode"] == "capture": expected_duration += case["rx_repeats"] * 5
    deadline = time.perf_counter() + expected_duration + 15
    while time.perf_counter() < deadline:
        rx.read()
        tx.read(nonblocking=True)
        recent_rx, recent_tx = rx.records[rx_cursor:], tx.records[tx_cursor:]
        for record in recent_rx + recent_tx:
            if record.get("type") in ("error", "radio_error"):
                raise RuntimeError("Metal operation failed: " + json.dumps(record))
        ended_rx = [r for r in recent_rx if r.get("type") == "run_end" and r.get("id") == run_id]
        ended_tx = [r for r in recent_tx if r.get("type") == "tx_end"]
        if ended_rx and (not case["tx_enabled"] or ended_tx): break
    else:
        raise TimeoutError("Missing finite metal terminal boundary; raw data retained; no recovery commands injected")
    run["receiver_run_end"] = ended_rx[-1]
    end = ended_rx[-1]
    if (end.get("ok") is not True or end.get("captures_completed") != case["rx_repeats"]
        or end.get("sample_count_each") != case["samples"] or end.get("nominal_rate_hz") != case["rate_hz"]
        or end.get("last_owner_restored") is not True or end.get("last_canary_ok") is not True
        or end.get("transfer_failures_delta") != 0):
        raise RuntimeError("Metal RX run completion/canary/restore/transfer proof failed")
    if case["rx_mode"] == "bench":
        read_id = run_id + "_last"
        cursor = len(rx.decoder.snapshots)
        rx.send("metal read " + read_id)
        deadline = time.perf_counter() + 15
        while time.perf_counter() < deadline:
            rx.read()
            tx.read(nonblocking=True)
            if any(record.get("type") == "error" for record in rx.records[rx_cursor:]):
                raise RuntimeError("Deferred snapshot read failed; error retained")
            if len(rx.decoder.snapshots) > cursor: break
        else: raise TimeoutError("Deferred last-snapshot read did not complete")
        last = rx.decoder.snapshots[-1]["header"]
        if last.get("id") != read_id or last.get("repetition") != 0:
            raise RuntimeError("Deferred read request/source identity mismatch")
    snapshots = rx.decoder.snapshots[snapshot_cursor:]
    if len(snapshots) != (1 if case["rx_mode"] == "bench" else case["rx_repeats"]):
        raise RuntimeError("Finalized snapshot count differs from selected capture/read mode")
    for receipt in snapshots: validate_snapshot(receipt, run_id, case, case["rx_mode"] == "bench")
    run["snapshots"] = snapshots
    run["rx_target"] = rx.records[rx_cursor:]
    run["tx_target"] = tx.records[tx_cursor:]
    if case["tx_enabled"]:
        results = [r for r in run["tx_target"] if r.get("type") == "tx_result"]
        traces = [r for r in run["tx_target"] if r.get("type") == "tx_trace"]
        segments = [r for r in run["tx_target"] if r.get("type") == "tx_segment"]
        if len(results) != 1: raise RuntimeError("Metal TX result missing or ambiguous")
        result = results[0]
        if (result.get("backend_available") is not True or result.get("mode") != case["tx_mode"]
            or result.get("requested_baud") != case["baud"] or result.get("requested_duration_us") != case["duration_us"]
            or result.get("requested_mask_segment_us") != case["mask_segment_us"]
            or result.get("backoff0_quarter_db") != case["backoff0_quarter_db"]
            or result.get("backoff1_quarter_db") != case["backoff1_quarter_db"]
            or result.get("unpaced") is not (case["baud"] == 0)
            or result.get("trace_stride") != plan["transmitter"]["trace_stride"]
            or result.get("init_path") != ("experimental_direct_PHY" if plan["transmitter"]["init_path"] == "phy" else "WiFi_NULL")
            or result.get("pattern_bits") != case["bit_count"] or not result.get("processed_bits")
            or len(traces) != result.get("trace_count") or len(segments) != result.get("segment_records")
            or result.get("restored_word_readback") != result.get("restored_word_requested")):
            raise RuntimeError("Metal TX mode/timing/trace/restore correlation failed")
        validate_tx_cleanup(result)
        if ended_tx[-1].get("trace_records") != len(traces) or ended_tx[-1].get("segment_records") != len(segments):
            raise RuntimeError("Metal TX terminal record differs from observed trace/segment counts")
        run["transmitter_result"] = result
    run["physical_rf_verified"] = None
    run["temperature_after"] = {"tx": temperature(tx), "rx": temperature(rx, True)}
    run["status"] = "ACQUIRED"
    run["snapshot_scope"] = "Separate native SRAM snapshots; sample index resets0 per burst, deferred USB outside capture"
    rx.decoder.sink = None


def capture(args):
    if not args.rf_authorized:
        raise ValueError("Explicit --rf-authorized required after owner approval of new images/physical experiment")
    identities = read_identities(args.identities)
    plan = json.loads(args.plan.read_text(encoding="utf-8-sig")); validate_plan(plan)
    args.output.mkdir(parents=True, exist_ok=False)
    ports, guard = {}, SecretGuard()
    result = {"schema": "cyd-sdr-metal-capture/1", "evidence_class": "physical_snapshot_acquisition", "status": "IN PROGRESS",
        "started_utc": utc_now(), "identities": identities, "plan": plan, "plan_sha256": digest(args.plan),
        "harness_sha256": digest(pathlib.Path(__file__)), "serial_module_sha256": digest(pathlib.Path(__file__).with_name("sdr_capture.py")),
        "operator_rf_authorization_flag": True, "runs": [], "limitations": [
            "Configured native sample clock/horizon is not an independently calibrated rate or continuous stream.",
            "MMIO/call loop completion and achieved writes/s are software observations, not measured RF edge limits.",
            "Local CPU IRQ masking excludes only specified interrupt levels; actual mask/gap durations remain measured.",
            "Separate board clocks and serial command ordering do not establish RF overlap or one-way latency.",
            "No-export bench occupancy and capture-with-USB deadtime are separate cohorts. Inspect raw IQ for signal claims."]}
    try:
        ports["tx"] = SDRPort("tx", identities["tx"], args.output / "tx", guard)
        ports["rx"] = MetalRX("rx", identities["rx"], args.output / "rx", guard)
        for name, command in (("tx", "info"), ("rx", "metal info")):
            info = ports[name].request(command, lambda r: r.get("type") == "info", records=True)
            if any(info.get(key) != value for key, value in identities[name]["expected_info"].items()):
                raise RuntimeError("Metal runtime source/profile/digest/MAC mismatch on " + name)
            result.setdefault("target_info", {})[name] = info
        tx, rx = ports["tx"], ports["rx"]
        config = plan["receiver"]
        init_path = config.get("init_path", "phy")
        initialized = rx.request("metal init " + init_path, lambda r: r.get("type") == "init", records=True)
        if (initialized.get("ok") is not True or initialized.get("requested") != init_path
            or initialized.get("actual_mode") != ("direct_PHY" if init_path == "phy" else "WiFi_NULL")
            or initialized.get("error_code") != 0):
            raise RuntimeError("Metal RX explicit PHY/driver initialization unavailable")
        result["receiver_init"] = initialized
        tune = rx.request(f"metal tune {config['frequency_mhz']} {config['offset_khz']} {config['manual_gain_index']}", lambda r: r.get("type") == "tune", records=True)
        if tune.get("ok") is not True or any(tune.get(key) != config[key] for key in ("frequency_mhz", "offset_khz", "manual_gain_index")):
            raise RuntimeError("Metal RX requested tune/gain unavailable")
        result["receiver_tune"] = tune
        if any(case["tx_enabled"] for case in plan["workloads"]):
            config = plan["transmitter"]
            for command, kind in ((f"reserve {config['symbol_capacity']} {config['trace_capacity']}", "reserved"),
                                  (f"observe {config['trace_stride']}", "observe"), ("init " + config["init_path"], "init_path")):
                tx.request(command, lambda r, kind=kind: r.get("type") == kind and r.get("ok") is True, records=True)
            configured = tx.request("info", lambda r: r.get("type") == "info", records=True)
            if any(configured.get(key) != value for key, value in (("capacity_symbols", config["symbol_capacity"]),
                ("trace_capacity", config["trace_capacity"]), ("trace_stride", config["trace_stride"]))):
                raise RuntimeError("Metal TX actual preallocation/observer configuration differs")
            result["transmitter_setup_info"] = configured
        for case in plan["workloads"]:
            for repetition in range(1, plan.get("repetitions", 1) + 1):
                run = {"run_id": f"m{len(result['runs']) + 1:03}_{repetition}", "workload": case["name"],
                       "repetition": repetition, "settings": case, "status": "IN PROGRESS"}
                result["runs"].append(run)
                cursors = {name: len(port.records) for name, port in ports.items()}
                try:
                    execute_run(run, tx, rx, plan, args.plan, args.output / run["run_id"], guard)
                except BaseException as error:
                    run.update(status="FAILED", error=guard.clean(str(error)), partial_target={
                        name: port.records[cursors[name]:] for name, port in ports.items()})
                    raise
                finally:
                    (args.output / "results.json").write_text(json.dumps(guard.clean(result), indent=2) + "\n", encoding="utf-8")
        result["status"] = "CAPTURE COMPLETE"
    except BaseException as error:
        result.update(status="INTERRUPTED" if isinstance(error, KeyboardInterrupt) else "FAILED", error=guard.clean(str(error)))
        raise
    finally:
        for port in ports.values(): port.close()
        result["ended_utc"] = utc_now()
        (args.output / "results.json").write_text(json.dumps(guard.clean(result), indent=2) + "\n", encoding="utf-8")
        hashes = {path.relative_to(args.output).as_posix(): digest(path) for path in sorted(args.output.rglob("*")) if path.is_file()}
        (args.output / "artifact-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n", encoding="utf-8")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    child = sub.add_parser("plan"); child.add_argument("--output", type=pathlib.Path, required=True)
    child.add_argument("--with-tx", action="store_true")
    child = sub.add_parser("capture")
    child.add_argument("--identities", type=pathlib.Path, required=True); child.add_argument("--plan", type=pathlib.Path, required=True)
    child.add_argument("--output", type=pathlib.Path, required=True); child.add_argument("--rf-authorized", action="store_true")
    args = parser.parse_args()
    if args.command == "plan":
        plan = default_plan(args.with_tx); validate_plan(plan)
        args.output.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8"); return 0
    return capture(args)


if __name__ == "__main__":
    raise SystemExit(main())
