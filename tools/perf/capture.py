#!/usr/bin/env python3
"""Quiet-window CYD capture. Never flashes, erases, or changes credentials.

Target timings and counters remain target observations. Host receive times are
transport evidence, not target scheduling or physical touch latency.
"""
from __future__ import annotations

import argparse
import binascii
import csv
import datetime as dt
import hashlib
import json
import math
import pathlib
import platform
import re
import struct
import sys
import time
from typing import Any

SCHEMA = "cyd-perf-capture/1"
MAGIC = b"CYD1"
FRAME_BYTES = 266
SAMPLES_PER_FRAME = 128
RATE = 24000
METRIC_NAMES = {"frame_interval_us", "frame_update_us", "frame_raster_us", "frame_present_us",
                "touch_poll_us", "touch_dispatch_us", "audio_period_us", "audio_lock_us",
                "audio_callback_us", "audio_overlay_us", "audio_mix_us", "audio_enqueue_us",
                "pcm_write_us", "audio_write_errors", "audio_short_writes", "audio_late_periods_inferred",
                "pcm_dropped_blocks", "pcm_queue_blocks", "frame_missed_periods_inferred"}


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def percentile(values: list[float], percent: float) -> float | None:
    """Nearest-rank percentile of actual samples; no missing-value synthesis."""
    if not values:
        return None
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * percent / 100) - 1)]


def distribution(values: list[float]) -> dict[str, Any]:
    return {"samples": len(values), "median": percentile(values, 50),
            "p95": percentile(values, 95), "p99": percentile(values, 99),
            "max": max(values) if values else None,
            "percentile_method": "nearest_rank_actual_samples",
            "p99_support": "at_least_100_samples" if len(values) >= 100 else "fewer_than_100_samples"}


class PCMDecoder:
    """Incremental resynchronising decoder for cyd_pcm_protocol.h v1."""
    def __init__(self) -> None:
        self.buffer = bytearray()
        self.frames = self.corrupt = self.missing = self.duplicates = 0
        self.discontinuities = self.discarded_bytes = 0
        self.last_sequence: int | None = None
        self.arrival_intervals_us: list[float] = []
        self.last_arrival: float | None = None
        self.first_arrival: float | None = None
        self.nonzero_samples = 0

    def feed(self, data: bytes, arrived: float | None = None) -> None:
        self.buffer.extend(data)
        while len(self.buffer) >= 4:
            position = self.buffer.find(MAGIC)
            if position < 0:
                count = max(0, len(self.buffer) - 3)
                self.discarded_bytes += count
                del self.buffer[:count]
                break
            if position:
                self.discarded_bytes += position
                del self.buffer[:position]
            if len(self.buffer) < FRAME_BYTES:
                break
            frame = self.buffer[:FRAME_BYTES]
            if binascii.crc_hqx(frame[4:264], 0xFFFF) != struct.unpack_from("<H", frame, 264)[0]:
                self.corrupt += 1
                self.discarded_bytes += 1
                del self.buffer[:1]
                continue
            del self.buffer[:FRAME_BYTES]
            sequence = struct.unpack_from("<I", frame, 4)[0]
            if self.last_sequence is not None:
                advance = (sequence - self.last_sequence) & 0xFFFFFFFF
                if advance == 0:
                    self.duplicates += 1
                    continue
                if advance < 0x80000000:
                    self.missing += advance - 1
                else:
                    self.discontinuities += 1
            self.last_sequence = sequence
            self.frames += 1
            self.nonzero_samples += sum(x != 0 for x in struct.unpack_from("<128h", frame, 8))
            # Multiple frames may arrive in one OS read: these zero intervals
            # describe host buffering, never the target's audio block cadence.
            if arrived is not None:
                if self.last_arrival is not None:
                    self.arrival_intervals_us.append((arrived - self.last_arrival) * 1e6)
                if self.first_arrival is None:
                    self.first_arrival = arrived
                self.last_arrival = arrived

    def summary(self) -> dict[str, Any]:
        seconds = (self.last_arrival - self.first_arrival
                   if self.first_arrival is not None and self.last_arrival is not None else None)
        return {"protocol": "CYD1", "rate_hz": RATE, "samples_per_frame": SAMPLES_PER_FRAME,
                "valid_frames": self.frames, "crc_errors": self.corrupt,
                "missing_sequence_frames": self.missing, "duplicate_frames": self.duplicates,
                "backward_sequence_discontinuities": self.discontinuities,
                "discarded_bytes": self.discarded_bytes, "trailing_bytes": len(self.buffer),
                "nonzero_samples": self.nonzero_samples,
                "decoded_audio_seconds": self.frames * SAMPLES_PER_FRAME / RATE,
                "host_first_to_last_frame_seconds": seconds,
                "host_arrival_interval_us": distribution(self.arrival_intervals_us),
                "limitations": ["Sequence gaps include producer queue drops and transport loss; classify with target counters.",
                                "Host arrival intervals include UART/driver buffering and are not DMA underruns.",
                                "Loss before first received sequence and after last received frame is unknown."]}


def parse_perf(line: bytes) -> dict[str, Any] | None:
    """Permit firmware's PERF prefix; keep malformed JSON visible as evidence."""
    stripped = line.strip()
    if not stripped.startswith(b"PERF "):
        return None
    def reject_constant(value: str) -> None:
        raise ValueError("Nonfinite JSON constant: " + value)
    item = json.loads(stripped[5:].decode("utf-8"), parse_constant=reject_constant)
    if not isinstance(item, dict) or item.get("schema") != 1 or not isinstance(item.get("type"), str):
        raise ValueError("Invalid PERF schema/type: " + repr(stripped[:160]))
    return item


class Capture:
    def __init__(self, port: str, baud: int, output: pathlib.Path) -> None:
        import serial  # Optional for offline tests/analysis.
        self.output = output
        output.mkdir(parents=True, exist_ok=False)
        self.raw = (output / "serial-rx.bin").open("wb")
        self.events = (output / "serial-events.jsonl").open("w", encoding="utf-8", buffering=1)
        self.text = (output / "serial-text.log").open("wb")
        self.perf = (output / "target.jsonl").open("w", encoding="utf-8", buffering=1)
        self.offset = 0
        self.started = time.perf_counter()
        self.pending = bytearray()
        self.records: list[dict[str, Any]] = []
        self.errors: list[str] = []
        self.serial = serial.Serial(port=None, baudrate=baud, timeout=0.05, write_timeout=2)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()
        self.log("open", port=port, baud=baud, dtr=False, rts=False)

    def log(self, event: str, **details: Any) -> None:
        self.events.write(json.dumps({"event": event, "host_elapsed_s": time.perf_counter() - self.started,
                                     "utc": utc_now(), **details}) + "\n")

    def command(self, command: str) -> None:
        if "\n" in command or "\r" in command or len(command) > 125:
            raise ValueError("Command must be one bounded line")
        self.log("tx", command=command)
        data = (command + "\n").encode("ascii")
        if self.serial.write(data) != len(data):
            raise OSError("Short serial write")

    def read(self, binary: bool = False, decoder: PCMDecoder | None = None,
             sink: Any = None) -> bytes:
        data = self.serial.read(max(1, min(8192, self.serial.in_waiting)))
        if not data:
            return data
        arrived = time.perf_counter()
        self.raw.write(data)
        self.log("rx", offset=self.offset, bytes=len(data), mode="binary" if binary else "text")
        self.offset += len(data)
        if sink is not None:
            sink.write(data)
        if decoder is not None:
            decoder.feed(data, arrived)
        if not binary:
            self.text.write(data)
            self.pending.extend(data)
            if len(self.pending) > 1024 * 1024:
                raise ValueError("Unterminated serial text exceeded 1 MiB")
            while b"\n" in self.pending:
                line, _, remainder = self.pending.partition(b"\n")
                self.pending = bytearray(remainder)
                try:
                    record = parse_perf(line)
                    if record is not None:
                        record["host_received_elapsed_s"] = arrived - self.started
                        self.records.append(record)
                        self.perf.write(json.dumps(record) + "\n")
                except (ValueError, UnicodeDecodeError) as error:
                    self.errors.append(str(error))
                    self.log("parse_error", error=str(error))
        return data

    def wait(self, seconds: float, binary: bool = False, decoder: PCMDecoder | None = None,
             ping: bool = False, actions: list[dict[str, Any]] | None = None, sink: Any = None) -> None:
        start = time.perf_counter()
        last_ping = start
        actions = iter(sorted(actions or [], key=lambda item: item["at_ms"]))
        action = next(actions, None)
        while time.perf_counter() - start < seconds:
            now = time.perf_counter()
            if ping and now - last_ping >= 0.9:
                self.command("pcm ping")
                last_ping = now
            while action is not None and now - start >= action["at_ms"] / 1000:
                self.command(action["command"])
                action = next(actions, None)
            self.read(binary=binary, decoder=decoder, sink=sink)

    def until(self, kind: str, timeout: float = 20, run_id: str | None = None,
              after: int = 0) -> list[dict[str, Any]]:
        start = time.perf_counter()
        while time.perf_counter() - start < timeout:
            if self.errors:
                raise RuntimeError("Malformed target telemetry; raw bytes retained: " + self.errors[-1])
            for record in self.records[after:]:
                if record["type"] in ("error", "kernel_error"):
                    raise RuntimeError("Target error: " + json.dumps(record))
                if record["type"] == kind and (run_id is None or record.get("run_id") == run_id):
                    return self.records[after:]
            self.read()
        raise TimeoutError(f"No target {kind!r} record for {run_id!r} after {timeout}s")

    def acknowledged(self, command: str) -> list[dict[str, Any]]:
        cursor = len(self.records)
        self.command(command)
        allowed_actions = {command[5:], command[5:].split(" ", 1)[0]}
        deadline = time.perf_counter() + 10
        while time.perf_counter() < deadline:
            records = self.until("ack", after=cursor, timeout=max(0.01, deadline - time.perf_counter()))
            for acknowledgement in records:
                if acknowledgement["type"] == "ack" and acknowledgement.get("action") in allowed_actions:
                    if acknowledgement.get("ok") is not True:
                        raise RuntimeError("Setup command rejected: " + command)
                    return records
            cursor = len(self.records)
        raise TimeoutError("Missing matching acknowledgement: " + command)

    def drain_pcm(self, decoder: PCMDecoder, sink: Any) -> None:
        """Keep all stop-tail bytes until at least 400 ms and 200 ms quiet."""
        started = last_data = time.perf_counter()
        while time.perf_counter() - started < 3:
            data = self.read(binary=True, decoder=decoder, sink=sink)
            now = time.perf_counter()
            if data:
                last_data = now
            if now - started >= 0.4 and now - last_data >= 0.2:
                return
        raise TimeoutError("PCM stop did not reach a quiet serial boundary within 3 s")

    def close(self) -> None:
        self.serial.close()
        for stream in (self.raw, self.events, self.text, self.perf):
            stream.close()


def validate_plan(plan: dict[str, Any]) -> None:
    if plan.get("schema") != "cyd-perf-plan/1" or not isinstance(plan.get("workloads"), list):
        raise ValueError("Expected cyd-perf-plan/1 workloads array")
    for case in plan["workloads"]:
        if not isinstance(case.get("name"), str) or not case["name"]:
            raise ValueError("Every workload needs a name")
        if case.get("status") == "NOT RUN":
            continue
        if case.get("kind") == "kernel":
            if not isinstance(case.get("kernel"), str) or case.get("kernel_samples") != 256:
                raise ValueError("Kernel workload requires kernel name and exactly 256 timing samples")
        if case.get("kind") == "spi" and not 3 <= case.get("samples", 0) <= 128:
            raise ValueError("SPI workload samples must be 3..128")
        if case.get("stress_seconds", 0) and case["stress_seconds"] < 360:
            raise ValueError("Extended CPU stress must last at least 360 seconds")
        if not 1 <= case.get("repetitions", plan.get("repetitions", 3)) <= 100:
            raise ValueError("Repetitions must be 1..100")
        if not 0 <= case.get("warmup_ms", plan.get("warmup_ms", 2000)) <= 60000:
            raise ValueError("Warmup must be 0..60000 ms")
        if not 1 <= case.get("duration_ms", plan.get("duration_ms", 10000)) <= 600000:
            raise ValueError("Duration must be 1..600000 ms")
        for command in case.get("commands", []):
            if not isinstance(command, str) or not command.startswith("perf "):
                raise ValueError("Plan setup commands must be explicit perf commands")
        for action in case.get("actions", []):
            if not isinstance(action.get("at_ms"), (float, int)) or action["at_ms"] < 0:
                raise ValueError("Replay at_ms must be nonnegative")
            if not re.fullmatch(r"(?:tap|touch) \d{1,3} \d{1,3}|release|perf key \d{1,3}", action.get("command", "")):
                raise ValueError("Replay permits only touch, release, or profiling key commands")


def flatten_results(result: dict[str, Any], output: pathlib.Path) -> None:
    metric_rows: list[dict[str, Any]] = []
    sample_rows: list[dict[str, Any]] = []
    for run in result.get("runs", []):
        base = {"source_sha": result.get("source_sha"), "firmware_sha256": result.get("firmware_sha256"),
                "profile": result.get("profile"), "run_id": run.get("run_id"), "workload": run["workload"],
                "repetition": run.get("repetition"), "status": run["status"],
                "settings": run.get("settings"), "seed": result.get("plan", {}).get("seed")}
        for record in run.get("target", []):
            if record.get("type") == "metric":
                metric_rows.append({**base, **record})
            elif record.get("type") == "sample":
                sample_rows.append({**base, **record})
            elif record.get("type") == "state":
                sample_rows.append({**base, **record, "sample_context": "outside_kernel_or_SPI_timing_window"})
            elif record.get("type") in ("kernel", "spi"):
                metric_rows.append({**base, **record})
        if run["status"] == "NOT RUN":
            metric_rows.append({**base, "reason": run.get("reason")})
        if run.get("frames_per_second") is not None:
            metric_rows.append({**base, "name": "completed_frame_rate", "unit": "frames/s",
                                "value": run["frames_per_second"], "method": "true_completed_presents_divided_by_target_elapsed_time"})
    for filename, rows in (("metrics.csv", metric_rows), ("samples.csv", sample_rows)):
        keys = list(dict.fromkeys(key for row in rows for key in row))
        with (output / filename).open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=keys or ["run_id", "status"])
            writer.writeheader()
            for row in rows:
                writer.writerow({key: json.dumps(value, separators=(",", ":")) if isinstance(value, (dict, list)) else value
                                 for key, value in row.items()})


def save_result(result: dict[str, Any], output: pathlib.Path) -> None:
    temporary = output / "results.tmp"
    temporary.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temporary.replace(output / "results.json")
    flatten_results(result, output)


def stress_assessment(records: list[dict[str, Any]], duration_s: float,
                      info: dict[str, Any] | None = None) -> dict[str, Any]:
    samples = [record for record in records if record.get("type") == "sample"]
    core_values: dict[str, list[float]] = {"core0": [], "core1": []}
    for sample in samples:
        for core in range(2):
            # Concrete key names pinned by firmware; null means unsupported.
            value = sample.get(f"core{core}_busy_pct")
            if isinstance(value, (int, float)):
                core_values[f"core{core}"].append(float(value))
    distributions = {key: distribution(values) for key, values in core_values.items()}
    weighted = {}
    for core in range(2):
        busy = sum(sample.get(f"cpu{core}_busy_ticks", 0) for sample in samples)
        total = sum(sample.get(f"cpu{core}_total_ticks", 0) for sample in samples)
        supported = info is None or info.get(f"cpu{core}_hook") is True
        weighted[f"core{core}"] = 100 * busy / total if total and supported else None
    ends = [record for record in records if record.get("type") == "end"]
    end = ends[-1] if ends else {}
    elapsed = end.get("elapsed_us", 0) / 1e6
    times = [sample["elapsed_ms"] for sample in samples if isinstance(sample.get("elapsed_ms"), (int, float))]
    coverage = (max(times) - min(times)) / 1000 if len(times) >= 2 else 0
    return {"requested_duration_s": duration_s,
            "minimum_duration_met_by_request": duration_s >= 360,
            "observed_busy_pct": distributions,
            "weighted_busy_pct": weighted, "target_elapsed_s": elapsed,
            "observed_sample_span_s": coverage,
            "near_100_percent_confirmed": end.get("complete") is True and elapsed >= 360
                and coverage >= 360 and all(value is not None and value >= 95 for value in weighted.values()),
            "limitation": "CPU tick sampling is an estimate; target complete/elapsed and >=360 s retained sample span accompany the >=360 s measured window."}


def validate_end(records: list[dict[str, Any]], run_id: str, duration_ms: int) -> dict[str, Any]:
    ends = [record for record in records if record.get("type") == "end" and record.get("run_id") == run_id]
    if len(ends) != 1 or ends[0].get("complete") is not True:
        raise RuntimeError("Target run did not complete: " + run_id)
    end = ends[0]
    if end.get("elapsed_us", 0) < duration_ms * 1000:
        raise RuntimeError("Target measured window shorter than requested: " + run_id)
    if end.get("samples_dropped", 0):
        raise RuntimeError("Target sample retention overflowed: " + run_id)
    metrics = [record for record in records if record.get("type") == "metric" and record.get("run_id") == run_id]
    names = [record.get("name") for record in metrics]
    if set(names) != METRIC_NAMES or len(names) != len(METRIC_NAMES):
        raise RuntimeError("Missing/duplicate target metrics: " + run_id)
    for metric in metrics:
        if not isinstance(metric.get("histogram"), list) or len(metric["histogram"]) != 32:
            raise RuntimeError("Invalid histogram retention: " + str(metric.get("name")))
        if sum(metric["histogram"]) != metric.get("count"):
            raise RuntimeError("Histogram count mismatch: " + str(metric.get("name")))
    samples = [record for record in records if record.get("type") == "sample" and record.get("run_id") == run_id]
    if len(samples) != end.get("samples"):
        raise RuntimeError("Missing retained samples: " + run_id)
    return end


def interpreted_records(records: list[dict[str, Any]], info: dict[str, Any] | None = None) -> list[dict[str, Any]]:
    result = []
    for original in records:
        record = dict(original)
        if record.get("type") == "metric":
            count = record.get("count", 0)
            record["percentile_method"] = "ceil_log2_histogram_upper_bound"
            record["p99_support"] = "at_least_100_samples" if count >= 100 else "INSUFFICIENT_SAMPLES"
            if count == 0:
                for field in ("min", "max", "p50_upper", "p95_upper", "p99_upper"):
                    record[field] = None
                record["status"] = "NO OBSERVATIONS"
            elif count < 100:
                record["p99_upper"] = None
        if record.get("type") == "sample":
            for core in range(2):
                if record.get(f"cpu{core}_total_ticks") == 0 or (info is not None and info.get(f"cpu{core}_hook") is not True):
                    record[f"core{core}_busy_pct"] = None
        result.append(record)
    return result


def capture_command(args: argparse.Namespace) -> int:
    plan = json.loads(args.plan.read_text(encoding="utf-8-sig"))
    validate_plan(plan)
    if not re.fullmatch(r"[0-9a-fA-F]{40}", args.source_sha):
        raise ValueError("--source-sha must be the exact 40-digit source commit")
    board = json.loads(args.board.read_text(encoding="utf-8-sig"))
    if not isinstance(board, dict) or not board.get("board"):
        raise ValueError("--board JSON requires the positively identified board name")
    result = {"schema": SCHEMA, "evidence_class": "physical_measured", "started_utc": utc_now(),
              "status": "IN PROGRESS", "port": args.port, "serial_baud": args.baud,
              "source_sha": args.source_sha.lower(), "source_tree_state": args.tree_state,
              "firmware_sha256": digest(args.firmware), "firmware_file": str(args.firmware.resolve()),
              "profile": args.profile, "board": board, "plan": plan,
              "plan_sha256": digest(args.plan), "host": {"platform": platform.platform(),
              "python": sys.version}, "runs": [], "limitations": [
                  "Temperature is a sensor indication with the target-reported calibration/units; not an external thermocouple.",
                  "Scripted touch processing does not measure contact-to-photon physical latency.",
                  "Direct rendering interleaves CPU drawing and SPI transfer; do not split this time speculatively.",
                  "Histogram percentiles are upper bounds at recorded bin precision, not exact samples.",
                  "No DMA underrun claim from i2s_write timing or block counts alone."]}
    capture = Capture(args.port, args.baud, args.output)
    try:
        capture.wait(0.3)
        cursor = len(capture.records)
        capture.command("perf info")
        result["target_info"] = capture.until("info", after=cursor)
        info = next(record for record in result["target_info"] if record["type"] == "info")
        if info.get("source_sha") != result["source_sha"] or info.get("profile") != args.profile:
            raise RuntimeError("Target firmware identity does not match --source-sha/--profile")
        if info.get("serial_baud") != args.baud:
            raise RuntimeError("Target serial profile does not match --baud")
        tree_digest = args.tree_state.removeprefix("sha256:")
        if re.fullmatch(r"[0-9a-fA-F]{64}", tree_digest):
            if info.get("source_digest") != tree_digest.lower():
                raise RuntimeError("Target source tree digest does not match --tree-state")
            result["target_source_digest_verified"] = True
        else:
            result["target_source_digest_verified"] = False
        result["target_firmware_digest_verified"] = False
        result["limitations"].append("Firmware SHA256 is calculated from the supplied binary; target reports source identity but does not independently attest the flashed binary digest.")
        save_result(result, args.output)
        if any(case.get("kind") == "kernel" for case in plan["workloads"]):
            cursor = len(capture.records)
            capture.command("perf kernel list")
            result["target_kernel_inventory"] = capture.until("kernel_list", after=cursor)
        index = 0
        for case in plan["workloads"]:
            if case.get("status") == "NOT RUN":
                result["runs"].append({"workload": case["name"], "status": "NOT RUN", "reason": case["reason"]})
                continue
            for repetition in range(1, case.get("repetitions", plan.get("repetitions", 3)) + 1):
                index += 1
                run_id = f"r{index:03}_{repetition}"
                duration = case.get("duration_ms", plan.get("duration_ms", 10000))
                warmup = case.get("warmup_ms", plan.get("warmup_ms", 2000))
                stress = case.get("stress_seconds", 0)
                if stress:
                    duration = int(stress * 1000)
                run = {"run_id": run_id, "workload": case["name"], "repetition": repetition,
                       "status": "IN PROGRESS", "settings": case,
                       "duration_ms_requested": duration, "warmup_ms_requested": warmup, "setup_target": []}
                result["runs"].append(run)
                detail = case.get("kind", f"{(duration + warmup) / 1000:g}s")
                print(f"{run_id} {case['name']} ({detail})", flush=True)
                for command in case.get("commands", []):
                    run["setup_target"].extend(capture.acknowledged(command))
                capture.wait(case.get("settle_ms", 250) / 1000)
                cursor = len(capture.records)
                if case.get("kind") in ("kernel", "spi"):
                    capture.command("perf sample")
                    capture.until("state", after=cursor)
                    if case["kind"] == "kernel":
                        command = f"perf kernel {case['kernel']} {plan.get('seed', 23063)} {case['kernel_samples']} {int(case.get('large', False))}"
                    else:
                        command = f"perf spi {case['samples']}"
                    run["host_command_correlation"] = command
                    capture.command(command)
                    target = capture.until(case["kind"], after=cursor, timeout=case.get("timeout_s", 180))
                    observation = next(record for record in target if record["type"] == case["kind"])
                    if case["kind"] == "kernel":
                        samples = observation.get("samples_us", [])
                        if (observation.get("kernel") != case["kernel"] or observation.get("samples") != 256
                            or observation.get("finite") is not True or len(samples) != 256
                            or observation.get("seed") != plan.get("seed", 23063)
                            or observation.get("large") != int(case.get("large", False))):
                            raise RuntimeError("Kernel output invalid/incomplete: " + command)
                        expected = {"sum_us": sum(samples), "min_us": min(samples), "max_us": max(samples),
                                    "median_us": percentile(samples, 50), "p95_us": percentile(samples, 95),
                                    "p99_us": percentile(samples, 99)}
                        if any(observation.get(key) != value for key, value in expected.items()):
                            raise RuntimeError("Kernel timing summary does not match raw samples: " + command)
                    else:
                        samples = observation.get("samples_us", [])
                        if len(samples) != case["samples"]:
                            raise RuntimeError("Missing SPI timing samples")
                        observation["duration_us"] = distribution(samples)
                        observation["goodput_bytes_per_second"] = distribution([
                            observation["bytes_per_sample"] * 1e6 / value for value in samples if value > 0])
                    sensor_cursor = len(capture.records)
                    capture.command("perf sample")
                    capture.until("state", after=sensor_cursor)
                    run["target"] = capture.records[cursor:]
                    run["status"] = "MEASURED"
                    save_result(result, args.output)
                    continue
                capture.command(f"perf run {run_id} {warmup} {duration}")
                capture.until("run", after=cursor, run_id=run_id)
                if stress:
                    capture.acknowledged(f"perf stress {duration + warmup + 1000}")
                if case.get("pcm", False):
                    capture.pending.clear()
                    run["pcm_rx_start_offset"] = capture.offset
                    capture.command("pcm start")
                    decoder = PCMDecoder()
                    with (args.output / f"{run_id}-pcm.bin").open("wb") as pcm_file:
                        capture.wait((duration + warmup) / 1000 + 0.25, binary=True, decoder=decoder,
                                     ping=True, actions=case.get("actions"), sink=pcm_file)
                        capture.command("pcm stop")
                        capture.drain_pcm(decoder, pcm_file)
                    run["pcm_rx_end_offset"] = capture.offset
                    capture.pending.clear()
                    run["pcm"] = decoder.summary()
                    run["pcm"]["receive_window_scope"] = "target warmup plus steady window plus 250 ms guard and bounded stop-tail drain"
                    if decoder.frames == 0:
                        raise RuntimeError("PCM workload received no valid CYD1 frames; raw binary retained")
                else:
                    capture.wait((duration + warmup) / 1000 + 0.25, actions=case.get("actions"))
                capture.command("perf export")
                target = capture.until("end", after=cursor, run_id=run_id,
                                       timeout=max(30, duration / 1000 * 0.2))
                end = validate_end(target, run_id, duration)
                run["target"] = interpreted_records(target, info)
                run["frames_per_second"] = end["frames"] * 1e6 / end["elapsed_us"]
                run["status"] = "MEASURED"
                if stress:
                    run["stress_assessment"] = stress_assessment(run["target"], duration / 1000, info)
                    capture.acknowledged("perf stress 0")
                save_result(result, args.output)
        result["status"] = "CAPTURE COMPLETE"
        result["ended_utc"] = utc_now()
        result["parse_errors"] = capture.errors
        if capture.errors:
            result["status"] = "CAPTURE COMPLETE WITH PARSE ERRORS"
    except BaseException as error:
        result["status"] = "INTERRUPTED" if isinstance(error, KeyboardInterrupt) else "FAILED"
        result["error"] = repr(error)
        # Preserve everything received even when export/transport fails.
        if result["runs"] and result["runs"][-1]["status"] == "IN PROGRESS":
            result["runs"][-1]["target_partial"] = capture.records[cursor:]
        try:
            capture.command("pcm stop")
            capture.command("perf stress 0")
            capture.command("perf stop")
        except Exception:
            pass
        raise
    finally:
        capture.close()
        save_result(result, args.output)
        hashes = {path.name: digest(path) for path in sorted(args.output.iterdir()) if path.is_file()}
        (args.output / "artifact-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n", encoding="utf-8")
    return 0


def identify_command(args: argparse.Namespace) -> int:
    capture = Capture(args.port, args.baud, args.output)
    try:
        capture.wait(0.3)
        capture.command("perf info")
        records = capture.until("info", timeout=5)
        print(json.dumps(records, indent=2))
    finally:
        capture.close()
    return 0


def shot_command(args: argparse.Namespace) -> int:
    """Physical RGB565 readback, intentionally outside performance windows."""
    capture = Capture(args.port, args.baud, args.output)
    try:
        capture.wait(0.2)
        capture.command("shot")
        header = bytearray()
        deadline = time.perf_counter() + 10
        width = height = 0
        pixels = bytearray()
        while time.perf_counter() < deadline:
            data = capture.read(binary=True)
            header.extend(data)
            match = re.search(rb"shot (\d+) (\d+) rgb565le\r?\n", header)
            if match:
                width, height = map(int, match.groups())
                if not 1 <= width <= 320 or not 1 <= height <= 320:
                    raise ValueError("Unexpected display dimensions")
                pixels.extend(header[match.end():])
                break
        if not width:
            raise TimeoutError("No shot header")
        deadline = time.perf_counter() + 40
        while len(pixels) < width * height * 2 and time.perf_counter() < deadline:
            pixels.extend(capture.read(binary=True))
        if len(pixels) < width * height * 2:
            raise TimeoutError("Incomplete physical readback")
        raw = bytes(pixels[:width * height * 2])
        (args.output / "display.rgb565le").write_bytes(raw)
        rgb = bytearray()
        for (word,) in struct.iter_unpack("<H", raw):
            r, g, b = (word >> 11) & 31, (word >> 5) & 63, word & 31
            rgb.extend(((r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31))
        (args.output / "display.ppm").write_bytes(f"P6\n{width} {height}\n255\n".encode() + rgb)
        (args.output / "display.json").write_text(json.dumps({"schema": "cyd-display-readback/1",
            "width": width, "height": height, "sha256": hashlib.sha256(raw).hexdigest(),
            "evidence": "physical_controller_readback_outside_measurement_window"}, indent=2) + "\n")
    finally:
        capture.close()
    return 0


def analyze_command(args: argparse.Namespace) -> int:
    result = json.loads(args.results.read_text(encoding="utf-8"))
    if result.get("schema") != SCHEMA:
        raise ValueError("Unsupported capture schema")
    flatten_results(result, args.results.parent)
    measured = [run for run in result["runs"] if run["status"] == "MEASURED"]
    print(json.dumps({"status": result["status"], "measured_runs": len(measured),
        "not_run": [run["workload"] for run in result["runs"] if run["status"] == "NOT RUN"],
        "parse_errors": result.get("parse_errors", []),
        "source_sha": result["source_sha"], "firmware_sha256": result["firmware_sha256"]}, indent=2))
    return 0


def decode_command(args: argparse.Namespace) -> int:
    decoder = PCMDecoder()
    with args.binary.open("rb") as stream:
        while data := stream.read(8192):
            decoder.feed(data)
    summary = decoder.summary()
    summary["file_sha256"] = digest(args.binary)
    print(json.dumps(summary, indent=2))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="command", required=True)
    for name in ("capture", "identify", "shot"):
        child = subs.add_parser(name)
        child.add_argument("--port", required=True)
        child.add_argument("--baud", type=int, default=115200)
        child.add_argument("--output", type=pathlib.Path, required=True, help="New output directory")
        child.set_defaults(function={"capture": capture_command, "identify": identify_command, "shot": shot_command}[name])
        if name == "capture":
            child.add_argument("--plan", type=pathlib.Path, required=True)
            child.add_argument("--board", type=pathlib.Path, required=True)
            child.add_argument("--source-sha", required=True)
            child.add_argument("--tree-state", required=True, help="clean or exact patch digest/description")
            child.add_argument("--firmware", type=pathlib.Path, required=True)
            child.add_argument("--profile", required=True)
    child = subs.add_parser("analyze")
    child.add_argument("results", type=pathlib.Path)
    child.set_defaults(function=analyze_command)
    child = subs.add_parser("decode-pcm")
    child.add_argument("binary", type=pathlib.Path)
    child.set_defaults(function=decode_command)
    args = parser.parse_args()
    try:
        return args.function(args)
    except KeyboardInterrupt:
        print("Capture interrupted; partial logs retained.", file=sys.stderr)
        return 130
    except Exception as error:
        print(f"{type(error).__name__}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
