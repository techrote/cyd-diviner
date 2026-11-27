#!/usr/bin/env python3
"""Generate labelled SDR stimuli or demodulate closed physical IQ captures offline."""
from __future__ import annotations

import argparse
import array
import csv
import hashlib
import json
import math
import pathlib
import subprocess

from audio_mvp_simulate import executable_path, moments, wav
from sdr_capture import default_plan, validate_plan


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def invoke(core, wsl, arguments, folder, timeout=180):
    command = (["wsl.exe", "-e"] if wsl else []) + [executable_path(core, wsl)] + arguments
    result = subprocess.run(command, capture_output=True, timeout=timeout, check=False)
    (folder / "core-stdout.json").write_bytes(result.stdout)
    (folder / "core-stderr.log").write_bytes(result.stderr)
    if result.returncode not in (0, 3):
        raise RuntimeError("SDR core failed; stdout/stderr retained")
    return result.returncode, json.loads(result.stdout), command


def pcm(path):
    payload = path.read_bytes()
    if len(payload) % 2:
        raise ValueError("Odd-length PCM16LE artifact")
    values = array.array("h")
    values.frombytes(payload)
    import sys
    if sys.byteorder != "little": values.byteswap()
    return values


def manifest(folder, schema, inputs, core):
    result = {"schema": schema, "inputs": inputs, "core_sha256": digest(core),
              "driver_sha256": digest(pathlib.Path(__file__)),
              "outputs": {path.relative_to(folder).as_posix(): digest(path) for path in sorted(folder.rglob("*")) if path.is_file()}}
    (folder / "manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


def emit_plan(args):
    args.output.mkdir(parents=True, exist_ok=False)
    plan = default_plan()
    plan.update(purpose="HOLD: framed PDM only after independent IQ switching calibration permits these modes/rates",
                calibration_prerequisite="Owner/operator reviews independent IQ switching before authorizing this plan")
    plan["workloads"] = []
    receipts = []
    variants = ((2000, 2, 23000), (1000, 1, 5750), (1000, 2, 11500), (500, 4, 11500))
    for rate, channels, baud in variants:
        name = f"{rate}Hz-{channels}ch-osr4"
        folder = args.output / name
        folder.mkdir()
        prefix = folder / "stimulus"
        code, receipt, command = invoke(args.core, args.wsl, ["--emit", executable_path(prefix, args.wsl),
            str(rate), str(channels), "4", "256", str(baud), str(args.frames), str(args.seed), str(args.stream_id)], folder)
        if (code or receipt.get("schema") != "greenfield_sdr_emit/1" or receipt.get("evidence_class") != "GENERATED_STIMULUS"
            or receipt.get("no_RF_measurement") is not True or receipt.get("baud_required_for_source_rate") != baud):
            raise ValueError("Generated stimulus receipt is invalid/mismatched")
        packed, reference, boxcar = (pathlib.Path(str(prefix) + suffix) for suffix in ("-symbols.bits", "-reference.pcm", "-boxcar.pcm"))
        if (len(packed.read_bytes()) != receipt["packed_bytes"] or len(reference.read_bytes()) != receipt["pcm_bytes"]
            or len(boxcar.read_bytes()) != receipt["pcm_bytes"]):
            raise ValueError("Generated stimulus artifact length mismatch")
        receipt.update(core_sha256=digest(args.core), command=command)
        (folder / "stimulus.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        wav(folder / "reference.wav", pcm(reference), channels, rate)
        wav(folder / "boxcar-digital-only.wav", pcm(boxcar), channels, rate)
        receipts.append(receipt)
        # Paths are relative to the generated plan's parent, preserving checkout
        # portability. Analyzer --stimulus-root points to that same directory.
        relative = lambda path: path.relative_to(args.output.parent).as_posix()
        for mode in ("api", "ram"):
            plan["workloads"].append({"name": f"framed-pdm/{mode}/{name}", "mode": mode, "baud": baud,
                "backoff_quarter_db": 96, "bit_count": receipt["symbols_total"], "bits_file": relative(packed),
                "stimulus": receipt, "reference_pcm_file": relative(reference), "reference_pcm_sha256": digest(reference),
                "boxcar_pcm_file": relative(boxcar), "boxcar_pcm_sha256": digest(boxcar),
                "generated_bits_sha256": digest(packed)})
    validate_plan(plan)
    plan_path = args.output.parent / args.plan_name
    if plan_path.exists():
        raise ValueError("Refusing to overwrite an existing framed RF plan")
    plan_path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    (args.output / "stimuli.json").write_text(json.dumps({"schema": "cyd-sdr-stimuli/1",
        "evidence_class": "GENERATED_STIMULUS", "no_RF_measurement": True, "receipts": receipts}, indent=2) + "\n", encoding="utf-8")
    manifest(args.output, "cyd-sdr-stimuli-manifest/1", {str(plan_path): digest(plan_path)}, args.core)
    return 0


def expected_payloads(packed, stimulus):
    bits = stimulus["payload_bits"]
    symbols = stimulus["frame_symbols"]
    if symbols != 112 + bits or len(packed) * 8 != stimulus["symbols_total"]:
        raise ValueError("Unknown generated symbol framing")
    result = {}
    for frame in range(stimulus["frame_count"]):
        start = frame * symbols // 8
        if packed[start:start + 8] != bytes.fromhex("55555555d391c5a7"):
            raise ValueError("Uploaded symbols differ from the generated training/sync framing")
        header = packed[start + 8:start + 14]
        sequence = int.from_bytes(header[:2], "little")
        payload = bytearray(bits // 8)
        first = frame * symbols + 112
        for index in range(bits):
            bit = (packed[(first + index) // 8] >> (7 - (first + index) % 8)) & 1
            payload[index // 8] |= bit << (index % 8)
        result[sequence] = bytes(payload)
    return result


def frame_touches_erasure(frame, erasures):
    start = frame["first_iq_sample"]
    end = start + math.ceil((112 + frame["payload_bits"]) * frame["recovered_samples_per_symbol"])
    return any(start < row["file_sample_offset"] + row["samples"] and end > row["file_sample_offset"] for row in erasures)


def compare_frames(frames, raw_pcm, source, reference, erasures):
    known, result, seen = expected_payloads(source, reference["stimulus"]), [], set()
    reference_pcm = reference["pcm"]
    selected_reference, selected_decoded = array.array("h"), array.array("h")
    for frame in frames:
        row = dict(frame)
        row["touches_host_IQ_erasure"] = frame_touches_erasure(frame, erasures)
        sequence = frame["sequence"]
        expected = known.get(sequence)
        matches_config = all(frame.get(key) == reference["stimulus"].get(key) for key in
                             ("source_rate_hz", "channels", "oversample", "payload_bits", "stream_id"))
        row["source_config_mismatch"] = expected is not None and not matches_config
        if not matches_config:
            expected = None
        row["known_source_sequence"] = expected is not None
        row["duplicate_for_quality"] = sequence in seen
        if expected is not None:
            actual = bytes.fromhex(frame["payload_hex"])
            if len(actual) != len(expected): raise ValueError("Decoded payload length differs from known source")
            row["payload_bits_compared"] = len(actual) * 8
            row["payload_bit_errors"] = sum((left ^ right).bit_count() for left, right in zip(expected, actual))
            if frame.get("pcm_written") and sequence not in seen:
                channels, count = frame["channels"], frame["payload_bits"] // (frame["channels"] * frame["oversample"])
                start = frame["pcm_offset_frames"] * channels
                ideal_start = sequence * count * channels
                ideal = reference_pcm[ideal_start:ideal_start + count * channels]
                actual_pcm = raw_pcm[start:start + count * channels]
                if len(ideal) != count * channels or len(actual_pcm) != count * channels:
                    raise ValueError("Known-sequence digital reconstruction sample ranges are incomplete")
                selected_reference.extend(ideal)
                selected_decoded.extend(actual_pcm)
            seen.add(sequence)
        result.append(row)
    return result, selected_reference, selected_decoded


def analyze(args):
    capture = json.loads(args.results.read_text(encoding="utf-8-sig"))
    if capture.get("schema") != "cyd-sdr-capture/1" or capture.get("evidence_class") != "physical_iq_acquisition":
        raise ValueError("Physical SDR capture required; offline IQ models cannot be relabelled RF evidence")
    if capture.get("status") == "IN PROGRESS":
        raise ValueError("Only closed captures may be demodulated/published")
    artifacts = json.loads((args.results.parent / "artifact-hashes.json").read_text())
    for name, expected in artifacts.items():
        path = args.results.parent / name
        if not path.is_file() or digest(path) != expected:
            raise ValueError("Physical capture artifact hash mismatch: " + name)
    args.output.mkdir(parents=True, exist_ok=False)
    result = {"schema": "cyd-sdr-physical-demodulation/1", "evidence_class": "HOST_DEMODULATION_OF_PHYSICAL_IQ",
        "status": "IN PROGRESS", "capture_status": capture["status"], "capture_results_sha256": digest(args.results), "core_sha256": digest(args.core),
        "identities": capture.get("identities"), "runs": [], "one_way_latency_us": None,
        "limitations": ["Raw IQ acquisition, observed signal/modulation and successfully decoded proprietary frames are distinct evidence.",
            "IQ host placeholders are labelled erasures, not received RF samples. Frame overlap with erasures is recorded.",
            "Payload bits deliberately have no CRC and bit errors remain accepted/measured against a known generated source when supplied.",
            "Header CRC/config rejection counters count search candidates, not RF packet/FCS errors.",
            "Decoded WAV concatenates received frames without gap concealment; it is not continuous on-board real-time playback.",
            "Independent board clocks and host/batch processing do not establish end-to-end latency."]}
    try:
        for run in capture["runs"]:
            folder = args.output / run["run_id"]
            folder.mkdir()
            row = {"run_id": run["run_id"], "workload": run["workload"], "acquisition_status": run["status"],
                "signal_verification": "NOT ESTABLISHED", "demodulation_status": "NOT RUN", "rf_origin_verified": None,
                "iq_integrity": run.get("iq_integrity"), "settings": run["settings"], "acquisition_error": run.get("error")}
            result["runs"].append(row)
            iq = args.results.parent / run["run_id"] / "iq-indexed.i8"
            erasure_path = iq.with_name("iq-erasures.json")
            required = (iq, erasure_path, iq.with_name("iq-integrity.json"), iq.with_name("iq-frames.csv"))
            missing = [path.name for path in required if not path.is_file()]
            if run["status"] != "ACQUIRED" or missing or (iq.is_file() and not iq.stat().st_size):
                row["demodulation_status"] = "NOT RUN: INCOMPLETE ACQUISITION"
                row["skip_reason"] = ("Original acquisition status is " + run["status"] if run["status"] != "ACQUIRED"
                    else "Finalized IQ sidecars are absent: " + ", ".join(missing) if missing else "Finalized IQ artifact is empty")
                continue
            if iq.relative_to(args.results.parent).as_posix() not in artifacts:
                raise ValueError("IQ artifact is absent from immutable capture manifest")
            erasures = json.loads(erasure_path.read_text())["erasures"]
            row["iq_erasures"] = erasures
            row["iq_sha256"] = digest(iq)
            start = run.get("iq_integrity", {}).get("start") or {}
            sample_rate = start.get("output_sample_rate_hz")
            if not isinstance(sample_rate, (int, float)) or sample_rate <= 0:
                raise ValueError("Measured IQ start metadata lacks a sample rate")
            raw_pcm, frame_path = folder / "decoded.pcm16le", folder / "frames.jsonl"
            stream_id = run["settings"].get("stimulus", {}).get("stream_id", args.stream_id)
            code, receipt, command = invoke(args.core, args.wsl, ["--demod-i8", executable_path(iq, args.wsl),
                str(sample_rate), str(run["settings"]["baud"]), executable_path(raw_pcm, args.wsl),
                executable_path(frame_path, args.wsl), str(stream_id)], folder, args.timeout)
            if (receipt.get("schema") != "greenfield_sdr_demod/1" or receipt.get("evidence_class") != "HOST_DEMODULATION"
                or receipt.get("input_iq_samples") * 2 != iq.stat().st_size or receipt.get("sample_rate_hz") != sample_rate
                or receipt.get("requested_symbol_rate") != run["settings"]["baud"] or receipt.get("stream_id") != stream_id):
                raise ValueError("Demodulator receipt does not correlate with physical input/configuration")
            row.update(core_result=receipt, command=command)
            frames = [json.loads(line) for line in frame_path.read_text().splitlines()]
            if len(frames) != receipt["frames_decoded"] or raw_pcm.stat().st_size != receipt["pcm_bytes"]:
                raise ValueError("Demodulated frame/PCM artifact counts differ from core receipt")
            for frame in frames: frame["touches_host_IQ_erasure"] = frame_touches_erasure(frame, erasures)
            row["frames_without_host_IQ_erasure"] = sum(not frame["touches_host_IQ_erasure"] for frame in frames)
            row["demodulation_status"] = "NO FRAMES" if not frames else "CONFIG CHANGED" if receipt["config_changes"] else "FRAMES DECODED"
            row["signal_verification"] = "VALID PROPRIETARY HEADER FRAMES OBSERVED" if frames else "NOT ESTABLISHED"
            if code == 0 and frames:
                samples = pcm(raw_pcm)
                wav(folder / "decoded-received-concatenated.wav", samples, receipt["channels"], receipt["source_rate_hz"])
                stimulus = run["settings"].get("stimulus")
                reference_name = run["settings"].get("reference_pcm_file")
                if stimulus and reference_name and args.stimulus_root:
                    reference_path = args.stimulus_root / reference_name
                    if digest(reference_path) != run["settings"]["reference_pcm_sha256"]:
                        raise ValueError("Generated reference digest differs from physical plan")
                    uploaded = args.results.parent / run["run_id"] / "uploaded-bits.bin"
                    if digest(uploaded) != run["settings"]["generated_bits_sha256"]:
                        raise ValueError("Physical uploaded bits differ from generated stimulus")
                    frames, ideal, actual = compare_frames(frames, samples, uploaded.read_bytes(),
                        {"stimulus": stimulus, "pcm": pcm(reference_path)}, erasures)
                    row["known_payload_bits_compared"] = sum(frame.get("payload_bits_compared", 0) for frame in frames if not frame.get("duplicate_for_quality"))
                    row["known_payload_bit_errors"] = sum(frame.get("payload_bit_errors", 0) for frame in frames if not frame.get("duplicate_for_quality"))
                    row["known_payload_bits_compared_outside_iq_erasures"] = sum(frame.get("payload_bits_compared", 0) for frame in frames
                        if not frame.get("duplicate_for_quality") and not frame["touches_host_IQ_erasure"])
                    row["known_payload_bit_errors_outside_iq_erasures"] = sum(frame.get("payload_bit_errors", 0) for frame in frames
                        if not frame.get("duplicate_for_quality") and not frame["touches_host_IQ_erasure"])
                    row["known_frames_unique"] = len({frame["sequence"] for frame in frames if frame.get("known_source_sequence")})
                    row["generated_frames_source"] = stimulus["frame_count"]
                    row["quality_received_domain"] = [moments(ideal[ch::receipt["channels"]], actual[ch::receipt["channels"]]) for ch in range(receipt["channels"])] if ideal else []
                    if run["settings"].get("boxcar_pcm_file"):
                        boxcar_path = args.stimulus_root / run["settings"]["boxcar_pcm_file"]
                        if digest(boxcar_path) != run["settings"]["boxcar_pcm_sha256"]:
                            raise ValueError("Generated codec reference digest differs from physical plan")
                        _, codec_reference, codec_actual = compare_frames(frames, samples, uploaded.read_bytes(),
                            {"stimulus": stimulus, "pcm": pcm(boxcar_path)}, erasures)
                        row["quality_vs_boxcar_received_domain"] = [moments(codec_reference[ch::receipt["channels"]],
                            codec_actual[ch::receipt["channels"]]) for ch in range(receipt["channels"])] if codec_reference else []
                    if ideal:
                        wav(folder / "reference-received-domain.wav", ideal, receipt["channels"], receipt["source_rate_hz"])
            row["frames"] = frames
            (folder / "frames-annotated.jsonl").write_text("".join(json.dumps(frame) + "\n" for frame in frames), encoding="utf-8")
        result["status"] = "ANALYSIS COMPLETE"
    except BaseException as error:
        result.update(status="FAILED", error=str(error))
        raise
    finally:
        (args.output / "results.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        rows = [{key: value for key, value in row.items() if not isinstance(value, (dict, list))} for row in result["runs"]]
        fields = list(dict.fromkeys(key for row in rows for key in row)) or ["run_id", "demodulation_status"]
        with (args.output / "decoded-summary.csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)
        quality_rows = [{"run_id": row["run_id"], "channel": channel, "comparison": key, **quality}
            for row in result["runs"] for key in ("quality_received_domain", "quality_vs_boxcar_received_domain")
            for channel, quality in enumerate(row.get(key, []))]
        with (args.output / "rf-quality.csv").open("w", newline="", encoding="utf-8") as stream:
            fields = list(dict.fromkeys(key for row in quality_rows for key in row)) or ["run_id", "channel", "comparison"]
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(quality_rows)
        manifest(args.output, "cyd-sdr-physical-demodulation-manifest/1", {str(args.results): digest(args.results),
            str(args.results.parent / "artifact-hashes.json"): digest(args.results.parent / "artifact-hashes.json")}, args.core)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("emit-plan", "analyze"):
        child = commands.add_parser(name)
        child.add_argument("--core", type=pathlib.Path, required=True)
        child.add_argument("--wsl", action="store_true")
        child.add_argument("--output", type=pathlib.Path, required=True)
        child.add_argument("--stream-id", type=int, default=1)
        if name == "emit-plan":
            child.add_argument("--frames", type=int, default=16)
            child.add_argument("--seed", type=int, default=23063)
            child.add_argument("--plan-name", default="plan-sdr-framed.json")
        else:
            child.add_argument("results", type=pathlib.Path)
            child.add_argument("--stimulus-root", type=pathlib.Path)
            child.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    return emit_plan(args) if args.command == "emit-plan" else analyze(args)


if __name__ == "__main__":
    raise SystemExit(main())
