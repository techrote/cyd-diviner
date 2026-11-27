#!/usr/bin/env python3
"""Run the portable continuous-PDM core offline; never open radio/serial ports."""
from __future__ import annotations

import argparse
import array
import csv
import hashlib
import json
import math
import pathlib
import subprocess
import sys
import wave


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def executable_path(path, use_wsl=False):
    resolved = path.resolve().as_posix()
    if not use_wsl:
        return str(path.resolve())
    if len(resolved) < 3 or resolved[1:3] != ":/" or not resolved[0].isalpha():
        raise ValueError("--wsl requires a local Windows drive path")
    return "/mnt/" + resolved[0].lower() + resolved[2:]


def pcm(path, frames, channels):
    payload = path.read_bytes()
    if len(payload) != frames * channels * 2:
        raise ValueError("Core PCM length differs from the requested finite frame count")
    values = array.array("h")
    values.frombytes(payload)
    if sys.byteorder != "little":
        values.byteswap()
    return values


def moments(reference, decoded):
    if len(reference) != len(decoded) or not reference:
        raise ValueError("Digital quality statistics require equal nonempty sample sequences")
    n = len(reference)
    sx, sy = sum(reference), sum(decoded)
    xx, yy = sum(x * x for x in reference), sum(y * y for y in decoded)
    xy = sum(x * y for x, y in zip(reference, decoded))
    errors = [y - x for x, y in zip(reference, decoded)]
    error_energy = sum(value * value for value in errors)
    denominator = (n * xx - sx * sx) * (n * yy - sy * sy)
    correlation = (n * xy - sx * sy) / math.sqrt(denominator) if denominator > 0 else None
    return {"samples": n, "reference_rms_pcm16": math.sqrt(xx / n),
            "decoded_rms_pcm16": math.sqrt(yy / n), "error_rms_pcm16": math.sqrt(error_energy / n),
            "error_peak_pcm16": max(abs(value) for value in errors), "error_mean_pcm16": sum(errors) / n,
            "correlation": max(-1.0, min(1.0, correlation)) if correlation is not None else None,
            "snr_db": 10 * math.log10(xx / error_energy) if xx and error_energy else None,
            "perfect_sample_match": error_energy == 0, "snr_unavailable_reason":
                "zero_error (unbounded mathematical SNR)" if not error_energy else "zero_reference_energy" if not xx else None,
            "reference_fullscale_samples": sum(abs(value) >= 32767 for value in reference),
            "decoded_fullscale_samples": sum(abs(value) >= 32767 for value in decoded)}


def aligned_pair(reference, decoded, lag):
    reference_start, decoded_start = max(0, -lag), max(0, lag)
    count = min(len(reference) - reference_start, len(decoded) - decoded_start)
    return reference[reference_start:reference_start + count], decoded[decoded_start:decoded_start + count]


def digital_quality(reference, decoded, channels, maximum_lag=64):
    rows = []
    for channel in range(channels):
        ref, out = reference[channel::channels], decoded[channel::channels]
        direct = moments(ref, out)
        # A bounded first-block search is an offline filter alignment aid. It
        # cannot establish one-way RF latency or distinguish pure-tone aliases.
        search_n = min(len(ref), 4096 + maximum_lag)
        lags = range(-min(maximum_lag, len(ref) - 2), min(maximum_lag, len(ref) - 2) + 1)
        scores = []
        for lag in lags:
            left, right = aligned_pair(ref[:search_n], out[:search_n], lag)
            score = moments(left, right)["correlation"]
            scores.append((score if score is not None else -2, -abs(lag), lag))
        lag = max(scores)[2]
        left, right = aligned_pair(ref, out, lag)
        rows.append({"channel": channel, "unaligned": direct, "alignment_lag_frames": lag,
                     "aligned": moments(left, right), "alignment_search_max_lag_frames": maximum_lag,
                     "alignment_search_max_frames": 4096 + maximum_lag,
                     "alignment_method": "maximize signed Pearson correlation in bounded offline search; positive lag means decoded follows reference"})
    return rows


def wav(path, samples, channels, rate):
    values = array.array("h", samples)
    if sys.byteorder != "little":
        values.byteswap()
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(channels)
        stream.setsampwidth(2)
        stream.setframerate(rate)
        stream.writeframes(values.tobytes())


def run(args):
    if (not args.core.is_file() or args.rate not in (24000, 48000) or args.channels not in (1, 2, 4)
        or args.block_frames not in (8, 16, 32) or args.osr not in (16, 32)
        or not 128 <= args.frames <= args.rate * 60 or not 0 <= args.max_lag <= 256):
        raise ValueError("Expected existing core, supported finite configuration, and <=60-second model")
    cases = []
    for value in args.cases:
        name, separator, strength = value.partition(":")
        if name not in ("clean", "flip", "burst", "stuck", "erase") or not separator or not strength.isdigit():
            raise ValueError("Cases are clean|flip|burst|stuck|erase:nonnegative_integer")
        cases.append((name, int(strength)))
    if not cases or len(cases) > 50:
        raise ValueError("Supply 1..50 explicit offline cases")
    args.output.mkdir(parents=True, exist_ok=False)
    result = {"schema": "cyd-audio-mvp-offline/1", "evidence_class": "OFFLINE_MODEL", "status": "IN PROGRESS",
              "core_sha256": digest(args.core), "driver_sha256": digest(pathlib.Path(__file__)),
              "configuration": {"sample_rate_hz": args.rate, "channels": args.channels, "block_frames": args.block_frames,
                  "osr": args.osr, "total_frames": args.frames, "seed": args.seed}, "cases": [],
              "limitations": ["Computed continuous raw-PDM bitstream impairment; no radio, packet CRC, I2S, DAC, microphone or analog measurement.",
                  "The continuous model preserves codec state across blocks. Packetized PDM RF behavior is separate evidence.",
                  "Offline lag adjustment is a digital reconstruction aid, not synchronized one-way transport latency.",
                  "Each deterministic impairment specimen is not a statistical characterization of RF loss."]}
    try:
        for index, (name, strength) in enumerate(cases, 1):
            folder = args.output / f"{index:02}-{name}-{strength}"
            folder.mkdir()
            reference_path, decoded_path = folder / "reference.pcm16le", folder / "reconstructed.pcm16le"
            use_wsl = getattr(args, "wsl", False)
            command = (["wsl.exe", "-e"] if use_wsl else []) + [executable_path(args.core, use_wsl), "--simulate", str(args.rate), str(args.channels), str(args.block_frames),
                       str(args.osr), str(args.frames), str(args.seed), name, str(strength),
                       executable_path(reference_path, use_wsl), executable_path(decoded_path, use_wsl)]
            case = {"name": name, "strength": strength, "status": "IN PROGRESS", "command": command}
            result["cases"].append(case)
            execution = subprocess.run(command, capture_output=True, timeout=args.timeout, check=False)
            (folder / "stdout.json").write_bytes(execution.stdout)
            (folder / "stderr.log").write_bytes(execution.stderr)
            if execution.returncode:
                raise RuntimeError("Portable model executable failed; stdout/stderr retained")
            receipt = json.loads(execution.stdout)
            if receipt.get("schema") != "offline_audio_model/1" or receipt.get("evidence_class") != "OFFLINE_MODEL":
                raise ValueError("Unexpected portable core model schema/evidence class")
            expected = {"sample_rate_hz": args.rate, "channels": args.channels, "block_frames": args.block_frames,
                        "oversample": args.osr, "total_pcm_frames": args.frames, "seed": args.seed,
                        "impairment": name, "strength": strength}
            if any(receipt.get(key) != value for key, value in expected.items()):
                raise ValueError("Portable core configuration receipt differs from invoked model")
            case["core_result"] = receipt
            reference = pcm(reference_path, args.frames, args.channels)
            decoded = pcm(decoded_path, args.frames, args.channels)
            case["digital_quality"] = digital_quality(reference, decoded, args.channels, args.max_lag)
            wav(folder / "reference.wav", reference, args.channels, args.rate)
            wav(folder / "reconstructed.wav", decoded, args.channels, args.rate)
            comparison = array.array("h")
            for frame in range(args.frames):
                start = frame * args.channels
                comparison.extend(reference[start:start + args.channels])
                comparison.extend(decoded[start:start + args.channels])
            wav(folder / "comparison.wav", comparison, args.channels * 2, args.rate)
            case["comparison_wav_channels"] = [f"reference_{index}" for index in range(args.channels)] + [f"reconstructed_{index}" for index in range(args.channels)]
            case["status"] = "COMPUTED"
        result["status"] = "MODEL COMPLETE"
    except BaseException as error:
        result["status"], result["error"] = "FAILED", str(error)
        if result["cases"] and result["cases"][-1]["status"] == "IN PROGRESS":
            result["cases"][-1]["status"] = "FAILED"
        raise
    finally:
        (args.output / "results.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        rows = [{"case": case["name"], "strength": case["strength"], "status": case["status"], "channel": row["channel"],
                 "alignment_lag_frames": row["alignment_lag_frames"],
                 **{prefix + "_" + key: value for prefix in ("unaligned", "aligned") for key, value in row[prefix].items()}}
                for case in result["cases"] for row in case.get("digital_quality", [])]
        fields = list(dict.fromkeys(key for row in rows for key in row)) or ["case", "status"]
        with (args.output / "quality.csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)
        manifest = {"schema": "cyd-audio-mvp-offline-manifest/1", "evidence_class": "OFFLINE_MODEL",
                    "status": result["status"], "core_sha256": result["core_sha256"], "driver_sha256": result["driver_sha256"],
                    "outputs": {path.relative_to(args.output).as_posix(): digest(path) for path in sorted(args.output.rglob("*")) if path.is_file()}}
        (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=pathlib.Path, required=True)
    parser.add_argument("--wsl", action="store_true", help="Run an ELF core through wsl.exe with local-drive paths translated")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--rate", type=int, default=24000)
    parser.add_argument("--channels", type=int, default=2)
    parser.add_argument("--block-frames", type=int, default=16)
    parser.add_argument("--osr", type=int, default=16)
    parser.add_argument("--frames", type=int, default=48000)
    parser.add_argument("--seed", type=int, default=23063)
    parser.add_argument("--max-lag", type=int, default=64)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--cases", nargs="+", default=["clean:0", "flip:1000", "burst:128", "stuck:128", "erase:128"])
    args = parser.parse_args()
    try:
        return run(args)
    except Exception as error:
        print(f"Offline model failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
