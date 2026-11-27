#!/usr/bin/env python3
"""Offline native-IQ envelope observations from closed SDR-metal captures."""
from __future__ import annotations

import argparse
import binascii
import csv
from dataclasses import asdict, dataclass
import hashlib
import json
import math
from pathlib import Path
import re

import numpy as np
from sdr_metal_analyze import decode_words


@dataclass(frozen=True)
class Options:
    minimum_power_ratio: float = 4.
    minimum_level_separation: float = 4.
    minimum_ac_peak_fraction: float = .1
    minimum_cycles: float = 3.
    minimum_state_fraction: float = .05
    hysteresis_low_fraction: float = .35
    hysteresis_high_fraction: float = .65

    def validate(self):
        if (not all(math.isfinite(v) for v in asdict(self).values())
            or self.minimum_power_ratio <= 1 or self.minimum_level_separation <= 0
            or not 0 < self.minimum_ac_peak_fraction <= 1 or self.minimum_cycles < 1
            or not 0 < self.minimum_state_fraction < .5
            or not 0 < self.hysteresis_low_fraction < self.hysteresis_high_fraction < 1):
            raise ValueError("Invalid explicit envelope/edge qualification thresholds")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def distribution(values):
    a = np.asarray(values, dtype=np.float64)
    return {"count": len(a), **{key: float(value) if len(a) else None for key, value in (
        ("min", np.min(a) if len(a) else 0), ("median", np.median(a) if len(a) else 0),
        ("p95", np.percentile(a, 95) if len(a) else 0), ("max", np.max(a) if len(a) else 0))}}


def ac_spectrum(power, rate):
    n = len(power)
    empty = {"available": False, "reason": "fewer_than_32_samples", "samples": n,
             "bin_spacing_hz_from_nominal_clock": rate / n, "dc_bin_excluded": True,
             "window": "symmetric_Hann", "peak_frequency_hz_from_nominal_clock": None,
             "peak_bin": None, "peak_fraction_of_nonDC_windowed_AC_power": None}
    if n < 32:
        return empty, np.array([]), np.array([])
    window = np.hanning(n)
    centered = power - np.mean(power)
    psd = np.abs(np.fft.rfft(centered * window)) ** 2 / (rate * np.sum(window ** 2))
    psd[1:-1 if n % 2 == 0 else None] *= 2
    psd[0] = 0.  # DC never serves as a modulation-frequency candidate.
    frequencies = np.fft.rfftfreq(n, 1 / rate)
    total = float(np.sum(psd))
    if total == 0:
        return {**empty, "reason": "constant_power_no_AC_envelope"}, frequencies, psd
    peak = int(np.argmax(psd))
    return {"available": True, "reason": None, "samples": n, "window": "symmetric_Hann",
            "mean_removed_before_window": True, "dc_bin_excluded": True,
            "bin_spacing_hz_from_nominal_clock": rate / n,
            "equivalent_noise_bandwidth_hz_from_nominal_clock": float(rate * np.sum(window ** 2) / np.sum(window) ** 2),
            "peak_bin": peak, "peak_frequency_hz_from_nominal_clock": float(frequencies[peak]),
            "peak_psd_code4_per_hz_from_nominal_clock": float(psd[peak]),
            "peak_fraction_of_nonDC_windowed_AC_power": float(psd[peak] / total),
            "nonDC_windowed_AC_power_code4": total * rate / n,
            "peak_cycles_in_snapshot_from_nominal_clock": float(frequencies[peak] * n / rate)}, frequencies, psd


def hysteresis(power, low, high):
    state, edges = -1, []
    states = np.full(len(power), -1, dtype=np.int8)
    if high <= low:
        return states, edges
    for sample, value in enumerate(power):
        if state == -1:
            if value <= low: state = 0
            elif value >= high: state = 1
        elif state == 0 and value >= high:
            state = 1; edges.append((sample, "rising"))
        elif state == 1 and value <= low:
            state = 0; edges.append((sample, "falling"))
        states[sample] = state
    return states, edges


def expectation_rows(settings, tx, cpu_hz, rate, n, spectrum, psd):
    if not settings.get("tx_enabled") or settings.get("pattern") != "alternating":
        return [], ["No alternating-pattern frequency reference: TX disabled or pattern not declared alternating."]
    warnings = ["Software average spans the TX loop, not a synchronized local RX snapshot; RF overlap is not proven."]
    refs = []
    requested = settings.get("baud")
    if type(requested) in (int, float) and math.isfinite(requested) and requested > 0:
        refs.append(("requested_baud_halved", requested / 2))
    if tx and cpu_hz and tx.get("elapsed_cycles", 0) > 0 and tx.get("processed_bits", 0) > 0:
        refs.append(("software_write_average_halved", tx["processed_bits"] * cpu_hz / tx["elapsed_cycles"] / 2))
    if tx and tx.get("skipped_paced_slots", 0):
        warnings.append("Skipped source slots break a uniform alternating sequence; writes/2 need not be its fundamental.")
    if settings.get("mask_segment_us", 0):
        warnings.append("Interrupt mask segments/gaps can give a local rate different from the whole-loop average.")
    if settings.get("bit_count", 0) % 2:
        warnings.append("Odd alternating pattern length repeats a level at buffer wrap and is not a uniform half-baud square wave.")
    rows = []
    for kind, frequency in refs:
        alias = abs((frequency + rate / 2) % rate - rate / 2)
        nearest = min(int(round(alias * n / rate)), len(psd) - 1) if len(psd) else None
        observed = spectrum.get("peak_frequency_hz_from_nominal_clock")
        rows.append({"kind": kind, "unaliased_reference_hz": frequency,
            "alias_hz_from_nominal_RX_clock": alias, "alias_to_DC": alias == 0,
            "reference_cycles_in_snapshot": alias * n / rate,
            "nearest_bin": nearest, "nearest_bin_frequency_hz_from_nominal_clock": nearest * rate / n if nearest is not None else None,
            "nearest_bin_psd_code4_per_hz": float(psd[nearest]) if nearest is not None else None,
            "observed_peak_minus_reference_alias_hz": observed - alias if observed is not None else None,
            "observed_peak_minus_reference_alias_bins": (observed - alias) * n / rate if observed is not None else None,
            "scope": "Reference only, not an RF-demodulated clock or proof of source origin."})
        if alias * n / rate < 3:
            warnings.append(kind + ": fewer than three aliased reference cycles fit in this snapshot.")
    return rows, warnings


def analyze_payload(payload, nominal_rate, options=Options(), settings=None, tx=None, cpu_hz=None):
    options.validate()
    if nominal_rate not in (16000000, 40000000, 80000000):
        raise ValueError("Unsupported nominal native sample clock")
    i, q, metadata = decode_words(payload)
    if not 1 <= len(i) <= 16380:
        raise ValueError("Native snapshot requires 1..16380 IQ words")
    # Upper metadata never contributes to envelope power. No IQ8 downconversion.
    power = i.astype(np.float64) ** 2 + q.astype(np.float64) ** 2
    quantiles = {f"q{p}": float(np.percentile(power, p)) for p in (1, 5, 10, 25, 50, 75, 90, 95, 99)}
    low, high = quantiles["q10"], quantiles["q90"]
    midpoint = (low + high) / 2
    lower, upper = power[power <= midpoint], power[power > midpoint]
    med_low = float(np.median(lower)) if len(lower) else low
    med_high = float(np.median(upper)) if len(upper) else high
    mad_low = float(np.median(abs(lower - med_low))) if len(lower) else 0.
    mad_high = float(np.median(abs(upper - med_high))) if len(upper) else 0.
    sigma = max(1., 1.4826 * mad_low, 1.4826 * mad_high)
    separation = (med_high - med_low) / sigma
    ratio = high / max(1., low)
    low_threshold = low + options.hysteresis_low_fraction * (high - low)
    high_threshold = low + options.hysteresis_high_fraction * (high - low)
    states, candidates = hysteresis(power, low_threshold, high_threshold)
    spectrum, frequencies, psd = ac_spectrum(power, nominal_rate)
    reasons = []
    if ratio < options.minimum_power_ratio: reasons.append("power_quantile_ratio_below_threshold")
    if separation < options.minimum_level_separation: reasons.append("robust_level_separation_below_threshold")
    if not spectrum["available"]: reasons.append(spectrum["reason"])
    else:
        if spectrum["peak_fraction_of_nonDC_windowed_AC_power"] < options.minimum_ac_peak_fraction:
            reasons.append("AC_spectral_peak_concentration_below_threshold")
        if spectrum["peak_cycles_in_snapshot_from_nominal_clock"] < options.minimum_cycles:
            reasons.append("too_few_AC_peak_cycles_in_snapshot")
    low_fraction, high_fraction = float(np.mean(states == 0)), float(np.mean(states == 1))
    if min(low_fraction, high_fraction) < options.minimum_state_fraction: reasons.append("insufficient_low_or_high_state_occupancy")
    if sum(kind == "rising" for _, kind in candidates) < 3 or sum(kind == "falling" for _, kind in candidates) < 3:
        reasons.append("fewer_than_three_threshold_crossings_each_polarity")
    qualified = not reasons
    edge_rows, intervals = [], {"high_width": [], "low_width": [], "rising_period": [], "falling_period": []}
    if qualified:
        edge_rows = [{"sample_index": sample, "kind": kind, "time_us_from_nominal_clock": sample * 1e6 / nominal_rate,
                      "power_code_squared": float(power[sample])} for sample, kind in candidates]
        for (start, kind), (end, _) in zip(candidates, candidates[1:]):
            intervals["high_width" if kind == "rising" else "low_width"].append(end - start)
        for kind in ("rising", "falling"):
            positions = [sample for sample, value in candidates if value == kind]
            intervals[kind + "_period"] = np.diff(positions).tolist()
    expected, warnings = expectation_rows(settings or {}, tx, cpu_hz, nominal_rate, len(i), spectrum, psd)
    return {"samples": len(i), "nominal_sample_rate_hz": nominal_rate,
        "nominal_horizon_us": len(i) * 1e6 / nominal_rate,
        "components": "native signed I10,-Q10; no Q-sign conversion, no IQ8 requantization",
        "mean_i10": float(np.mean(i)), "mean_native_minus_q10": float(np.mean(q)),
        "metadata12_unique_values": int(len(np.unique(metadata))),
        "signed10_field_limit_fraction": float(np.mean((i == -512) | (i == 511) | (q == -512) | (q == 511))),
        "power_mean_code_squared": float(np.mean(power)), "power_variance_code4": float(np.var(power)),
        "power_quantiles_code_squared": quantiles,
        "q90_q10_ratio_with_one_code_squared_denominator_floor": ratio,
        "q90_q10_normalized_contrast": (high - low) / (high + low) if high + low else 0.,
        "level_medians_code_squared": {"lower": med_low, "upper": med_high},
        "level_robust_sigma_code_squared": sigma, "robust_level_separation_sigma": separation,
        "threshold_scope": "Per-snapshot quantile/MAD heuristics; one code-squared arithmetic floor, not calibrated RF noise or SNR.",
        "midpoint_threshold_code_squared": midpoint,
        "fraction_above_midpoint_descriptive": float(np.mean(power > midpoint)),
        "hysteresis_thresholds_code_squared": {"low": low_threshold, "high": high_threshold},
        "hysteretic_high_fraction_descriptive": high_fraction,
        "hysteretic_unclassified_fraction": float(np.mean(states < 0)),
        "ac_power_spectrum": spectrum,
        "edge_qualification": {"qualified": qualified, "reasons": reasons, "options": asdict(options),
            "minimum_samples_fixed": 32, "minimum_crossings_each_polarity_fixed": 3,
            "candidate_crossings_diagnostic_only": len(candidates)},
        "qualified_threshold_intervals": {kind: {"samples": distribution(values),
            "us_from_nominal_clock": distribution(np.asarray(values) * 1e6 / nominal_rate)} for kind, values in intervals.items()} if qualified else None,
        "qualified_threshold_duty_estimate": high_fraction if qualified else None,
        "edge_scope": "Hysteretic envelope threshold crossings, not calibrated RF edges; first/last censored widths excluded; resolution one nominal sample.",
        "alternating_references": expected, "warnings": warnings,
        "RF_origin_verified": False, "payload_BER_measured": False, "one_way_latency_measured": False}, frequencies, psd, edge_rows


def safe_path(root, name):
    path = (root / name).resolve()
    if Path(name).is_absolute() or not path.is_relative_to(root.resolve()):
        raise ValueError("Artifact path escapes the closed capture directory")
    return path


def load_capture(path):
    result = json.loads(path.read_text(encoding="utf-8-sig"))
    if (result.get("schema") != "cyd-sdr-metal-capture/1" or result.get("evidence_class") != "physical_snapshot_acquisition"
        or result.get("status") not in ("CAPTURE COMPLETE", "FAILED", "INTERRUPTED")):
        raise ValueError("Closed physical SDR-metal capture required; active/unknown status rejected")
    hashes_path = path.parent / "artifact-hashes.json"
    hashes = json.loads(hashes_path.read_text(encoding="utf-8"))
    if hashes.get(path.name) != digest(path): raise ValueError("Closed results hash is absent or changed")
    for name, expected in hashes.items():
        artifact = safe_path(path.parent, name)
        if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected) or not artifact.is_file() or digest(artifact) != expected:
            raise ValueError("Closed artifact hash mismatch: " + name)
    return result, hashes


def verified_snapshots(root, result, hashes):
    for run in result["runs"]:
        if run["status"] != "ACQUIRED": continue
        settings = run["settings"]
        snapshots = run["snapshots"]
        expected_count = 1 if settings["rx_mode"] == "bench" else settings["rx_repeats"]
        if len(snapshots) != expected_count or len({r["file"] for r in snapshots}) != len(snapshots):
            raise ValueError("Acquired run snapshot count/files do not match its finite mode")
        for snapshot_index, receipt in enumerate(snapshots, 1):
            header = receipt["header"]
            path = safe_path(root, run["run_id"] + "/" + receipt["file"])
            payload = path.read_bytes()
            relative = path.relative_to(root.resolve()).as_posix()
            n = header.get("sample_count")
            if (hashes.get(relative) != digest(path) or receipt.get("sha256") != digest(path)
                or receipt.get("crc_ok") is not True or receipt.get("following_lf_ok") is not True
                or type(n) is not int or not 1 <= n <= 16380 or n != settings["samples"]
                or header.get("raw_bytes") != 4 * n or len(payload) != 4 * n
                or header.get("raw_format") != "LE32_I10_Q10_metadata12_native"
                or f"{binascii.crc32(payload) & 0xffffffff:08x}" != header.get("crc32")
                or header.get("source_id") != run["run_id"] or header.get("nominal_rate_hz") != settings["rate_hz"]
                or any(header.get(key) is not True for key in ("complete", "hardware_done", "registers_restored", "tail_canary_ok"))
                or header.get("unchanged_sentinel_words") != 0
                or not header.get("owner_before") or header.get("owner_before") != header.get("owner_after")
                or not header.get("lanes_before") or header.get("lanes_before") != header.get("lanes_after")):
                raise ValueError("Native snapshot identity/hash/CRC/count/canary/owner proof failed: " + relative)
            try:
                status = int(header["status"], 16)
                control = int(header["control"], 16)
            except (KeyError, ValueError, TypeError) as error:
                raise ValueError("Native snapshot lacks raw hardware control/status proof") from error
            rate_bits = {16000000: 1 << 16, 40000000: 1 << 15, 80000000: 0}.get(header["nominal_rate_hz"])
            if (not status & (1 << 18) or not control & (1 << 31) or control & 0x7fff != n
                or rate_bits is None or control & ((1 << 15) | (1 << 16)) != rate_bits):
                raise ValueError("Native snapshot hardware count/clock-selection/completion proof failed")
            expected_rep = settings["rx_repeats"] if settings["rx_mode"] == "bench" else snapshot_index
            if settings["rx_mode"] != "bench" and header.get("repetition") != expected_rep:
                raise ValueError("Native snapshot export repetition is not sequential")
            if type(expected_rep) is not int or not 1 <= expected_rep <= settings["rx_repeats"] or type(header.get("source_repetition")) is not int or header.get("source_repetition") != expected_rep:
                raise ValueError("Native snapshot source repetition differs from its finite run")
            yield run, receipt, path, payload


def write_csv(path, rows, fields=None):
    rows = list(rows)
    fields = fields or list(dict.fromkeys(key for row in rows for key in row)) or ["run_id", "file"]
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields); writer.writeheader(); writer.writerows(rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path); parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--write-spectra", action="store_true"); parser.add_argument("--write-edges", action="store_true")
    for key, value in asdict(Options()).items(): parser.add_argument("--" + key.replace("_", "-"), type=float, default=value)
    args = parser.parse_args(argv)
    options = Options(**{key: getattr(args, key) for key in asdict(Options())}); options.validate()
    if args.output.resolve().is_relative_to(args.results.parent.resolve()):
        raise ValueError("Analysis output must be outside the immutable capture directory")
    result, hashes = load_capture(args.results)
    # Validate every acquired raw receipt before creating any analysis output.
    for _ in verified_snapshots(args.results.parent, result, hashes): pass
    args.output.mkdir(parents=True, exist_ok=False)
    summary = {"schema": "cyd-sdr-metal-envelope/1", "evidence_class": "OFFLINE_ANALYSIS_OF_PHYSICAL_NATIVE_IQ",
        "capture_status": result["status"], "source_results_sha256": digest(args.results), "identities": result.get("identities"),
        "numpy_version": np.__version__, "qualification_options": asdict(options), "runs": [],
        "limitations": ["Each snapshot resets sample index0; no continuous stream, inter-snapshot gap filling or pooling.",
            "Mean-removed I²+Q² AC spectrum describes amplitude modulation, not the complex carrier spectrum.",
            "Frequencies and threshold intervals use nominal RX clock; bin spacing/ENBW are not calibrated frequency accuracy.",
            "Quantile/MAD thresholds are explicit heuristics, not calibrated RF SNR or sensitivity.",
            "Threshold duty is per-snapshot occupancy, not transmitter RF duty or RF on-time.",
            "Interfering carriers can produce envelope beating; periodicity and TX metadata alone do not prove RF origin.",
            "No calibration-pattern BER, framed-payload decoding, audio fidelity or synchronized one-way latency claim."]}
    by_id = {}
    for run in result["runs"]:
        row = {"run_id": run["run_id"], "workload": run.get("workload"), "acquisition_status": run["status"],
               "analysis_status": "ANALYZED" if run["status"] == "ACQUIRED" else "NOT RUN: INCOMPLETE ACQUISITION",
               "settings": run["settings"], "error": run.get("error"), "snapshots": []}
        summary["runs"].append(row); by_id[run["run_id"]] = row
    rows = []
    cpu_mhz = result.get("target_info", {}).get("tx", {}).get("cpu_mhz", 0)
    cpu_hz = cpu_mhz * 1000000 if type(cpu_mhz) is int and 0 < cpu_mhz <= 1000 else None
    for run, receipt, path, payload in verified_snapshots(args.results.parent, result, hashes):
        signal, frequency, psd, edges = analyze_payload(payload, receipt["header"]["nominal_rate_hz"], options,
            run["settings"], run.get("transmitter_result"), cpu_hz)
        by_id[run["run_id"]]["snapshots"].append({"source_file": receipt["file"], "raw_sha256": receipt["sha256"],
            "source_header": receipt["header"], "signal": signal})
        spectrum = signal["ac_power_spectrum"]
        rows.append({"run_id": run["run_id"], "file": receipt["file"], "nominal_rate_hz": receipt["header"]["nominal_rate_hz"],
            "samples": signal["samples"], "raw_sha256": receipt["sha256"], "mean_power_code_squared": signal["power_mean_code_squared"],
            "q90_q10_ratio": signal["q90_q10_ratio_with_one_code_squared_denominator_floor"],
            "q90_q10_normalized_contrast": signal["q90_q10_normalized_contrast"],
            "robust_level_separation_sigma": signal["robust_level_separation_sigma"],
            "AC_peak_hz_nominal_clock": spectrum["peak_frequency_hz_from_nominal_clock"],
            "AC_bin_spacing_hz_nominal_clock": spectrum["bin_spacing_hz_from_nominal_clock"],
            "AC_peak_fraction": spectrum["peak_fraction_of_nonDC_windowed_AC_power"],
            "qualified_threshold_edges": signal["edge_qualification"]["qualified"],
            "threshold_high_occupancy_descriptive": signal["hysteretic_high_fraction_descriptive"],
            "qualified_threshold_duty_estimate": signal["qualified_threshold_duty_estimate"],
            "edge_rejection_reasons": ";".join(signal["edge_qualification"]["reasons"])})
        if args.write_spectra or args.write_edges:
            folder = args.output / run["run_id"]; folder.mkdir(exist_ok=True)
            stem = Path(receipt["file"]).stem
            if args.write_spectra:
                write_csv(folder / (stem + "-power-AC-spectrum.csv"), ({"frequency_hz_from_nominal_clock": float(f),
                    "PSD_code4_per_hz_from_nominal_clock": float(p)} for f, p in zip(frequency, psd)),
                    fields=["frequency_hz_from_nominal_clock", "PSD_code4_per_hz_from_nominal_clock"])
            if args.write_edges: write_csv(folder / (stem + "-qualified-threshold-edges.csv"), edges,
                fields=["sample_index", "kind", "time_us_from_nominal_clock", "power_code_squared"])
    (args.output / "results.json").write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    write_csv(args.output / "envelope-summary.csv", rows)
    manifest = {"schema": "cyd-sdr-metal-envelope-manifest/1", "input_results_sha256": digest(args.results),
        "input_artifact_manifest_sha256": digest(args.results.parent / "artifact-hashes.json"),
        "tool_sha256": digest(Path(__file__)), "native_decoder_sha256": digest(Path(__file__).with_name("sdr_metal_analyze.py")),
        "outputs": {p.relative_to(args.output).as_posix(): digest(p) for p in sorted(args.output.rglob("*")) if p.is_file()}}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "snapshots_analyzed": len(rows), "capture_status": result["status"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
