#!/usr/bin/env python3
"""Independent fixed-window review of immutable indexed IQ calibration files.

This intentionally does not import the capture parser or primary signal tool.
The retained frame table and erasure table must agree about measured samples.
No serial access, plots, framed decoding or mutation of physical inputs.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
from pathlib import Path
import numpy as np


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fit(time, power, frequency):
    phase = 2 * np.pi * frequency * time
    basis = np.column_stack((np.ones(len(time)), np.cos(phase), np.sin(phase)))
    coefficients, _, _, _ = np.linalg.lstsq(basis, power, rcond=None)
    total = float(np.sum((power - np.mean(power)) ** 2))
    residual = float(np.sum((power - basis @ coefficients) ** 2))
    return {"frequency_hz": frequency, "power_intercept": float(coefficients[0]),
            "power_harmonic_amplitude": float(np.hypot(*coefficients[1:])),
            "r2": 1 - residual / total if total else None}


def analyze(folder, run, hashes):
    names = ["iq-indexed.i8", "iq-frames.csv", "iq-erasures.json", "iq-integrity.json"]
    paths = {name: folder / run["run_id"] / name for name in names}
    checked = {}
    for name, path in paths.items():
        actual = digest(path)
        if hashes[path.relative_to(folder).as_posix()] != actual:
            raise ValueError(f"Closed artifact changed: {path}")
        checked[name] = actual
    integrity = json.loads(paths["iq-integrity.json"].read_text(encoding="utf-8"))
    erasures = json.loads(paths["iq-erasures.json"].read_text(encoding="utf-8"))
    origin = erasures["origin_output_sample_index"]
    components = np.frombuffer(paths["iq-indexed.i8"].read_bytes(), dtype=np.int8).reshape(-1, 2)
    mask = np.zeros(len(components), dtype=bool)
    with paths["iq-frames.csv"].open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            if int(row["bits_per_component"]) != 8:
                raise ValueError("Only native IQS8 is supported")
            start = int(row["output_sample_index"]) - origin
            end = start + int(row["samples"])
            if start < 0 or end > len(mask) or np.any(mask[start:end]):
                raise ValueError("Out-of-range or overlapping frame table")
            mask[start:end] = True
    erasure_mask = np.ones(len(mask), dtype=bool)
    for row in erasures["erasures"]:
        start = row["file_sample_offset"]
        erasure_mask[start:start + row["samples"]] = False
    if not np.array_equal(mask, erasure_mask):
        raise ValueError("Frame and erasure masks disagree")
    if np.count_nonzero(mask) != integrity["valid_samples_in_frames"]:
        raise ValueError("Valid-sample count disagrees with integrity receipt")
    sample_rate = integrity["start"]["output_sample_rate_hz"]
    time = (np.arange(len(mask)) + origin) / sample_rate
    power = np.sum(components.astype(np.float64) ** 2, axis=1)
    steady = mask & (time >= .30) & (time < .60)
    baseline = mask & (time >= 1.) & (time < 1.5)
    if not np.any(steady) or not np.any(baseline):
        raise ValueError("The explicit fixed windows contain no valid samples")
    signal, background = power[steady], power[baseline]
    baud = run["settings"]["baud"]
    threshold = max(10., float(np.percentile(signal, 90)))
    adjacent = steady[1:] & steady[:-1] & (power[1:] > threshold) & (power[:-1] > threshold)
    complex_samples = components[:, 0].astype(float) + 1j * components[:, 1]
    cross = complex_samples[1:][adjacent] * np.conj(complex_samples[:-1][adjacent])
    phasor = np.sum(cross)
    coherence = float(abs(phasor) / np.sum(abs(cross))) if len(cross) else None
    carrier = {"adjacent_valid_high_power_pairs": len(cross), "selection_power_threshold_code_squared": threshold,
               "wire_complex_offset_hz": float(np.angle(phasor) * sample_rate / (2 * np.pi)) if len(cross) else None,
               "weighted_phase_coherence": coherence,
               "coherence_at_least_0_9_and_at_least_50_pairs": bool(len(cross) >= 50 and coherence >= .9)}
    fixed_adjacent = steady[1:] & steady[:-1] & (power[1:] > 500) & (power[:-1] > 500)
    fixed_cross = complex_samples[1:][fixed_adjacent] * np.conj(complex_samples[:-1][fixed_adjacent])
    fixed_phasor = np.sum(fixed_cross)
    fixed_carrier = {"adjacent_valid_high_power_pairs": len(fixed_cross), "selection_power_threshold_code_squared": 500,
        "wire_complex_offset_hz": float(np.angle(fixed_phasor) * sample_rate / (2 * np.pi)) if len(fixed_cross) else None,
        "weighted_phase_coherence": float(abs(fixed_phasor) / np.sum(abs(fixed_cross))) if len(fixed_cross) else None}
    # Widths include only complete contiguous measured runs. Neither a host
    # placeholder nor a real quantized zero is silently reclassified.
    midpoint = (float(np.percentile(signal, 10)) + float(np.percentile(signal, 90))) / 2
    high = steady & (power > max(1., midpoint))
    starts = np.flatnonzero(high & ~np.r_[False, high[:-1]])
    ends = np.flatnonzero(high & ~np.r_[high[1:], False]) + 1
    widths = [(end - start) * 1e6 / sample_rate for start, end in zip(starts, ends)
              if start > 0 and end < len(mask) and steady[start-1] and steady[end]]
    return {"run_id": run["run_id"], "settings": run["settings"], "acquisition_status": run["status"],
            "source_files_sha256": checked, "sample_rate_hz": sample_rate,
            "indexed_samples": len(mask), "valid_samples": int(np.count_nonzero(mask)),
            "excluded_placeholder_samples": int(np.count_nonzero(~mask)),
            "target_queue_drops": integrity["end"].get("drops"), "crc_errors": integrity["crc_errors"],
            "fixed_steady_window_s": [.30, .60], "fixed_baseline_window_s": [1., 1.5],
            "steady_valid_samples": len(signal), "baseline_valid_samples": len(background),
            "steady_mean_power_code_squared": float(np.mean(signal)),
            "baseline_mean_power_code_squared": float(np.mean(background)),
            "steady_to_baseline_power_ratio": float(np.mean(signal) / np.mean(background)) if np.mean(background) else None,
            "steady_zero_fraction": float(np.mean(signal == 0)),
            "clipped_valid_pair_fraction": float(np.mean(np.any((components[mask] == -128) | (components[mask] == 127), axis=1))),
            "power_harmonics": [fit(time[steady], signal, baud * factor) for factor in [.5, 1., 2.]],
            "high_power_adjacent_pair_phase": carrier,
            "fixed_500_power_adjacent_pair_phase": fixed_carrier,
            "complete_high_runs": len(widths),
            "complete_high_run_median_us": float(np.median(widths)) if widths else None,
            "limitations": ["Uncalibrated signed8 power codes, not dBm or transmitter EIRP.",
                "Wire is I,-Q; complex-offset sign is not an absolute RF-frequency calibration.",
                "Fixed windows are declared analysis choices, not synchronized transmitter timestamps.",
                "An alternating calibration contains no framed sync/header; framed no_frames is expected.",
                "No absolute one-way or audio-output latency is inferred."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--captures", nargs="+", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = {"schema": "cyd-sdr-independent-calibration-review/1", "evidence_class": "OFFLINE_ANALYSIS_OF_PHYSICAL_IQ",
              "tool_sha256": digest(Path(__file__)), "captures": []}
    for folder in args.captures:
        receipt_path = folder / "results.json"
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        if receipt["status"] not in ("CAPTURE COMPLETE", "FAILED"):
            raise ValueError("Only closed captures may be analyzed")
        hashes = json.loads((folder / "artifact-hashes.json").read_text(encoding="utf-8"))
        if digest(receipt_path) != hashes["results.json"]:
            raise ValueError("Closed results hash changed")
        runs = [analyze(folder, run, hashes) for run in receipt["runs"]
                if run.get("iq_integrity") and (folder / run["run_id"] / "iq-indexed.i8").exists()]
        result["captures"].append({"capture": folder.as_posix(), "status": receipt["status"],
                                   "results_sha256": digest(receipt_path), "receiver": receipt["plan"]["receiver"], "runs": runs})
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "captures": len(result["captures"]),
                      "runs": sum(len(c["runs"]) for c in result["captures"])}))


if __name__ == "__main__":
    main()
