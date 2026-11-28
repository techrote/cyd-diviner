#!/usr/bin/env python3
"""Valid-IQ-only envelope/periodicity analysis of closed physical calibration."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import pathlib

import numpy as np

from sdr_capture import IQSDecoder


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_valid_iq(path):
    indices, components = [], []
    last_end = None
    def receive(row, payload):
        nonlocal last_end
        if row["bits_per_component"] != 8:
            raise ValueError("Signal analyzer requires native IQS8; no implicit quantization")
        values = np.frombuffer(payload, dtype=np.int8).reshape(-1, 2)
        start = row["output_sample_index"]
        overlap = min(row["samples"], max(0, (last_end or start) - start))
        if overlap < len(values):
            indices.append(np.arange(start + overlap, start + len(values), dtype=np.int64))
            components.append(values[overlap:].copy())
        last_end = max(last_end or 0, start + len(values))
    decoder = IQSDecoder(receive)
    with path.open("rb") as stream:
        while block := stream.read(65536): decoder.feed(block)
    if not components:
        raise ValueError("No CRC-valid IQ samples")
    return np.concatenate(indices), np.concatenate(components), decoder.summary()


def describe(values):
    return {"n": len(values), "mean": float(np.mean(values)), "p50": float(np.percentile(values, 50)),
            "p95": float(np.percentile(values, 95)), "p99": float(np.percentile(values, 99)), "max": float(np.max(values))}


def sinusoid_fit(time, envelope, frequency):
    if len(envelope) < 8 or frequency <= 0:
        return {"frequency_hz": frequency, "r2": None, "fitted_power_amplitude": None}
    phase = 2 * np.pi * frequency * (time - time[0])
    basis = np.column_stack((np.ones(len(time)), np.sin(phase), np.cos(phase)))
    coefficients, _, _, _ = np.linalg.lstsq(basis, envelope, rcond=None)
    residual = envelope - basis @ coefficients
    variance = float(np.sum((envelope - np.mean(envelope)) ** 2))
    return {"frequency_hz": float(frequency), "r2": max(0., min(1., 1 - float(np.sum(residual ** 2)) / variance)) if variance else None,
            "fitted_power_amplitude": float(np.hypot(coefficients[1], coefficients[2])),
            "fitted_power_intercept": float(coefficients[0])}


def analyze_signal(indices, components, sample_rate, baud, baseline_ms=100, window_ms=50):
    if not len(indices) or components.shape != (len(indices), 2) or sample_rate <= 0 or baud <= 0:
        raise ValueError("Nonempty indexed IQ8 and positive measured rate/requested baud required")
    time = indices / sample_rate
    power = np.sum(components.astype(np.float64) ** 2, axis=1)
    clipped_components = (components <= -128) | (components >= 127)
    baseline_mask = (indices - indices[0]) < sample_rate * baseline_ms / 1000
    baseline = power[baseline_mask]
    if not len(baseline): raise ValueError("No valid baseline IQ")
    baseline_mean = float(np.mean(baseline))
    threshold = float(np.percentile(baseline, 99.9)) * 16
    high = power > threshold
    bins = np.floor((indices - indices[0]) / (sample_rate * window_ms / 1000)).astype(np.int64)
    windows = []
    frequency = baud / 2
    for number in np.unique(bins):
        mask = bins == number
        n = int(np.sum(mask))
        fit = sinusoid_fit(time[mask], power[mask], frequency)
        windows.append({"window": int(number), "start_output_sample": int(indices[0] + number * sample_rate * window_ms / 1000),
            "time_start_s": float(indices[0] / sample_rate + number * window_ms / 1000), "duration_ms": window_ms,
            "valid_samples": n, "expected_samples": sample_rate * window_ms / 1000,
            "valid_fraction": n / (sample_rate * window_ms / 1000), "power_mean": float(np.mean(power[mask])),
            "power_p95": float(np.percentile(power[mask], 95)), "power_max": float(np.max(power[mask])),
            "clipped_IQ_pair_fraction": float(np.mean(np.any(clipped_components[mask], axis=1))), **fit})
    eligible = [row for row in windows if row["valid_fraction"] >= .25 and row["r2"] is not None]
    peak = max(windows, key=lambda row: row["power_mean"])
    best = max(eligible, key=lambda row: row["r2"]) if eligible else None
    search, folded = [], []
    if best:
        mask = bins == best["window"]
        for candidate in np.linspace(frequency * .8, frequency * 1.2, 81):
            search.append(sinusoid_fit(time[mask], power[mask], float(candidate)))
        phase = np.floor((time[mask] * frequency % 1) * 32).astype(int)
        selected = power[mask]
        for index in range(32):
            values = selected[phase == index]
            folded.append({"phase_bin": index, "n": len(values), "mean_power": float(np.mean(values)) if len(values) else None})
    result = {"valid_IQ_samples": len(indices), "first_output_sample_index": int(indices[0]), "last_output_sample_index": int(indices[-1]),
        "measured_output_sample_rate_hz": sample_rate, "requested_baud": baud, "alternating_expected_frequency_hz": frequency,
        "valid_sample_exposure_s": len(indices) / sample_rate,
        "output_index_span_s": (int(indices[-1]) - int(indices[0]) + 1) / sample_rate,
        "baseline": {"scope": "first requested baseline_ms in receiver output-index time, valid samples only",
            "duration_ms": baseline_ms, "power": describe(baseline), "complex_rms_code": math.sqrt(baseline_mean)},
        "all_valid_power": describe(power), "clipped_component_fraction": float(np.mean(clipped_components)),
        "clipped_IQ_pair_fraction": float(np.mean(np.any(clipped_components, axis=1))),
        "peak_window": peak, "peak_window_mean_power_to_baseline_ratio": peak["power_mean"] / baseline_mean if baseline_mean else None,
        "threshold_method": "16 times observed baseline power p99.9; descriptive threshold, no formal detection significance",
        "threshold_power_code_squared": threshold, "above_threshold_valid_samples": int(np.sum(high)),
        "above_threshold_exposure_s": int(np.sum(high)) / sample_rate,
        "above_threshold_first_s": float(time[high][0]) if np.any(high) else None,
        "above_threshold_last_s": float(time[high][-1]) if np.any(high) else None,
        "best_expected_frequency_window": best,
        "best_scanned_frequency_in_selected_window": max(search, key=lambda row: row["r2"] if row["r2"] is not None else -1) if search else None,
        "periodicity_method": "OLS intercept+sin+cos of power envelope at requested baud/2 on exact valid sample times; selected maximum over windows, not a hypothesis test",
        "frequency_search_method": "81-point grid from0.8..1.2 times requested baud/2 within selected window; no assertion that a weak fit is a real tone",
        "window_ms": window_ms, "baseline_ms": baseline_ms}
    return result, windows, search, folded


def write_csv(path, rows):
    fields = list(dict.fromkeys(key for row in rows for key in row)) or ["status"]
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--window-ms", type=int, default=50)
    args = parser.parse_args()
    capture = json.loads(args.results.read_text())
    if capture.get("schema") != "cyd-sdr-capture/1" or capture.get("evidence_class") != "physical_iq_acquisition" or capture.get("status") == "IN PROGRESS":
        raise ValueError("Closed physical IQ capture required")
    hashes = json.loads((args.results.parent / "artifact-hashes.json").read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    result = {"schema": "cyd-sdr-calibration-signals/1", "evidence_class": "PHYSICAL_IQ_SIGNAL_ANALYSIS",
        "capture_status": capture["status"], "source_results_sha256": digest(args.results), "identities": capture["identities"], "runs": [],
        "limitations": ["Only CRC-valid native IQ8 samples are used; no host zero-filled IQ erasure placeholders enter statistics.",
            "IQ magnitudes/powers are quantized receiver codes at the recorded fixed gain, not calibrated RF power.",
            "Envelope contrast and fitted periodicity are observations, not proprietary packet/header/audio decode proof.",
            "Selected maximum-window and frequency fits are descriptive and not formal detection significance.",
            "TX timestamps are software call timings on another unsynchronized clock, not RF edge or end-to-end latency measurements."]}
    rows = []
    for run in capture["runs"]:
        path = args.results.parent / run["run_id"] / "iq-stream.bin"
        if not path.exists(): continue
        if digest(path) != hashes.get(path.relative_to(args.results.parent).as_posix()):
            raise ValueError("IQ input hash differs from closed capture")
        indices, components, integrity = load_valid_iq(path)
        sample_rate = integrity["start"]["output_sample_rate_hz"]
        metrics, windows, search, folded = analyze_signal(indices, components, sample_rate, run["settings"]["baud"],
            capture["plan"]["receiver"].get("baseline_ms", 100), args.window_ms)
        folder = args.output / run["run_id"]
        folder.mkdir()
        write_csv(folder / "windows.csv", windows)
        write_csv(folder / "frequency-scan.csv", search)
        write_csv(folder / "folded-envelope.csv", folded)
        result["runs"].append({"run_id": run["run_id"], "workload": run["workload"], "acquisition_status": run["status"],
            "settings": run["settings"], "tx_result": run.get("tx_result"), "receiver_gain": run.get("receiver_gain_readback"),
            "integrity": integrity, "signal": metrics})
        rows.append({"run_id": run["run_id"], "workload": run["workload"], "acquisition_status": run["status"],
            "valid_samples": len(indices), "valid_exposure_s": metrics["valid_sample_exposure_s"],
            "baseline_mean_power": metrics["baseline"]["power"]["mean"], "peak_window_mean_power": metrics["peak_window"]["power_mean"],
            "peak_to_baseline_power_ratio": metrics["peak_window_mean_power_to_baseline_ratio"],
            "clip_fraction": metrics["clipped_IQ_pair_fraction"], "above_threshold_samples": metrics["above_threshold_valid_samples"],
            "best_expected_frequency_r2": (metrics["best_expected_frequency_window"] or {}).get("r2"),
            "scanned_best_frequency_hz": (metrics["best_scanned_frequency_in_selected_window"] or {}).get("frequency_hz"),
            "scanned_best_r2": (metrics["best_scanned_frequency_in_selected_window"] or {}).get("r2")})
    (args.output / "results.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    write_csv(args.output / "signal-summary.csv", rows)
    output_hashes = {path.relative_to(args.output).as_posix(): digest(path) for path in sorted(args.output.rglob("*")) if path.is_file()}
    (args.output / "manifest.json").write_text(json.dumps({"schema": "cyd-sdr-signal-analysis-manifest/1",
        "input_results_sha256": digest(args.results), "tool_sha256": digest(pathlib.Path(__file__)),
        "iq_parser_sha256": digest(pathlib.Path(__file__).with_name("sdr_capture.py")), "outputs": output_hashes}, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
