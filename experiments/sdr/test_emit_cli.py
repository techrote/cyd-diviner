#!/usr/bin/env python3
"""Validate emitter bounds/metadata/byte independence; no RF or serial access."""
import argparse
import hashlib
import json
import math
import pathlib
import subprocess


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def linux(path):
    value = path.resolve().as_posix()
    if len(value) < 3 or value[1:3] != ":/": raise ValueError("WSL requires a local Windows drive path")
    return "/mnt/" + value[0].lower() + value[2:]


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    binary = linux(args.binary) if args.wsl else str(args.binary.resolve())
    fixed = {"source_rate_hz": 24000, "channels": 2, "oversample": 16, "payload_bits": 256,
             "frame_count": 8, "seed": 23063, "stream_id": 1}
    result = {"schema": "greenfield_sdr_emit_cli_validation/1", "evidence_class": "HOST_CORRECTNESS_ONLY",
        "no_RF_measurement": True, "binary_sha256": sha(args.binary), "driver_sha256": sha(pathlib.Path(__file__)),
        "configuration": fixed, "valid_baud_cases": [], "invalid_baud_cases": [], "valid": False}
    output_bytes = {}
    for baud in (1, 100000, 1104000, 240000000):
        prefix = args.output / ("baud-" + str(baud))
        command = (["wsl.exe", "-e"] if args.wsl else []) + [binary, "--emit", linux(prefix) if args.wsl else str(prefix.resolve()),
            "24000", "2", "16", "256", str(baud), "8", "23063", "1"]
        execution = subprocess.run(command, capture_output=True, timeout=30)
        prefix.with_suffix(".stdout.json").write_bytes(execution.stdout)
        prefix.with_suffix(".stderr.log").write_bytes(execution.stderr)
        if execution.returncode: raise AssertionError("Valid emit baud was rejected: " + str(baud))
        receipt = json.loads(execution.stdout)
        if receipt.get("schema") != "greenfield_sdr_emit/1" or any(receipt.get(k) != v for k, v in fixed.items()):
            raise AssertionError("Emitter configuration receipt mismatch")
        required = 24000 * 2 * 16 * 368 / 256
        effective = baud * 256 / (368 * 2 * 16)
        if (receipt.get("baud_requested") != baud or receipt.get("baud_required_for_source_rate") != required
            or not math.isclose(receipt.get("effective_audio_rate_hz_at_requested_baud", -1), effective, rel_tol=1e-10, abs_tol=1e-12)):
            raise AssertionError("Required/effective baud metadata is incorrect")
        files = {}
        for suffix in ("-symbols.bits", "-reference.pcm", "-boxcar.pcm"):
            path = pathlib.Path(str(prefix) + suffix)
            data = path.read_bytes()
            expected_bytes = 368 if suffix == "-symbols.bits" else 256
            if len(data) != expected_bytes: raise AssertionError("Emitter artifact length differs")
            if suffix in output_bytes and data != output_bytes[suffix]: raise AssertionError("Requested baud changed stimulus bytes")
            output_bytes[suffix] = data
            files[suffix] = {"bytes": len(data), "sha256": sha(path)}
        result["valid_baud_cases"].append({"baud": baud, "receipt": receipt, "files": files})
    for text in ("0", "240000001", "4294967297", "18446744073709551616", "-1", "100000junk"):
        prefix = args.output / ("invalid-" + text.replace("-", "minus"))
        command = (["wsl.exe", "-e"] if args.wsl else []) + [binary, "--emit", linux(prefix) if args.wsl else str(prefix.resolve()),
            "24000", "2", "16", "256", text, "8", "23063", "1"]
        execution = subprocess.run(command, capture_output=True, timeout=30)
        if execution.returncode != 2 or pathlib.Path(str(prefix) + "-symbols.bits").exists():
            raise AssertionError("Invalid baud accepted or produced artifacts: " + text)
        result["invalid_baud_cases"].append({"input": text, "returncode": execution.returncode})
    result["all_three_outputs_byte_identical_across_bauds"] = True
    result["valid"] = True
    (args.output / "results.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--wsl", action="store_true")
    raise SystemExit(run(parser.parse_args()))
