#!/usr/bin/env python3
"""Offline finite native-IQ demodulation with declared payload-independent trials."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics

import numpy as np

from audio_mvp_simulate import executable_path, moments
from sdr_analyze import compare_frames, expected_payloads, invoke, pcm
from sdr_metal_envelope import decode_words, load_capture, safe_path, verified_snapshots, write_csv

CORE_SHA256 = "09c500232d4046ac6021dfcb8e83c998ff96846296abcc583ef62ace176b8f28"
CONFIG_FIELDS = ("source_rate_hz", "channels", "oversample", "payload_bits", "stream_id")
DEFAULT_MULTIPLIERS = (1., 1.004, 1.008)


def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def native_i8(payload):
    """Arithmetic shift of native signed fields; no Q flip/DC fit/gap filling."""
    i, q, metadata = decode_words(payload)
    out = np.empty((len(i), 2), dtype=np.int8)
    out[:, 0], out[:, 1] = i >> 2, q >> 2
    return out.tobytes(), {"samples": len(i), "native_I10_range": [int(i.min()), int(i.max())],
        "native_minus_Q10_range": [int(q.min()), int(q.max())],
        "metadata12_range": [int(metadata.min()), int(metadata.max())],
        "metadata12_unique_values": int(len(np.unique(metadata))),
        "method": "signed arithmetic right shift2; -512 maps-128,511 maps127; native I,-Q order; no Q flip, mean removal or resampling"}


def load_stimuli(path):
    document = json.loads(path.read_text(encoding="utf-8-sig"))
    if document.get("schema") != "cyd-sdr-metal-stimuli/1":
        raise ValueError("Expected generated metal stimulus manifest")
    result = {}
    for record in document["records"]:
        meta, prefix = record["metadata"], record["prefix"]
        if (meta.get("schema") != "greenfield_sdr_emit/1" or meta.get("evidence_class") != "GENERATED_STIMULUS"
            or meta.get("no_RF_measurement") is not True): raise ValueError("Unlabelled or invalid generated stimulus")
        files = {}
        for name, expected in record["files"].items():
            file = safe_path(path.parent, name)
            if not file.is_file() or digest(file) != expected: raise ValueError("Generated stimulus hash differs: " + name)
            files[name] = file
        bits, reference, boxcar = (files[prefix + suffix] for suffix in ("-symbols.bits", "-reference.pcm", "-boxcar.pcm"))
        packed = bits.read_bytes()
        known = expected_payloads(packed, meta)
        if (len(packed) != meta["packed_bytes"] or len(known) != meta["frame_count"]
            or set(known) != set(range(meta["frame_count"]))
            or reference.stat().st_size != meta["pcm_bytes"] or boxcar.stat().st_size != meta["pcm_bytes"]
            or meta["pcm_bytes"] != meta["frame_count"] * meta["payload_bits"] // meta["oversample"] * 2):
            raise ValueError("Stimulus framing, sequence ring or reference lengths differ")
        key = digest(bits)
        if key in result: raise ValueError("Ambiguous duplicate stimulus packed hash")
        result[key] = {"metadata": meta, "prefix": prefix, "packed": packed,
            "reference": pcm(reference), "boxcar": pcm(boxcar), "files_sha256": record["files"]}
    if not result: raise ValueError("Empty stimulus manifest")
    return document, result


def candidate_centres(settings, tx, cpu_mhz, multipliers):
    if not multipliers or any(not math.isfinite(v) or v <= 0 for v in multipliers):
        raise ValueError("Clock-centre multipliers must be positive finite numbers")
    baud = settings["baud"]
    if baud:
        return [{"symbol_rate": baud, "basis": "requested paced baud", "multiplier": 1.}]
    if (not tx or type(cpu_mhz) is not int or cpu_mhz <= 0 or not tx.get("processed_bits") or not tx.get("elapsed_cycles")):
        raise ValueError("Unpaced trials require validated software write count/cycles and CPU clock")
    mean = tx["processed_bits"] * cpu_mhz * 1e6 / tx["elapsed_cycles"]
    return [{"symbol_rate": mean * value, "multiplier": value,
             "basis": "whole TX-loop software write mean times declared multiplier; not RF clock proof"} for value in multipliers]


def matching_header(frame, stimulus):
    return frame.get("header_crc_valid") is True and all(frame.get(k) == stimulus[k] for k in CONFIG_FIELDS)


def trial_score(frames, stimulus):
    matching = [frame for frame in frames if matching_header(frame, stimulus)]
    fits = [frame.get("training_fit_error") for frame in matching]
    if any(type(v) not in (int, float) or not math.isfinite(v) or v < 0 for v in fits):
        raise ValueError("Header training-fit values must be finite and nonnegative")
    return {"matching_header_config_frames": len(matching),
            "matching_training_fit_error_mean": statistics.mean(fits) if fits else None}


def select_trial(trials):
    """Known payload, BER/confidence/PCM quality never enter this ranking."""
    candidates = [i for i, row in enumerate(trials) if row.get("matching_header_config_frames", 0)]
    return min(candidates, key=lambda i: (-trials[i]["matching_header_config_frames"],
        trials[i]["matching_training_fit_error_mean"], i)) if candidates else None


def validate_trial(receipt, frames, pcm_path, sample_count, sample_rate, symbol_rate, stream_id):
    if (receipt.get("schema") != "greenfield_sdr_demod/1" or receipt.get("evidence_class") != "HOST_DEMODULATION"
        or receipt.get("input_iq_samples") != sample_count or receipt.get("sample_rate_hz") != sample_rate
        or not math.isclose(receipt.get("requested_symbol_rate", -1), symbol_rate, rel_tol=1e-10)
        or receipt.get("stream_id") != stream_id or receipt.get("frames_decoded") != len(frames)
        or pcm_path.stat().st_size != receipt.get("pcm_bytes")):
        raise ValueError("Core trial receipt does not correlate with native conversion/configuration")
    for frame in frames:
        count = frame.get("payload_bits")
        sps = frame.get("recovered_samples_per_symbol")
        if (frame.get("schema") != "greenfield_sdr_frame/1" or frame.get("header_crc_valid") is not True
            or frame.get("payload_crc_present") is not False or count not in (128, 256)
            or len(bytes.fromhex(frame["payload_hex"])) * 8 != count
            or type(frame.get("first_iq_sample")) is not int or not 0 <= frame["first_iq_sample"] < sample_count
            or type(sps) not in (int, float) or not math.isfinite(sps) or sps <= 0):
            raise ValueError("Invalid bounded decoded frame receipt")


def received_quality(frames, samples, stimulus):
    # Reuse the uploaded-ring sequence oracle. Repeated sequences are annotated
    # and excluded from unique-quality totals, while all-frame BER stays visible.
    annotated, ideal, actual = compare_frames(frames, samples, stimulus["packed"],
        {"stimulus": stimulus["metadata"], "pcm": stimulus["reference"]}, [])
    _, boxcar, actual_boxcar = compare_frames(frames, samples, stimulus["packed"],
        {"stimulus": stimulus["metadata"], "pcm": stimulus["boxcar"]}, [])
    channels = stimulus["metadata"]["channels"]
    known = [f for f in annotated if f.get("known_source_sequence")]
    unique = [f for f in known if not f["duplicate_for_quality"]]
    totals = lambda values: {"payload_bits_compared": sum(f["payload_bits_compared"] for f in values),
                            "payload_bit_errors": sum(f["payload_bit_errors"] for f in values)}
    statistics_by_channel = lambda reference, decoded: [moments(reference[c::channels], decoded[c::channels])
                                                      for c in range(channels)] if reference else None
    return annotated, {"scope": "received-domain known ring sequences; quality uses first occurrence per sequence, no loss concealment or timing alignment",
        "known_frames_all": len(known), "known_sequences_unique": len(unique),
        "all_known_frames_BER_counts": totals(known), "unique_known_sequences_BER_counts": totals(unique),
        "vs_generated_source": statistics_by_channel(ideal, actual),
        "vs_generated_ideal_boxcar": statistics_by_channel(boxcar, actual_boxcar)}


def analyze(args):
    if args.output.resolve().is_relative_to(args.results.parent.resolve()) or args.output.resolve().is_relative_to(args.stimulus_manifest.parent.resolve()):
        raise ValueError("Analysis output must be outside immutable capture and stimulus directories")
    if not math.isfinite(args.timeout) or args.timeout <= 0: raise ValueError("Finite positive per-trial timeout required")
    if digest(args.core) != args.core_sha256: raise ValueError("Demodulator ELF digest differs from declared frozen core")
    capture, hashes = load_capture(args.results)
    native = list(verified_snapshots(args.results.parent, capture, hashes))
    stimulus_document, stimuli = load_stimuli(args.stimulus_manifest)
    # Verify uploaded source association before creating any analysis output.
    for run, _, _, _ in native:
        uploaded = safe_path(args.results.parent, run["run_id"] + "/uploaded-bits.bin")
        key = digest(uploaded)
        relative = uploaded.relative_to(args.results.parent.resolve()).as_posix()
        if hashes.get(relative) != key or run.get("uploaded_bits_sha256") != key or key not in stimuli:
            raise ValueError("Physical uploaded bit hash is absent/mismatched/unknown in stimulus manifest")
        if uploaded.read_bytes() != stimuli[key]["packed"]: raise ValueError("Uploaded source bytes differ")
    multipliers = tuple(args.unpaced_multipliers)
    if not multipliers or any(not math.isfinite(v) or v <= 0 for v in multipliers): raise ValueError("Positive finite multipliers required")
    args.output.mkdir(parents=True, exist_ok=False)
    result = {"schema": "cyd-sdr-metal-demod/1", "evidence_class": "HOST_DEMODULATION_OF_PHYSICAL_NATIVE_IQ",
        "status": "IN PROGRESS", "capture_status": capture["status"], "capture_results_sha256": digest(args.results),
        "capture_artifact_manifest_sha256": digest(args.results.parent / "artifact-hashes.json"),
        "identities": capture["identities"], "core_sha256": digest(args.core),
        "stimulus_manifest_sha256": digest(args.stimulus_manifest), "stimulus_provenance": stimulus_document,
        "trial_policy": {"paced": "one requested-baud centre", "unpaced_multipliers": multipliers,
            "selection": "largest valid matching header/config frame count; lowest mean training-fit error; earliest declared trial on tie; no payload or BER criterion",
            "decoder_internal_grid": "frozen core defaults, clock search +/-4000ppm about each declared centre"},
        "runs": [], "one_way_latency_us": None, "limitations": [
            "Finite independently timed native bursts; cropped first/last frames are not RF packet-loss counts.",
            "Header CRC/config rejects count search candidates, not independent packets or RF/FCS errors.",
            "Core sequence-gap counters assume16-bit monotonic sequences; replaying the uploaded0..31 ring can wrap, so these counters are not RF loss estimates.",
            "Payload bits have no CRC; known uploaded-ring BER remains measured only for matching known sequences.",
            "I10/native-minus-Q10 converted by deterministic arithmetic shift2; no Q flip, DC removal, resampling or erasure fabrication.",
            "All declared trials remain visible; candidate selection excludes payload bits, BER, confidence and reconstruction quality.",
            "Recovered symbol/audio rates use nominal receiver clocks; header rate is a source label, not unpaced playback proof.",
            "PCM is digital received-frame concatenation, not continuous waveform/DAC/audio output or synchronized latency proof."]}
    trial_csv, frame_csv, quality_csv = [], [], []
    by_id = {}
    for run in capture["runs"]:
        row = {"run_id": run["run_id"], "workload": run["workload"], "settings": run["settings"],
            "acquisition_status": run["status"], "acquisition_error": run.get("error"),
            "demodulation_status": "NOT RUN: INCOMPLETE ACQUISITION" if run["status"] != "ACQUIRED" else "NOT RUN", "snapshots": []}
        result["runs"].append(row); by_id[run["run_id"]] = row
    try:
        for run, receipt, path, payload in native:
            source_key = run["uploaded_bits_sha256"]; stimulus = stimuli[source_key]; meta = stimulus["metadata"]
            folder = args.output / run["run_id"] / Path(receipt["file"]).stem
            folder.mkdir(parents=True)
            converted, conversion = native_i8(payload)
            iq_path = folder / "native-shift2.i8"; iq_path.write_bytes(converted)
            conversion.update(native_raw_sha256=digest(path), converted_i8_sha256=digest(iq_path), source_header=receipt["header"])
            (folder / "conversion.json").write_text(json.dumps(conversion, indent=2) + "\n", encoding="utf-8")
            snapshot = {"source_file": receipt["file"], "raw_sha256": receipt["sha256"], "conversion": conversion,
                "stimulus_prefix": stimulus["prefix"], "stimulus_metadata": meta, "trials": [],
                "selected_trial_index": None, "demodulation_status": "NO MATCHING HEADERS", "RF_origin_verified": None,
                "RF_packet_loss_fraction": None, "one_way_latency_us": None}
            by_id[run["run_id"]]["snapshots"].append(snapshot)
            rate = receipt["header"]["nominal_rate_hz"]
            candidates = candidate_centres(run["settings"], run.get("transmitter_result"), capture.get("target_info", {}).get("tx", {}).get("cpu_mhz"), multipliers)
            for index, candidate in enumerate(candidates):
                trial_folder = folder / f"trial-{index:02}"; trial_folder.mkdir()
                trial = {"index": index, **candidate, "status": "NOT RUN", "matching_header_config_frames": 0, "matching_training_fit_error_mean": None}
                snapshot["trials"].append(trial)
                sps = rate / candidate["symbol_rate"]
                if not 4 <= sps <= 1024:
                    trial["status"] = "NOT RUN: UNSUPPORTED SAMPLE DENSITY"; continue
                output_pcm, output_frames = trial_folder / "decoded.pcm16le", trial_folder / "frames.jsonl"
                code, core, command = invoke(args.core, args.wsl, ["--demod-i8", executable_path(iq_path, args.wsl),
                    str(rate), str(candidate["symbol_rate"]), executable_path(output_pcm, args.wsl),
                    executable_path(output_frames, args.wsl), str(meta["stream_id"])], trial_folder, args.timeout)
                frames = [json.loads(line) for line in output_frames.read_text(encoding="utf-8").splitlines()]
                validate_trial(core, frames, output_pcm, len(payload) // 4, rate, candidate["symbol_rate"], meta["stream_id"])
                trial.update(status="DEMODULATED", exit_code=code, core_result=core, command=command,
                    frames=frames, **trial_score(frames, meta))
            selected = select_trial(snapshot["trials"]); snapshot["selected_trial_index"] = selected
            for trial in snapshot["trials"]:
                trial_csv.append({"run_id": run["run_id"], "snapshot": receipt["file"], "trial": trial["index"],
                    "selected": trial["index"] == selected, **{k: v for k, v in trial.items() if not isinstance(v, (dict, list))}})
            if selected is not None:
                trial = snapshot["trials"][selected]
                samples = pcm(folder / f"trial-{selected:02}" / "decoded.pcm16le")
                annotated, quality = received_quality(trial["frames"], samples, stimulus)
                snapshot.update(demodulation_status="MATCHING HEADERS DECODED", selected_frames=annotated, quality_received_domain=quality)
                clocks = []
                for frame in annotated:
                    if matching_header(frame, meta):
                        symbol_rate = rate / frame["recovered_samples_per_symbol"]
                        effective = symbol_rate * meta["payload_bits"] / (meta["frame_symbols"] * meta["channels"] * meta["oversample"])
                        frame.update(recovered_symbol_rate_hz_from_nominal_RX_clock=symbol_rate,
                                     effective_audio_rate_hz_from_recovered_clock=effective, source_header_audio_rate_hz=meta["source_rate_hz"])
                        clocks.append(effective)
                    frame_csv.append({"run_id": run["run_id"], "snapshot": receipt["file"], "selected_trial": selected, **frame})
                snapshot["effective_audio_rate_hz_from_recovered_clock"] = {"median": statistics.median(clocks), "min": min(clocks), "max": max(clocks)}
                for comparison in ("vs_generated_source", "vs_generated_ideal_boxcar"):
                    for channel, values in enumerate(quality[comparison] or []):
                        quality_csv.append({"run_id": run["run_id"], "snapshot": receipt["file"], "channel": channel, "comparison": comparison, **values})
                (folder / "selected-frames-annotated.jsonl").write_text("".join(json.dumps(frame) + "\n" for frame in annotated), encoding="utf-8")
            by_id[run["run_id"]]["demodulation_status"] = snapshot["demodulation_status"]
        result["status"] = "ANALYSIS COMPLETE"
    except BaseException as error:
        result.update(status="FAILED", error=str(error)); raise
    finally:
        (args.output / "results.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        write_csv(args.output / "trials.csv", trial_csv); write_csv(args.output / "frames.csv", frame_csv); write_csv(args.output / "quality.csv", quality_csv)
        summary = []
        for row in result["runs"]:
            for snap in row["snapshots"] or [{}]:
                unique = snap.get("quality_received_domain", {}).get("unique_known_sequences_BER_counts", {})
                summary.append({"run_id": row["run_id"], "workload": row["workload"], "acquisition_status": row["acquisition_status"],
                    "demodulation_status": snap.get("demodulation_status", row["demodulation_status"]), "source_file": snap.get("source_file"),
                    "selected_trial": snap.get("selected_trial_index"), "unique_known_payload_bits_compared": unique.get("payload_bits_compared"),
                    "unique_known_payload_bit_errors": unique.get("payload_bit_errors"),
                    "effective_audio_rate_hz": snap.get("effective_audio_rate_hz_from_recovered_clock", {}).get("median")})
        write_csv(args.output / "decoded-summary.csv", summary)
        manifest = {"schema": "cyd-sdr-metal-demod-manifest/1", "input_results_sha256": digest(args.results),
            "input_artifact_manifest_sha256": result["capture_artifact_manifest_sha256"], "input_stimulus_manifest_sha256": result["stimulus_manifest_sha256"],
            "core_sha256": result["core_sha256"], "numpy_version": np.__version__,
            "tools_sha256": {n: digest(Path(__file__).with_name(n)) for n in
                ("sdr_metal_demod.py", "sdr_analyze.py", "audio_mvp_simulate.py", "sdr_metal_envelope.py", "sdr_metal_analyze.py")},
            "outputs": {p.relative_to(args.output).as_posix(): digest(p) for p in sorted(args.output.rglob("*")) if p.is_file()}}
        (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path); parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stimulus-manifest", type=Path, required=True); parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--core-sha256", default=CORE_SHA256); parser.add_argument("--wsl", action="store_true")
    parser.add_argument("--unpaced-multipliers", type=float, nargs="+", default=list(DEFAULT_MULTIPLIERS))
    parser.add_argument("--timeout", type=float, default=180)
    return analyze(parser.parse_args())


if __name__ == "__main__": raise SystemExit(main())
