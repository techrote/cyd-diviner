#!/usr/bin/env python3
"""Offline native-IQ and timing analysis for closed SDR-metal acquisitions."""
from __future__ import annotations

import argparse
import binascii
import csv
import hashlib
import json
import math
import pathlib

import numpy as np


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def decode_words(payload):
    if len(payload) % 4:
        raise ValueError("Native snapshot is not an integral number of LE32 words")
    words = np.frombuffer(payload, dtype="<u4")
    i = (words & 1023).astype(np.int16)
    q = ((words >> 10) & 1023).astype(np.int16)
    i[i >= 512] -= 1024
    q[q >= 512] -= 1024
    return i, q, words >> 20


def iq_statistics(payload, nominal_rate):
    i, q, metadata = decode_words(payload)
    if not len(i) or nominal_rate not in (16000000, 40000000, 80000000):
        raise ValueError("Nonempty native IQ and a supported nominal clock required")
    z = i.astype(np.float64) + 1j * q.astype(np.float64)
    power = np.abs(z) ** 2
    lag = np.sum(z[1:] * np.conj(z[:-1]))
    denominator = math.sqrt(float(np.sum(np.abs(z[1:]) ** 2) * np.sum(np.abs(z[:-1]) ** 2))) if len(z) > 1 else 0
    spectrum = np.abs(np.fft.fft((z - np.mean(z)) * np.hanning(len(z)))) ** 2
    frequencies = np.fft.fftfreq(len(z), 1 / nominal_rate)
    peak = int(np.argmax(spectrum))
    return {"samples": len(i), "native_components": "signed I10,-Q10; upper12 metadata retained; no Q-sign conversion",
        "power_mean_code_squared": float(np.mean(power)), "power_p50": float(np.percentile(power, 50)),
        "power_p95": float(np.percentile(power, 95)), "power_max": float(np.max(power)),
        "complex_rms_code": math.sqrt(float(np.mean(power))), "mean_i": float(np.mean(i)), "mean_native_minus_q": float(np.mean(q)),
        "at_signed10_field_limit_fraction": float(np.mean((i == -512) | (i == 511) | (q == -512) | (q == 511))),
        "metadata12_unique_values": int(len(np.unique(metadata))), "metadata12_min": int(np.min(metadata)), "metadata12_max": int(np.max(metadata)),
        "lag1_complex_coherence": abs(lag) / denominator if denominator else None,
        "lag1_phase_frequency_from_nominal_clock_hz": float(np.angle(lag) * nominal_rate / (2 * np.pi)) if denominator else None,
        "hann_ac_peak_frequency_from_nominal_clock_hz": float(frequencies[peak]) if np.sum(spectrum) else None,
        "hann_ac_peak_power_fraction": float(spectrum[peak] / np.sum(spectrum)) if np.sum(spectrum) else None,
        "frequency_bin_spacing_from_nominal_clock_hz": nominal_rate / len(i),
        "scope": "one separate native snapshot; code values and frequencies derived from configured native clock, not calibrated RF power/rate"}


def rx_rates(end):
    loop, observed = end.get("loop_elapsed_us"), end.get("capture_observation_us_sum")
    captures, samples, rate = end.get("captures_completed"), end.get("sample_count_each"), end.get("nominal_rate_hz")
    if any(type(value) is not int or value < 0 for value in (loop, observed, captures, samples, rate)) or not rate:
        raise ValueError("Invalid RX completion timing/count fields")
    nominal = captures * samples * 1e6 / rate
    return {"mode": end.get("mode"), "completed_snapshots": captures, "native_samples_total": captures * samples,
        "nominal_sample_horizon_sum_us": nominal, "observed_trigger_to_completion_sum_us": observed,
        "observed_loop_us": loop, "observed_loop_non_capture_us": loop - observed,
        "nominal_horizon_to_loop_ratio": nominal / loop if loop else None,
        "observed_capture_to_loop_ratio": observed / loop if loop else None,
        "native_samples_per_loop_second": captures * samples * 1e6 / loop if loop else None,
        "native_samples_per_observed_capture_second": captures * samples * 1e6 / observed if observed else None,
        "scope": "bench excludes per-capture CRC/format/USB; capture includes deferred USB plus setup/check/gap costs. Ratios are not continuous RF duty or calibrated clock proof."}


def write_csv(path, rows):
    fields = list(dict.fromkeys(key for row in rows for key in row)) or ["run_id", "status"]
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=pathlib.Path); parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    result = json.loads(args.results.read_text(encoding="utf-8-sig"))
    if result.get("schema") != "cyd-sdr-metal-capture/1" or result.get("evidence_class") != "physical_snapshot_acquisition" or result.get("status") == "IN PROGRESS":
        raise ValueError("Closed physical metal acquisition required")
    hashes = json.loads((args.results.parent / "artifact-hashes.json").read_text())
    for name, expected in hashes.items():
        path = args.results.parent / name
        if not path.is_file() or digest(path) != expected: raise ValueError("Original metal artifact hash mismatch: " + name)
    args.output.mkdir(parents=True, exist_ok=False)
    summary = {"schema": "cyd-sdr-metal-analysis/1", "evidence_class": "PHYSICAL_NATIVE_SNAPSHOT_ANALYSIS",
        "capture_status": result["status"], "identities": result["identities"], "source_results_sha256": digest(args.results),
        "runs": [], "limitations": ["Every burst resets sample index0. Separate snapshots and requested inter-burst gaps are not a continuous IQ stream.",
            "Native-clock arithmetic horizons differ from CPU-observed trigger/completion intervals and whole-loop elapsed time.",
            "No-export bench and USB-export capture have different observer/deadtime scopes and are not pooled.",
            "TX achieved writes/s and mask cycle brackets measure software execution, not independent RF edges or ultimate modulation bandwidth.",
            "TX late_cycles/late_cycles_max refer to the pending deadline before obsolete slots are skipped; they measure backlog/missed deadlines, not final output-symbol jitter.",
            "IQ statistics are receiver codes; spectral frequencies assume the configured native clock and are not calibrated RF frequency or power.",
            "No on-board audio reconstruction, analog-output proof or synchronized end-to-end latency claim."]}
    rates_rows, iq_rows = [], []
    provenance = {name + "_" + key: value for name, entry in result["identities"].items()
                  for key, value in {"profile": entry.get("expected_info", {}).get("profile"),
                    "source_sha": entry.get("expected_info", {}).get("source_sha"), "firmware_sha256": entry.get("firmware_sha256")}.items()}
    for run in result["runs"]:
        row = {"run_id": run["run_id"], "workload": run["workload"], "acquisition_status": run["status"],
               "settings": run["settings"], "error": run.get("error"), "analysis_status": "NOT RUN: INCOMPLETE ACQUISITION"}
        row["temperature_before"], row["temperature_after"] = run.get("temperature_before"), run.get("temperature_after")
        summary["runs"].append(row)
        if run["status"] != "ACQUIRED": continue
        row["rx_timing"] = rx_rates(run["receiver_run_end"])
        row["receiver_run_end"] = run["receiver_run_end"]
        tx = run.get("transmitter_result")
        if tx:
            row["transmitter_result"] = tx
            cycles, writes = tx.get("elapsed_cycles", 0), tx.get("processed_bits", 0)
            cpu = result.get("target_info", {}).get("tx", {}).get("cpu_mhz", 0) * 1000000
            row["software_tx_rates"] = {"writes": writes, "elapsed_cycles_mod32": cycles,
                "writes_per_s_from_reported_CPU_clock": writes * cpu / cycles if cycles and cpu else None,
                "mean_loop_cycles_per_write": cycles / writes if writes else None,
                "requested_baud": tx.get("requested_baud"), "unpaced": tx.get("unpaced"),
                "loop_variant": tx.get("loop_variant"), "trace_stride": tx.get("trace_stride"),
                "scope": "Includes IRAM loop/control/observer/IRQ costs; not a single-store latency or measured RF edge rate"}
        rate_row = {"run_id": run["run_id"], "workload": run["workload"], "status": run["status"], **provenance, **row["rx_timing"]}
        if tx: rate_row.update({"tx_" + key: value for key, value in row["software_tx_rates"].items()})
        if tx: rate_row.update({"tx_raw_" + key: value for key, value in tx.items() if not isinstance(value, (list, dict))})
        rates_rows.append(rate_row)
        row["snapshots"] = []
        for receipt in run["snapshots"]:
            header = receipt["header"]
            path = args.results.parent / run["run_id"] / receipt["file"]
            name = path.relative_to(args.results.parent).as_posix()
            payload = path.read_bytes()
            if (hashes.get(name) != digest(path) or receipt.get("sha256") != digest(path)
                or len(payload) != header["raw_bytes"] or f"{binascii.crc32(payload) & 0xffffffff:08x}" != header["crc32"]):
                raise ValueError("Native snapshot hash/size/CRC differs from acquisition")
            signal = iq_statistics(payload, header["nominal_rate_hz"])
            row["snapshots"].append({"source": receipt, "signal": signal})
            iq_rows.append({"run_id": run["run_id"], "file": receipt["file"], **provenance,
                **{"raw_" + key: value for key, value in header.items() if not isinstance(value, (list, dict))},
                "crc_ok": receipt["crc_ok"], "following_lf_ok": receipt["following_lf_ok"], "sha256": receipt["sha256"], "source_id": header["source_id"],
                "source_repetition": header["source_repetition"], "nominal_rate_hz": header["nominal_rate_hz"],
                "trigger_before_us": header["trigger_before_us"], "done_observed_us": header["done_observed_us"],
                "capture_cycles_mod32": header["capture_cycles_mod32"], "tail_canary_ok": header["tail_canary_ok"],
                "registers_restored": header["registers_restored"], **signal})
        row["analysis_status"] = "ANALYZED"
    (args.output / "results.json").write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    write_csv(args.output / "rates.csv", rates_rows); write_csv(args.output / "native-iq.csv", iq_rows)
    lines = ["# Native snapshot and software-loop observations", "", f"Original capture status **{result['status']}** remains unchanged.", "",
        "| Run | mode | snapshots | nominal horizon sum µs | observed capture sum µs | loop µs | samples/loop second |",
        "| --- | --- | --- | --- | --- | --- | --- |"]
    for row in rates_rows:
        rate_text = "—" if row["native_samples_per_loop_second"] is None else f"{row['native_samples_per_loop_second']:.1f}"
        lines.append(f"| {row['run_id']} | {row['mode']} | {row['completed_snapshots']} | {row['nominal_sample_horizon_sum_us']:.3f} | "
                     f"{row['observed_trigger_to_completion_sum_us']} | {row['observed_loop_us']} | {rate_text} |")
    lines += ["", *["- " + text for text in summary["limitations"]], "",
        "Exact MMIO/backend/PHY-init mode, observer stride, mask/gap cycles, register words, heap/sensor values, CRC and canary/restoration records remain in structured JSON/CSV. "
        "API/void-call completion alone does not prove physical RF cessation or edge shape.", ""]
    (args.output / "METAL-REPORT.md").write_text("\n".join(lines), encoding="utf-8")
    manifest = {"schema": "cyd-sdr-metal-analysis-manifest/1", "input_results_sha256": digest(args.results),
        "tool_sha256": digest(pathlib.Path(__file__)),
        "outputs": {path.name: digest(path) for path in args.output.iterdir() if path.is_file()}}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
