#!/usr/bin/env python3
"""Offline physical-capture figures. Reads JSON only; never opens a serial port.

Missing observations are omitted and listed in the manifest. Histogram upper
bounds are never substituted for exact percentiles or labelled as exact times.
"""
from __future__ import annotations

import argparse
import collections
import datetime as dt
import hashlib
import importlib.metadata
import json
import math
import pathlib
import statistics
import sys
from typing import Any

from summarize import settings_scope


def percentile(values: list[float], percent: int) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * percent / 100) - 1)]


def finite(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def metrics(run: dict[str, Any], name: str) -> list[dict[str, Any]]:
    return [record for record in run.get("target", [])
            if record.get("type") == "metric" and record.get("name") == name and record.get("count", 0) > 0]


class Figures:
    def __init__(self, results: pathlib.Path, output: pathlib.Path, label: str):
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        self.plt = plt
        plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 9,
                             "axes.titlesize": 11, "axes.labelsize": 9,
                             "figure.facecolor": "white", "axes.spines.top": False,
                             "axes.spines.right": False, "svg.fonttype": "none"})
        self.input = results
        self.output = output
        output.mkdir(parents=True, exist_ok=True)
        input_bytes = results.read_bytes()
        self.data = json.loads(input_bytes.decode("utf-8-sig"))
        if self.data.get("schema") != "cyd-perf-capture/1":
            raise ValueError("Expected cyd-perf-capture/1 result JSON")
        if self.data.get("evidence_class") != "physical_measured":
            raise ValueError("These figures require physical_measured capture evidence")
        self.runs = [run for run in self.data.get("runs", []) if run.get("status") == "MEASURED"]
        scopes: dict[str, set[str]] = collections.defaultdict(set)
        for run in self.runs:
            scopes[run["workload"]].add(settings_scope(run))
        self.variant_workloads = {name for name, variants in scopes.items() if len(variants) > 1}
        self.label = label
        self.caption = f"{label} | {self.data.get('status', 'UNKNOWN')} | source {self.data.get('source_sha', 'unknown')[:12]} | firmware {self.data.get('firmware_sha256', 'unknown')[:12]}"
        self.manifest: dict[str, Any] = {
            "schema": "cyd-perf-figures/1", "generated_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "label": label, "input": str(results.resolve()), "input_sha256": hashlib.sha256(input_bytes).hexdigest(),
            "capture_status": self.data.get("status"), "evidence_class": self.data.get("evidence_class"),
            "source_sha": self.data.get("source_sha"), "firmware_sha256": self.data.get("firmware_sha256"),
            "profile": self.data.get("profile"), "measured_runs": len(self.runs), "figures": [], "omitted": [],
            "tool_sha256": hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
            "python": sys.version,
            "analysis_packages": {name: importlib.metadata.version(name) for name in ("matplotlib", "numpy", "pillow", "contourpy", "fonttools", "kiwisolver", "pyparsing", "python-dateutil")},
            "limitations": ["Only completed MEASURED runs are plotted; missing data are not synthesized.",
                            "Repetition ranges use matching requested settings, windows, instrumentation and actual scene/allocation receipts; differing scopes are labelled separately.",
                            "Histogram p99 values are power-of-two upper bounds, distinct from exact observed maxima.",
                            "Per-run FPS uses completed physical presentations divided by the target measurement window.",
                            "CPU utilization is target tick sampling, not cycle-exact utilization.",
                            "Temperature codes come from an undocumented RTC-library function; they are uncalibrated sensor indications, not calibrated die temperatures.",
                            "Primary capture's ESP32_ROM_raw_uncalibrated tag is retained verbatim in raw evidence; its ROM attribution is a legacy inaccurate label."]}

    def workload_label(self, run: dict[str, Any]) -> str:
        name = run["workload"]
        return name + (f" [{settings_scope(run)}]" if name in self.variant_workloads else "")

    @staticmethod
    def timing_axis(axis, values: list[float]) -> None:
        from matplotlib import ticker
        positive = [value for value in values if value > 0]
        logarithmic = bool(positive) and len(positive) == len(values) and max(positive) / min(positive) >= 10
        if logarithmic:
            axis.set_xscale("log")
            axis.xaxis.set_minor_formatter(ticker.NullFormatter())
        else:
            axis.xaxis.set_major_locator(ticker.MaxNLocator(nbins=5))
        axis.set_xlabel("Microseconds" + (" (log scale)" if logarithmic else ""))
        axis.grid(axis="x", alpha=.2)

    def save(self, fig, name: str, title: str, sample_note: str, observations: Any, note: str = "") -> None:
        fig.suptitle(f"{title}\n{self.label}", fontsize=12, fontweight="bold", x=.02, ha="left")
        footer = self.caption + "\n" + sample_note + ("\n" + note if note else "")
        fig.text(.02, .012, footer, fontsize=7, va="bottom", ha="left", color="#384355")
        fig.tight_layout(rect=(0, .10 if note else .08, 1, .90))
        artifacts = []
        for extension in ("png", "svg"):
            path = self.output / f"{name}.{extension}"
            fig.savefig(path, dpi=170, facecolor="white", metadata={"Description": footer})
            artifacts.append({"file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        self.manifest["figures"].append({"name": name, "title": title, "sample_note": sample_note,
                                          "observations": observations, "files": artifacts})
        self.plt.close(fig)

    def fps(self) -> None:
        groups: dict[str, dict[str, list[dict[str, Any]]]] = collections.defaultdict(lambda: collections.defaultdict(list))
        for run in self.runs:
            ends = [record for record in run.get("target", []) if record.get("type") == "end" and record.get("complete") is True]
            if len(ends) != 1:
                continue
            end = ends[0]
            if not finite(end.get("frames")) or not finite(end.get("elapsed_us")) or end["elapsed_us"] <= 0:
                continue
            name = run["workload"]
            category = "audio" if name.startswith(("audio/", "combined/")) else "radio" if name.startswith(("radio-init/", "instrumentation/")) else "scenes"
            groups[category][self.workload_label(run)].append({"run_id": run.get("run_id"), "scope": settings_scope(run), "frames": end["frames"],
                                           "elapsed_us": end["elapsed_us"], "fps": end["frames"] * 1e6 / end["elapsed_us"]})
        if not groups:
            self.manifest["omitted"].append({"figure": "fps", "reason": "No completed physical frame counts and elapsed windows"})
            return
        for category, workloads in groups.items():
            # Page long scene matrices while preserving every measured workload.
            ordered = list(workloads.items())
            for page in range(0, len(ordered), 24):
                rows = ordered[page:page + 24]
                fig, axis = self.plt.subplots(figsize=(12, max(4, len(rows) * .27 + 2)))
                total_frames = total_runs = 0
                for y, (name, observations) in enumerate(rows):
                    values = [item["fps"] for item in observations]
                    middle = statistics.median(values)
                    axis.plot([min(values), max(values)], [y, y], color="#2166ac", linewidth=2.5)
                    axis.scatter(values, [y] * len(values), color="#2166ac", s=19, zorder=3)
                    axis.scatter([middle], [y], marker="|", s=100, color="#172338", zorder=4)
                    total_frames += sum(item["frames"] for item in observations)
                    total_runs += len(observations)
                axis.set_yticks(range(len(rows)), [f"{name}  (r={len(obs)})" for name, obs in rows])
                axis.invert_yaxis()
                axis.set_xlabel("Completed physical presentations / target steady-state second")
                axis.set_xlim(left=0)
                axis.grid(axis="x", alpha=.2)
                axis.set_title("Dots: repetitions; line: observed repetition range; black tick: median", loc="left", fontsize=9)
                suffix = f"-{page // 24 + 1}" if len(ordered) > 24 else ""
                self.save(fig, f"fps-{category}{suffix}", "Actual presentation rate",
                          f"{total_runs} measured repetitions; {total_frames:,} completed presentations; r per row.",
                          dict(rows), "Direct rendering interleaves drawing and SPI. Loop iterations are not frame counts.")

    def audio(self) -> None:
        # Keep representative local, PCM and combined loads readable; exact
        # observations and all omitted workloads remain in the source JSON.
        selected = {}
        for run in self.runs:
            name = run["workload"]
            if (name.startswith(("audio/", "combined/")) or name in {
                "menu/landscape/theme", "visions/landscape/fullscreen/gate-trails", "visions/fullscreen/layered/pcm", "sustained/cpu-both-cores"}):
                selected[(name, settings_scope(run))] = self.workload_label(run)
        definitions = [("audio_callback_us", "Main callback"), ("audio_mix_us", "Mixer"), ("audio_enqueue_us", "Blocking I2S write")]
        rows = []
        count = 0
        for (name, scope), display_label in selected.items():
            item: dict[str, Any] = {"workload": name, "scope": scope, "label": display_label}
            for metric_name, _ in definitions:
                records = [metric for run in self.runs if run["workload"] == name and settings_scope(run) == scope for metric in metrics(run, metric_name)]
                upper = [metric["p99_upper"] for metric in records if metric.get("count", 0) >= 100 and finite(metric.get("p99_upper"))]
                maximum = [metric["max"] for metric in records if finite(metric.get("max"))]
                count += sum(metric.get("count", 0) for metric in records)
                item[metric_name] = {"run_p99_upper_us": upper, "exact_observed_max_us": max(maximum) if maximum else None,
                                     "samples": sum(metric.get("count", 0) for metric in records), "runs": len(records)}
            if any(item[name]["exact_observed_max_us"] is not None for name, _ in definitions):
                rows.append(item)
        if not rows:
            self.manifest["omitted"].append({"figure": "audio-tails", "reason": "No observed callback/mix/enqueue timings"})
            return
        fig, axes = self.plt.subplots(1, 3, figsize=(13, max(4.6, len(rows) * .29 + 2.5)), sharey=True)
        for axis, (metric_name, title) in zip(axes, definitions):
            all_values = []
            for y, row in enumerate(rows):
                metric = row[metric_name]
                upper = metric["run_p99_upper_us"]
                maximum = metric["exact_observed_max_us"]
                if upper:
                    axis.plot([min(upper), max(upper)], [y, y], color="#2166ac", linewidth=2)
                    axis.scatter([max(upper)], [y], marker="^", s=28, color="#2166ac", label="p99 upper bound" if y == 0 else None)
                    all_values.extend(upper)
                if maximum is not None:
                    axis.scatter([maximum], [y], marker="x", s=28, color="#b35806", label="exact observed max" if y == 0 else None)
                    all_values.append(maximum)
            axis.set_title(title)
            self.timing_axis(axis, all_values)
        axes[0].set_yticks(range(len(rows)), [row["label"] for row in rows])
        axes[0].invert_yaxis()
        handles = [self.plt.Line2D([], [], color="#2166ac", marker="^", linestyle="-", label="Largest per-run p99 upper; line spans repetitions"),
                   self.plt.Line2D([], [], color="#b35806", marker="x", linestyle="", label="Exact observed maximum across repetitions")]
        fig.legend(handles=handles, loc="upper right", bbox_to_anchor=(.99, .94), fontsize=7)
        self.save(fig, "audio-tails", "Audio callback, mixing and I2S timing tails",
                  f"{count:,} timing observations across the plotted metrics; per-workload counts in manifest; p99 shown only with >=100 samples/run.", rows,
                  "Triangles are histogram upper bounds and can exceed exact maxima. Blocking I2S time includes waiting; neither duration nor count detects DMA underruns.")

    def kernels(self) -> None:
        grouped: dict[tuple[str, int], list[dict[str, Any]]] = collections.defaultdict(list)
        for run in self.runs:
            for record in run.get("target", []):
                if record.get("type") != "kernel":
                    continue
                samples = record.get("samples_us")
                if not isinstance(samples, list) or not samples or not all(finite(value) and value >= 0 for value in samples):
                    continue
                if record.get("samples") != len(samples):
                    raise ValueError("Kernel samples/count mismatch in " + str(run.get("run_id")))
                key = (record["kernel"], int(record.get("large", 0)))
                if grouped[key] and grouped[key][0]["scope"] != settings_scope(run):
                    raise ValueError("Kernel scope changed across repetitions; plot separate captures rather than merge differing settings: " + str(key))
                grouped[key].append({
                    "run_id": run.get("run_id"), "scope": settings_scope(run), "samples": len(samples), "median_us": percentile(samples, 50),
                    "p99_us": percentile(samples, 99), "precision": record.get("precision"),
                    "output_bytes": record.get("output_bytes"), "working_bytes": record.get("working_bytes")})
        if not grouped:
            self.manifest["omitted"].append({"figure": "kernel-dsp", "reason": "No exact target kernel timing samples"})
            return
        dsp = []
        for family in ("osc", "filter", "modal"):
            for large in (0, 1):
                if any((family + suffix, large) in grouped for suffix in ("-f32", "-q15")):
                    dsp.append((family, large))
        if dsp:
            fig, axes = self.plt.subplots(1, 2, figsize=(12, 5), sharey=True)
            total_samples = 0
            observations = []
            for y, (family, large) in enumerate(dsp):
                for offset, suffix, color in ((-.13, "-f32", "#2166ac"), (.13, "-q15", "#b35806")):
                    records = grouped.get((family + suffix, large), [])
                    if not records:
                        continue
                    total_samples += sum(item["samples"] for item in records)
                    observations.append({"kernel": family + suffix, "large": large, "repetitions": records})
                    for axis, statistic in zip(axes, ("median_us", "p99_us")):
                        values = [record[statistic] for record in records]
                        axis.plot([min(values), max(values)], [y + offset] * 2, color=color, linewidth=2)
                        axis.scatter(values, [y + offset] * len(values), color=color, s=28)
            for axis, title in zip(axes, ("Per-run median", "Per-run p99")):
                axis.set_title(title + " from exact samples")
                self.timing_axis(axis, [record["median_us"] if axis is axes[0] else record["p99_us"] for item in observations for record in item["repetitions"]])
            axes[0].set_yticks(range(len(dsp)), [f"{family}: {'larger' if large else 'representative'}" for family, large in dsp])
            axes[0].invert_yaxis()
            handles = [self.plt.Line2D([], [], color=color, marker="o", label=name) for color, name in (("#2166ac", "float32"), ("#b35806", "fixed point"))]
            axes[1].legend(handles=handles)
            self.save(fig, "kernel-dsp", "Portable DSP kernels on the measured target",
                      f"{total_samples:,} exact timed steps; each dot is a repetition statistic, line spans repetition range.", observations,
                      "Same workload definitions and Q15 output checks; coefficient/state precision differs. RTOS preemption included; initialization/checksum inspection excluded.")
        other = [(key, records) for key, records in grouped.items() if not key[0].startswith(("osc-", "filter-", "modal-"))]
        if other:
            fig, axes = self.plt.subplots(1, 2, figsize=(12, max(4, .3 * len(other) + 2)), sharey=True)
            total = 0
            observations = []
            for y, ((name, large), records) in enumerate(other):
                total += sum(item["samples"] for item in records)
                observations.append({"kernel": name, "large": large, "repetitions": records})
                for axis, statistic in zip(axes, ("median_us", "p99_us")):
                    values = [record[statistic] for record in records]
                    axis.plot([min(values), max(values)], [y, y], color="#2166ac", linewidth=2)
                    axis.scatter(values, [y] * len(values), color="#2166ac", s=25)
            for axis, title in zip(axes, ("Per-run median", "Per-run p99")):
                axis.set_title(title + " from exact samples")
                self.timing_axis(axis, [record["median_us"] if axis is axes[0] else record["p99_us"] for item in observations for record in item["repetitions"]])
            axes[0].set_yticks(range(len(other)), [f"{name}: {'larger' if large else 'representative'}" for (name, large), _ in other])
            axes[0].invert_yaxis()
            self.save(fig, "kernel-other", "Portable graphics and FFT kernels on the measured target",
                      f"{total:,} exact timed steps; dots and lines show repetition statistics/ranges.", observations,
                      "RGB565 strip generation excludes physical SPI transfer. Kernels are scalar references; numeric outputs and data sizes are recorded in capture JSON.")

    def stress(self) -> None:
        stress_runs = [run for run in self.runs if run.get("settings", {}).get("stress_seconds")]
        if not stress_runs:
            self.manifest["omitted"].append({"figure": "stress-temperature", "reason": "No completed retained stress samples"})
            return
        for index, run in enumerate(stress_runs):
            samples = [record for record in run.get("target", []) if record.get("type") == "sample" and finite(record.get("elapsed_ms"))]
            if not samples:
                continue
            ends = [record for record in run.get("target", []) if record.get("type") == "end"]
            warmup = ends[-1].get("warmup_ms", 0) if ends else run.get("warmup_ms_requested", 0)
            fig, axes = self.plt.subplots(2, 1, figsize=(12, 6.4), sharex=True)
            plotted = {"run_id": run.get("run_id"), "target_sample_count": len(samples), "cores": {}, "temperature": []}
            for core, color in ((0, "#2166ac"), (1, "#b35806")):
                pairs = [((sample["elapsed_ms"] - warmup) / 1000, sample[f"core{core}_busy_pct"])
                         for sample in samples if finite(sample.get(f"core{core}_busy_pct"))]
                if pairs:
                    axes[0].plot([p[0] for p in pairs], [p[1] for p in pairs], color=color, linewidth=1, label=f"Core {core}, n={len(pairs)}")
                    plotted["cores"][str(core)] = pairs
            axes[0].set_ylim(0, 102)
            axes[0].set_ylabel("Target busy ticks (%)")
            axes[0].legend(loc="lower left")
            axes[0].grid(alpha=.2)
            pairs = [((sample["elapsed_ms"] - warmup) / 1000, sample["temperature_raw"])
                     for sample in samples if finite(sample.get("temperature_raw"))]
            if pairs:
                axes[1].plot([p[0] for p in pairs], [p[1] for p in pairs], color="#5e3c99", linewidth=1, marker=".", markersize=2)
                plotted["temperature"] = pairs
                axes[1].set_title(f"Undocumented raw sensor indication: n={len(pairs)}, range {min(p[1] for p in pairs):g}–{max(p[1] for p in pairs):g} codes", loc="left", fontsize=9)
            else:
                axes[1].text(.5, .5, "Temperature samples NOT RUN", ha="center", transform=axes[1].transAxes)
            axes[1].set_ylabel("Uncalibrated raw code")
            axes[1].set_xlabel("Target elapsed steady-state seconds (retained sample timestamps)")
            axes[1].grid(alpha=.2)
            suffix = f"-{index + 1}" if len(stress_runs) > 1 else ""
            assessment = run.get("stress_assessment", {})
            self.save(fig, "stress-temperature" + suffix, "Sustained CPU stress and sensor indication",
                      f"{len(samples)} retained target samples; requested {run['settings']['stress_seconds']} s; near-full CPU confirmed by harness: {assessment.get('near_100_percent_confirmed', 'unknown')}.", plotted,
                      "CPU tick sampling is an estimate. Undocumented RTC-library raw sensor codes and Fahrenheit-derived values are uncalibrated; no calibrated die temperature.")

    def finish(self) -> None:
        (self.output / "manifest.json").write_text(json.dumps(self.manifest, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--label", required=True, help="Explicit capture label, e.g. Preflight only or Full campaign 01")
    args = parser.parse_args()
    figures = Figures(args.results, args.output, args.label)
    figures.manifest["command_argv"] = sys.argv
    figures.fps()
    figures.audio()
    figures.kernels()
    figures.stress()
    figures.finish()
    print(json.dumps({"manifest": str((args.output / "manifest.json").resolve()),
                      "figures": len(figures.manifest["figures"]), "omitted": figures.manifest["omitted"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
