#!/usr/bin/env python3
"""Capture portable kernels from an explicitly selected board; never flash it.

Standalone S3/ESP32/C3 images and classic CYD profiling images are distinguished
in metadata. No network credentials are accepted by this tool.
"""
from __future__ import annotations

import argparse
import csv
import json
import pathlib
import platform
import re
import sys
from typing import Any

from capture import Capture, digest, percentile, save_result, utc_now

KERNELS = ("osc-f32", "osc-q15", "filter-f32", "filter-q15", "modal-f32", "modal-q15",
           "fft-f32", "cellular", "particles", "feedback", "stripe-rgb565")
OUTPUT_BYTES = {"osc-f32": (256, 1024), "osc-q15": (256, 1024),
                "filter-f32": (256, 1024), "filter-q15": (256, 1024),
                "modal-f32": (256, 1024), "modal-q15": (256, 1024),
                "fft-f32": (2048, 8192), "cellular": (3072, 12288),
                "particles": (2048, 8192), "feedback": (4800, 19200),
                "stripe-rgb565": (2560, 10240)}


def validate_identity(info: dict[str, Any], args: argparse.Namespace, board: dict[str, Any]) -> None:
    for field, expected in (("source_sha", args.source_sha.lower()), ("profile", args.profile),
                            ("source_digest", args.source_digest.lower()), ("serial_baud", args.baud)):
        if info.get(field) != expected:
            raise ValueError(f"Target {field} does not match requested identity")
    if board.get("port") and board["port"].casefold() != args.port.casefold():
        raise ValueError("Board metadata port differs from --port")
    for field in ("flash_bytes", "psram_bytes"):
        if not isinstance(board.get(field), int) or info.get(field) != board[field]:
            raise ValueError(f"Target {field} differs from positively identified board metadata")
    chip = info.get("chip", "")
    if not chip or chip.casefold() not in str(board.get("chip_model", "")).casefold():
        raise ValueError("Target chip does not match board chip_model")
    for field in ("cpu_mhz", "flash_hz"):
        if board.get(field) is not None and info.get(field) != board[field]:
            raise ValueError(f"Target {field} differs from board metadata")
    if args.legacy_internal and info.get("psram_bytes") != 0:
        raise ValueError("Legacy inferred-region capture is allowed only on a board with zero PSRAM")
    if info.get("native_usb") is True and info.get("usb_tx_buffer_configured") is not True:
        raise ValueError("Target failed to allocate its declared native USB telemetry buffer")


def validate_kernel(record: dict[str, Any], kernel: str, large: int, seed: int,
                    region: str, legacy_internal: bool = False) -> None:
    samples = record.get("samples_us")
    if (record.get("type") != "kernel" or record.get("kernel") != kernel or
        record.get("seed") != seed or record.get("large") != large or record.get("warmup") != 3 or
        record.get("samples") != 256 or not isinstance(samples, list) or len(samples) != 256 or
        any(type(value) is not int or value < 0 for value in samples) or record.get("finite") is not True):
        raise ValueError("Kernel metadata/raw sample validation failed")
    expected = {"sum_us": sum(samples), "min_us": min(samples), "max_us": max(samples),
                "median_us": percentile(samples, 50), "p95_us": percentile(samples, 95),
                "p99_us": percentile(samples, 99), "output_bytes": OUTPUT_BYTES[kernel][large]}
    if any(record.get(key) != value for key, value in expected.items()):
        raise ValueError("Kernel timing summary or output dimensions disagree with raw samples/workload")
    if not isinstance(record.get("checksum_fnv1a"), int) or not 0 <= record["checksum_fnv1a"] <= 0xFFFFFFFF:
        raise ValueError("Missing/invalid output fingerprint")
    if not legacy_internal:
        if record.get("memory_requested") != region or record.get("memory_region") != region:
            raise ValueError("Target did not verify the requested kernel memory region")
        flag = "MALLOC_CAP_INTERNAL" if region == "internal" else "MALLOC_CAP_SPIRAM"
        if flag not in record.get("memory_caps", "") or "MALLOC_CAP_8BIT" not in record["memory_caps"]:
            raise ValueError("Target memory allocation capability metadata missing")
    elif region != "internal":
        raise ValueError("Legacy allocation cannot establish a PSRAM run")


def write_extra_csv(result: dict[str, Any], output: pathlib.Path) -> None:
    rows, sensors = [], []
    for run in result["runs"]:
        for record in run.get("target", []):
            common = {"source_sha": result["source_sha"], "firmware_sha256": result["firmware_sha256"],
                      "profile": result["profile"], "run_id": run["run_id"], "workload": run["workload"],
                      "repetition": run["repetition"], "memory_region": run["settings"]["memory_region"]}
            if record.get("type") == "kernel":
                for index, timing in enumerate(record["samples_us"]):
                    rows.append({**common, "sample_index": index, "duration_us": timing})
            elif record.get("type") == "state":
                sensors.append({**record, **common, "target_run_id": record.get("run_id")})
    for name, values in (("kernel-timings.csv", rows), ("kernel-sensors.csv", sensors)):
        keys = list(dict.fromkeys(key for row in values for key in row)) or ["run_id", "status"]
        with (output / name).open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=keys)
            writer.writeheader()
            writer.writerows(values)


def execute(args: argparse.Namespace) -> int:
    if not re.fullmatch(r"[0-9a-fA-F]{40}", args.source_sha) or not re.fullmatch(r"[0-9a-fA-F]{64}", args.source_digest):
        raise ValueError("Exact 40-digit source SHA and 64-digit source digest are required")
    board = json.loads(args.board.read_text(encoding="utf-8-sig"))
    if not isinstance(board, dict) or not board.get("board"):
        raise ValueError("Board metadata requires owner-identified board name")
    result = {"schema": "cyd-perf-capture/1", "capture_kind": "portable_kernel_matrix",
              "evidence_class": "physical_measured", "started_utc": utc_now(), "status": "IN PROGRESS",
              "source_sha": args.source_sha.lower(), "source_digest": args.source_digest.lower(),
              "source_tree_state": "sha256:" + args.source_digest.lower(), "port": args.port,
              "serial_baud": args.baud, "profile": args.profile, "board": board,
              "firmware_file": str(args.firmware.resolve()), "firmware_sha256": digest(args.firmware),
              "host": {"platform": platform.platform(), "python": sys.version},
              "plan": {"schema": "cyd-perf-plan/1", "seed": args.seed, "repetitions": 3,
                       "kernel_samples": 256, "memory_request": args.memory, "workloads": []},
              "runs": [], "limitations": [
                  "Supplied binary hash is recorded; target source identity does not independently attest the flashed binary digest.",
                  "Standalone and Diviner profiling images have different concurrent audio/display/task contexts; record that distinction in cross-board comparisons.",
                  "Temperature is a driver/raw sensor indication, with external accuracy not verified.",
                  "Kernel step timings include RTOS preemption; allocation, preparation, warmup, yields and output inspection are excluded.",
                  "FFT legacy checksum field is XOR of FNV-1a(real) and FNV-1a(imag); cross-chip float bit equality is not required."]}
    capture = Capture(args.port, args.baud, args.output)
    cursor = 0
    try:
        capture.wait(.3)
        cursor = len(capture.records)
        capture.command("perf info")
        information = capture.until("info", after=cursor, timeout=10)
        info = next(record for record in information if record["type"] == "info")
        validate_identity(info, args, board)
        result["target_info"] = information
        result["target_source_digest_verified"] = True
        result["target_firmware_digest_verified"] = False
        if info.get("link_peer_compiled"):
            cursor = len(capture.records)
            capture.command("perf link mode off 0")
            acknowledgements = capture.until("link_ack", after=cursor, timeout=15)
            acknowledgement = next(record for record in acknowledgements if record["type"] == "link_ack")
            if acknowledgement.get("ok") is not True or acknowledgement.get("action") != "mode":
                raise ValueError("Peer radio-off request failed")
            cursor = len(capture.records)
            capture.command("perf link info")
            result["link_baseline"] = capture.until("link_info", after=cursor, timeout=10)
            link = next(record for record in result["link_baseline"] if record["type"] == "link_info")
            if link.get("mode") != "off" or link.get("active") is not False:
                raise ValueError("Link peer did not report an inactive radio-off kernel baseline")
        cursor = len(capture.records)
        capture.command("perf kernel list")
        inventory_records = capture.until("kernel_list", after=cursor)
        inventory = next(record for record in inventory_records if record["type"] == "kernel_list")
        if set(inventory.get("kernels", [])) != set(KERNELS):
            raise ValueError("Target kernel inventory does not match the portable suite")
        result["target_kernel_inventory"] = inventory_records
        has_psram = bool(info.get("psram_bytes", 0))
        regions = ["internal"] if args.memory == "internal" else ["psram"] if args.memory == "psram" else ["internal", "psram"]
        if "psram" in regions and not has_psram:
            if args.memory != "auto":
                raise ValueError("Explicit PSRAM matrix requested but PSRAM is not initialized")
            regions.remove("psram")
            result["runs"].append({"workload": "kernel/psram", "status": "NOT RUN", "reason": "Target reports zero initialized PSRAM bytes"})
            result["plan"]["workloads"].append({"name": "kernel/psram", "status": "NOT RUN", "reason": "Target reports zero initialized PSRAM bytes"})
        if args.legacy_internal:
            result["limitations"].append("Legacy image lacks explicit memory-region/capability receipts; internal placement is inferred only because the identified board has zero PSRAM.")
        # Register the entire requested matrix before the first kernel command:
        # an interrupted run must not shrink the apparent planned denominator.
        result["plan"]["workloads"].extend({
            "name": f"kernel/{kernel}/{'large' if large else 'realistic'}/{region}",
            "kind": "kernel", "kernel": kernel, "large": large,
            "memory_region": region, "repetitions": 3}
            for region in regions for kernel in KERNELS for large in (0, 1))
        save_result(result, args.output)
        reference_checksums = {}
        for region in regions:
            for kernel in KERNELS:
                for large in (0, 1):
                    name = f"kernel/{kernel}/{'large' if large else 'realistic'}/{region}"
                    for repetition in range(1, 4):
                        run_id = f"k{len(result['runs']) + 1:03}"
                        run = {"run_id": run_id, "workload": name, "repetition": repetition, "status": "IN PROGRESS",
                               "settings": {"kind": "kernel", "kernel": kernel, "large": large, "memory_region": region,
                                            "memory_region_method": "inferred_zero_psram" if args.legacy_internal else "target_verified_capability_allocation"}}
                        result["runs"].append(run)
                        print(f"{run_id} {name} repetition {repetition}/3", flush=True)
                        cursor = len(capture.records)
                        capture.command("perf sample")
                        capture.until("state", after=cursor)
                        memory_suffix = "" if args.legacy_internal else " " + region
                        command = f"perf kernel {kernel} {args.seed} 256 {large}{memory_suffix}"
                        run["host_command_correlation"] = command
                        capture.command(command)
                        records = capture.until("kernel", after=cursor, timeout=180)
                        record = next(item for item in records if item["type"] == "kernel")
                        validate_kernel(record, kernel, large, args.seed, region, args.legacy_internal)
                        key = (kernel, large)
                        fingerprint = record["checksum_fnv1a"]
                        if key in reference_checksums and reference_checksums[key] != fingerprint:
                            raise ValueError("Output checksum changed across same-target repetitions/memory regions: " + kernel)
                        reference_checksums[key] = fingerprint
                        after = len(capture.records)
                        capture.command("perf sample")
                        capture.until("state", after=after)
                        run["target"] = capture.records[cursor:]
                        run["status"] = "MEASURED"
                        save_result(result, args.output)
        result["status"] = "CAPTURE COMPLETE"
        result["ended_utc"] = utc_now()
    except BaseException as error:
        result["status"] = "INTERRUPTED" if isinstance(error, KeyboardInterrupt) else "FAILED"
        result["error"] = repr(error)
        if result["runs"] and result["runs"][-1]["status"] == "IN PROGRESS":
            result["runs"][-1]["target_partial"] = capture.records[cursor:]
            result["runs"][-1]["status"] = result["status"]
        raise
    finally:
        capture.close()
        result["parse_errors"] = capture.errors
        save_result(result, args.output)
        write_extra_csv(result, args.output)
        files = {path.name: digest(path) for path in args.output.iterdir() if path.is_file() and path.name != "artifact-hashes.json"}
        (args.output / "artifact-hashes.json").write_text(json.dumps(files, indent=2) + "\n", encoding="utf-8")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--board", type=pathlib.Path, required=True)
    parser.add_argument("--firmware", type=pathlib.Path, required=True)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--source-digest", required=True)
    parser.add_argument("--seed", type=int, default=23063)
    parser.add_argument("--memory", choices=("auto", "internal", "psram", "both"), default="auto")
    parser.add_argument("--legacy-internal", action="store_true", help="Explicit inference-only compatibility for an older CYD image on zero-PSRAM hardware")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    if not 0 <= args.seed <= 0xFFFFFFFF:
        parser.error("seed must fit uint32")
    return execute(args)


if __name__ == "__main__":
    raise SystemExit(main())
