#!/usr/bin/env python3
"""Write an editable report from observed physical capture records, entirely offline."""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import pathlib
import re

from capture import stress_assessment
from summarize import settings_scope, summarize


def fmt(value, digits=2):
    return "—" if value is None else f"{value:.{digits}f}" if isinstance(value, float) else str(value)


def esc(value):
    return str(value).replace("|", "\\|").replace("\n", " ")


def table(headers, rows):
    if not rows:
        return "NOT RUN / no completed observations in this capture.\n"
    return "\n".join(["| " + " | ".join(headers) + " |", "| " + " | ".join("---" for _ in headers) + " |",
                      *["| " + " | ".join(esc(value) for value in row) + " |" for row in rows]]) + "\n"


def number(record, field, divisor=1, digits=2):
    value = record.get(field) if record else None
    if value is not None and field.endswith("_upper") and record.get("max") is not None:
        value = min(value, record["max"])
    return fmt(value / divisor if value is not None else None, digits)


def clean_receipt(record):
    return {key: value for key, value in record.items()
            if key not in ("schema", "type", "host_received_elapsed_s", "run_id")}


def generate_kernel_report(result: dict, label: str, input_sha256: str, summary: dict, plots: list[str]) -> str:
    measured = [run for run in result["runs"] if run["status"] == "MEASURED"]
    info = next((record for record in result.get("target_info", []) if record.get("type") == "info"), {})
    board, plan = result.get("board", {}), result.get("plan", {})
    registered = sum(case.get("repetitions", plan.get("repetitions", 3)) for case in plan.get("workloads", []) if case.get("status") != "NOT RUN")
    failed = result["status"] != "CAPTURE COMPLETE"
    text = [f"# Portable kernel evidence — {label}", "",
        f"Capture status: **{result['status']}**. Valid completed cases: **{len(measured)}/{registered} registered repetitions**. "
        "Only MEASURED cases are summarized; partial target records remain raw evidence.", ""]
    if failed:
        text += ["This historical portable harness registered its workload plan incrementally. The failed snapshot lists cases reached, "
                 "not every remaining kernel/size/memory-region combination. Its registered denominator is not a completed full-matrix claim.", "",
                 f"Recorded failure: `{esc(result.get('error', 'UNSPECIFIED'))}`.", ""]
    text += [f"Board: {board.get('board', 'UNKNOWN')}; profile `{result['profile']}`. "
             f"Source [{result['source_sha']}](https://github.com/techrote/cyd-diviner/commit/{result['source_sha']}); "
             f"firmware SHA256 `{result['firmware_sha256']}`; input results SHA256 `{input_sha256}`.", "",
        f"CPU {info.get('cpu_mhz', 'UNKNOWN')} MHz; flash {info.get('flash_bytes', 'UNKNOWN')} bytes at {info.get('flash_hz', 'UNKNOWN')} Hz; "
        f"initialized PSRAM heap {info.get('psram_bytes', 'UNKNOWN')} bytes. Nominal package PSRAM: "
        f"{board.get('psram_physical_package_bytes', 'NOT SEPARATELY RECORDED')} bytes. "
        f"Standalone: `{info.get('standalone', False)}`; ordinary audio/display worker flags: "
        f"`{info.get('ordinary_audio_worker', 'profiling context')}` / `{info.get('ordinary_display_worker', 'profiling context')}`.", "",
        "The kernel suite uses three warmup steps, reinitializes from the fixed seed, then records 256 exact step timings per case. "
        "Allocation, preparation, warmup, yields and output inspection are excluded; RTOS preemption during a step is included. "
        "Percentiles below pool exact timings across matching completed repetitions. Different memory-region/settings scopes remain separate. "
        "A kernel timing sample is not a physical display frame or audio deadline guarantee.", "",
        "## Measured kernels", ""]
    rows = []
    for row in summary["kernels"]:
        matching = [run for run in measured if run["workload"] == row["workload"] and settings_scope(run) == row["scope"]]
        first = matching[0]
        region = first["settings"].get("memory_region", "NOT RECORDED")
        method = first["settings"].get("memory_region_method", "NOT RECORDED")
        rows.append([row["kernel"], "large" if row["large"] else "realistic", f"{region} ({method})", row["repetitions"], row["samples"],
                     row["median_us"], row["p95_us"], row["p99_us"], row["max_us"], row["working_bytes"], row["output_bytes"],
                     row["checksum_consistent_across_repetitions"]])
    text += [table(["Kernel", "size", "memory scope", "reps", "n", "median µs", "p95 µs", "p99 µs", "max µs", "working B", "output B", "fingerprint stable"], rows),
        "Working bytes are active algorithm state; the target separately reports the complete allocated workspace. "
        "The same allocation capabilities and data sizes must be used for a cross-board comparison. The classic CYD profiling image has "
        "concurrent audio/display tasks and a 24,008-byte static observer-array footprint; a standalone S3 image has a different context. "
        "Ordinary firmware memory headroom cannot be recovered by subtracting that footprint alone. "
        "The repository's `evidence/method-notes.md` and `tools/perf/KERNELS.md` document context, precision and fingerprint semantics.", "",
        "FFT's legacy `checksum_fnv1a` field composes separate real/imaginary fingerprints with XOR. "
        "Float bit fingerprints may differ across architectures; numerical error and host double-precision comparisons are separate correctness evidence. "
        "Stable repeated fingerprints do not by themselves establish equivalent float/fixed-point precision.", "",
        "## Sensor and allocation snapshots", ""]
    sensors = [record for run in measured for record in run.get("target", []) if record.get("type") == "state"]
    sensor_rows = []
    for field in ("temperature_raw", "temperature_c_indication", "heap_internal_free_bytes", "heap_internal_largest_bytes", "heap_psram_free_bytes", "heap_psram_largest_bytes"):
        values = [record[field] for record in sensors if isinstance(record.get(field), (int, float))]
        if values:
            sensor_rows.append([field, len(values), fmt(min(values), 3), fmt(max(values), 3)])
    text += [table(["Field", "n", "minimum", "maximum"], sensor_rows),
        f"Recorded sensor method: `{info.get('temperature_method', 'UNKNOWN')}`. "
        + ("The classic ESP32 raw RTC-library indication and its legacy ROM tag are undocumented/uncalibrated; they do not establish accurate die temperature. "
           if "ESP32_ROM" in info.get("temperature_method", "") or "RTC" in info.get("temperature_method", "") else
           "The S3 driver Celsius indication has not been independently checked against an external temperature reference. ")
        + "Snapshot ranges are outside kernel timing windows. Neither these snapshots nor the numeric conversion establish a sustained stress or calibrated cross-chip thermal comparison.", "",
        "## Bounds and remaining work", "",
        "Only the listed board/profile, kernel sizes, memory regions and finite repetitions were measured. "
        "This capture establishes no C3 physical performance, local DAC/display cadence, direct driver-underrun count or wireless throughput/RTT. "
        "Unlisted combinations in a failed incremental plan remain unmeasured. Preserve the failed stream and protocol error; do not infer timings from truncated JSON.", "",
        "Compute time per output byte in [summary-kernels.csv](summary-kernels.csv) nominates offload experiments. "
        "It excludes required input/state transfers, serialization, measured link tails and remote scheduling. "
        "A speedup requires `local compute > remote compute + RTT + (input+output bytes)/goodput` in consistent units; no wireless benefit is asserted by these kernels alone.", "",
        "Artifacts: [results.json](results.json), [target records](target.jsonl), [serial RX](serial-rx.bin), "
        "[serial events](serial-events.jsonl), [samples.csv](samples.csv), [summary.json](summary.json), "
        "[summary kernels](summary-kernels.csv), [artifact checksums](artifact-hashes.json).", ""]
    if plots:
        text += ["## Plots", ""]
        for plot in plots:
            text += [f"![{pathlib.Path(plot).stem}]({plot})", ""]
    return "\n".join(text).rstrip() + "\n"


def generate(result: dict, label: str, input_sha256: str, plots: list[str] | None = None) -> str:
    summary = summarize(result)
    if result.get("capture_kind") == "portable_kernel_matrix":
        return generate_kernel_report(result, label, input_sha256, summary, plots or [])
    measured = [run for run in result["runs"] if run["status"] == "MEASURED"]
    grouped = collections.defaultdict(list)
    for run in measured:
        grouped[(run["workload"], settings_scope(run))].append(run)
    metric_index = {(row["workload"], row["scope"], row["name"]): row for row in summary["metrics"]}
    scene_index = {(row["workload"], row["scope"]): row for row in summary["scenes"]}
    info = next((record for record in result.get("target_info", []) if record.get("type") == "info"), {})
    legacy_sensor_note = ("The approved primary a4549f8 image's legacy tag says ROM, while its linked implementation resolves through SDK `librtc.a` / `rtc_analog.o`; the tag does not establish a mask-ROM implementation. "
                          if "ESP32_ROM" in info.get("temperature_method", "") else "")
    display = next((record for record in result.get("target_info", []) if record.get("type") == "display_settings"), {})
    board = result.get("board", {})
    plan = result.get("plan", {})
    expected = sum(case.get("repetitions", plan.get("repetitions", 3)) for case in plan.get("workloads", [])
                   if case.get("status") != "NOT RUN")
    missing_cases = [case for case in plan.get("workloads", []) if case.get("status") == "NOT RUN"]
    pending = max(0, expected - len(measured))
    is_preflight = "preflight" in label.lower()
    title = f"CYD performance evidence — {label}"
    text = [f"# {title}", "",
        f"Capture status: **{result['status']}**. Completed physical runs: **{len(measured)}/{expected}**; "
        f"pending or incomplete: **{pending}**. Explicit NOT RUN workload classes: **{len(missing_cases)}**.", ""]
    if is_preflight:
        text += ["This is a short preflight capture. Its observations do not establish the full mission envelope or a sustained stress result.", ""]
    text += [f"Board: {board.get('board', 'UNKNOWN')}. Source [{result['source_sha']}](https://github.com/techrote/cyd-diviner/commit/{result['source_sha']}); "
             f"profile `{result['profile']}`; firmware SHA256 `{result['firmware_sha256']}`. "
             f"Input results SHA256 `{input_sha256}`.", "",
        f"CPU {info.get('cpu_mhz', 'UNKNOWN')} MHz, {info.get('cores', 'UNKNOWN')} cores; flash {info.get('flash_bytes', 'UNKNOWN')} bytes at "
        f"{info.get('flash_hz', 'UNKNOWN')} Hz; PSRAM {info.get('psram_bytes', 'UNKNOWN')} bytes. "
        f"LCD SPI write/read {info.get('lcd_spi_hz', 'UNKNOWN')}/{info.get('lcd_read_hz', 'UNKNOWN')} Hz; "
        f"serial {result.get('serial_baud', 'UNKNOWN')} baud. SDK `{info.get('sdk', 'UNKNOWN')}`.", "",
        f"Fixed plan seed: `{plan.get('seed', 'UNKNOWN')}`. Ordinary requested warmup/window: "
        f"{plan.get('warmup_ms', 'UNKNOWN')}/{plan.get('duration_ms', 'UNKNOWN')} ms; "
        f"default repetitions: {plan.get('repetitions', 'UNKNOWN')}. Per-case overrides remain in [results.json](results.json). "
        f"Reset policy: `{info.get('scene_reset_policy', 'UNKNOWN')}`. "
        f"Audio-window alignment: `{info.get('audio_window_alignment', 'UNKNOWN')}`.", "",
        f"Saved display settings at initial inventory: theme {display.get('theme_id', 'UNKNOWN')}, "
        f"background style {display.get('background_style', 'UNKNOWN')}, gradient mode {display.get('gradient_mode', 'UNKNOWN')}, "
        f"canvas depth {display.get('canvas_depth', 'UNKNOWN')} bits. Orientation/backdrop commands then override the inventory values per workload. "
        "The exact palette, intensity table, setup acknowledgements and actual scene receipts are retained in JSON/raw logs.", "",
        "## Interpretation", "",
        "FPS is completed physical presentations divided by the target window, with the existing 33 ms frame cap. "
        "CPU percentages use tick-sampled current-task versus idle-task classification, not cycle-accurate task or interrupt accounting. "
        "Direct rendering interleaves CPU drawing and SPI transfer; its present duration cannot be separated into independent raster and bus costs. "
        "Histogram percentiles below are **upper bounds**. Human tables tighten each bound to `min(raw powers-of-two bucket upper, exact observed maximum)`; "
        "a capped bound equal to the maximum is still a bound, not an exact percentile. Raw target records and summary CSVs retain the original bucket upper bounds unchanged. Maxima are exact observed durations. "
        "A p99 is omitted below 100 observations. Percentiles and maxima describe these finite runs; they are not deadline guarantees.", "",
        "Only repetitions with matching requested settings, duration, warmup, instrumentation and actual scene/allocation receipts are pooled. "
        "A scope suffix identifies variants if the same workload name has differing receipts. Independent maxima or percentiles must not be added "
        "as if they came from the same audio block.", "",
        f"Instrumentation A/B scope: `{info.get('instrumentation_ab_scope', 'UNKNOWN')}`. "
        "The enable switch controls histogram insertion while timestamps, hooks, frame/block counts and retained sampling remain. "
        "The on/off rows therefore measure that insertion change only. Total overhead relative to an ordinary firmware binary is not established by this switch. "
        "Disabled histograms give unavailable timing distributions rather than zero-duration work. "
        "The approved primary image reserves 24,008 bytes in its static histogram/sample arrays alone; other profiler state, queues and task stacks are additional. "
        "These arrays remain with insertion disabled. Reported heap describes the profiling image and workload history; ordinary firmware headroom cannot be inferred by subtracting only this footprint. "
        "Detailed source and method audits are in the repository's `evidence/method-notes.md`.", "",
        "## Physical scene envelopes", ""]
    rows = []
    name_counts = collections.Counter(row["workload"] for row in summary["scenes"])
    for scene in summary["scenes"]:
        workload, scope = scene["workload"], scene["scope"]
        interval = metric_index.get((workload, scope, "frame_interval_us"), {})
        present = metric_index.get((workload, scope, "frame_present_us"), {})
        name = workload + (f" [{scope}]" if name_counts[workload] > 1 else "")
        rows.append([name, scene["repetitions"], scene["frames"], fmt(scene["completed_frames_per_s"]),
                     f"{number(interval, 'p95_upper', 1000)}/{number(interval, 'max', 1000)}",
                     f"{number(present, 'p95_upper', 1000)}/{number(present, 'max', 1000)}",
                     f"{number(scene, 'core0_weighted_busy_pct', digits=1)}/{number(scene, 'core1_weighted_busy_pct', digits=1)}",
                     f"{scene.get('heap_free_min_bytes')}/{scene.get('heap_largest_min_bytes')}"])
    text += [table(["Workload", "reps", "presents", "FPS", "interval p95≤/max ms", "present p95≤/max ms", "CPU0/1 %", "min free/largest B"], rows),
        "Free/largest heap values are minima of retained samples in each scope, not continuous allocation traces. "
        "Boot-wide heap low water and task-lifetime stack low water are separate fields in the summary; they cannot identify a per-scene leak alone.", "",
        "## Touchscreen polling and control dispatch", ""]
    touch_selected = set()
    for row in summary["scenes"]:
        if row["workload"] == "menu/landscape/theme" or row["workload"].startswith("touch/"):
            touch_selected.add((row["workload"], row["scope"]))
    for orientation in ("landscape", "portrait"):
        candidates = [row for row in summary["scenes"] if row["workload"].startswith(f"visions/{orientation}/fullscreen/")]
        if candidates:
            slowest = min(candidates, key=lambda row: row["completed_frames_per_s"])
            touch_selected.add((slowest["workload"], slowest["scope"]))
    touch_rows = []
    for row in summary["scenes"]:
        workload, scope = row["workload"], row["scope"]
        if (workload, scope) not in touch_selected:
            continue
        poll = metric_index.get((workload, scope, "touch_poll_us"), {})
        dispatch = metric_index.get((workload, scope, "touch_dispatch_us"), {})
        poll_count, dispatch_count = poll.get("count"), dispatch.get("count")
        scope_ends = [record for run in grouped[(workload, scope)] for record in run.get("target", []) if record.get("type") == "end"]
        if scope_ends and not any(end.get("histograms_enabled", end.get("instrumentation", True)) for end in scope_ends):
            poll_count = dispatch_count = None
        touch_rows.append([workload, row["repetitions"], fmt(poll_count), fmt(poll_count / row["elapsed_s"] if poll_count is not None else None),
                           f"{number(poll, 'p95_upper', digits=0)}/{number(poll, 'max', digits=0)}", fmt(dispatch_count),
                           f"{number(dispatch, 'p95_upper', digits=0)}/{number(dispatch, 'max', digits=0)}"])
    text += [table(["Selected scope", "reps", "XPT polls n", "observed polls/s", "poll p95≤/max µs", "dispatch n", "dispatch p95≤/max µs"], touch_rows),
        "The selection includes the menu baseline, the slowest captured fullscreen scope per orientation and scripted-touch cases. "
        "Polling rate is the measured `touch_poll_us` observation count divided by target steady-window elapsed time: actual XPT2046 `getPoint` calls, "
        "including polls with no contact. It is an average call rate, not a guaranteed polling interval or physical touch response rate. "
        "`touch_dispatch_us` counts control-dispatch invocations, including no-contact samples and synthetic input; it does not count distinct physical touches or generated application keys. "
        "Scripted replay is firmware injection. No controlled physical contact stimulus, contact-to-photon acquisition or physical latency distribution was measured.", "",
        "## Audio scheduling", ""]
    rate = info.get("audio_rate")
    block = info.get("audio_block")
    period = block * 1e6 / rate if rate and block else None
    text += [f"Configured audio: {rate if rate else 'UNKNOWN'} Hz, {block if block else 'UNKNOWN'} samples/block "
             f"({fmt(period, 3)} µs nominal block period). Audio task core/priority {info.get('audio_core', 'UNKNOWN')}/{info.get('audio_priority', 'UNKNOWN')}; "
             f"PCM writer core/priority {info.get('pcm_core', 'UNKNOWN')}/{info.get('pcm_priority', 'UNKNOWN')}. "
             f"I2S inventory: {display.get('i2s_dma_buffers', 'UNKNOWN')} DMA buffers × "
             f"{display.get('i2s_dma_frames_per_buffer', 'UNKNOWN')} frames, {display.get('i2s_slots', 'UNKNOWN')} slots, "
             f"DAC GPIO {display.get('dac_gpio', 'UNKNOWN')}.", ""]
    audio_rows = []
    audio_selected = set()
    for name in ("audio_callback_us", "audio_period_us", "audio_lock_us", "audio_enqueue_us"):
        candidates = sorted((row for row in summary["metrics"] if row["name"] == name and row.get("max") is not None),
                            key=lambda row: row["max"], reverse=True)[:2]
        audio_selected.update((row["workload"], row["scope"]) for row in candidates)
    for scene in summary["scenes"]:
        workload, scope = scene["workload"], scene["scope"]
        if not workload.startswith(("audio/", "combined/", "sustained/")) and (workload, scope) not in audio_selected:
            continue
        def metric(name):
            return metric_index.get((workload, scope, name), {})
        callback, enqueue, cadence = metric("audio_callback_us"), metric("audio_enqueue_us"), metric("audio_period_us")
        late = metric("audio_late_periods_inferred").get("sum")
        errors = metric("audio_write_errors").get("sum")
        short = metric("audio_short_writes").get("sum")
        audio_rows.append([workload, callback.get("count", 0), f"{number(callback, 'p95_upper', digits=0)}/{number(callback, 'max', digits=0)}",
                           number(metric("audio_lock_us"), "max", digits=0), number(metric("audio_mix_us"), "max", digits=0),
                           f"{number(enqueue, 'p95_upper', digits=0)}/{number(enqueue, 'max', digits=0)}",
                           f"{number(cadence, 'p99_upper', digits=0)}/{number(cadence, 'max', digits=0)}", f"{late}/{errors}/{short}"])
    text += [table(["Workload", "callback n", "callback p95≤/max µs", "lock max µs", "mix max µs", "enqueue p95≤/max µs", "period p99≤/max µs", "late/write/short"], audio_rows),
        "The table includes explicit audio/combined/stress workloads and the scopes with the largest observed callback, cadence, lock or enqueue tails. "
        "Enqueue is the duration of blocking `i2s_write`, including waiting for DMA capacity. It is not an isolated compute cost. "
        "Late-period counters are inferred scheduling events. DMA underruns and analog output continuity are **unknown** without an appropriate detector/acquisition fixture. "
        "The complete lock, callback, overlay, mix, enqueue and cadence distributions are in [summary-metrics.csv](summary-metrics.csv).", "",
        "## USB PCM integrity", ""]
    pcm_rows = []
    for run in measured:
        pcm = run.get("pcm")
        if pcm:
            pcm_rows.append([run["run_id"], run["workload"], pcm["valid_frames"], pcm["crc_errors"], pcm["missing_sequence_frames"],
                             pcm["duplicate_frames"], pcm["backward_sequence_discontinuities"],
                             f"{pcm['discarded_bytes']}/{pcm['trailing_bytes']}"])
    text += [table(["Run", "Workload", "valid", "CRC", "missing", "duplicate", "backward", "discarded/tail B"], pcm_rows),
        "PCM receive windows include warmup, the steady target window, a 250 ms guard and stop-tail draining. "
        "Target counters cover the steady target window only. Missing sequence numbers can originate in a producer queue or transport; "
        "compare target queue/drop records before attribution. Loss before the first received sequence and after the last frame is unknown. "
        "Host arrival timing includes UART/OS buffering and cannot establish target audio cadence or DMA underruns. "
        "Raw per-run PCM binaries can be replayed with `capture.py decode-pcm`.", "",
        "## Sustained CPU load and temperature", ""]
    stress_runs = [run for run in measured if run.get("settings", {}).get("stress_seconds")]
    stress_rows = []
    for run in stress_runs:
        assessment = stress_assessment(run["target"], run["duration_ms_requested"] / 1000, info)
        samples = [record for record in run["target"] if record.get("type") == "sample"]
        raw = [record["temperature_raw"] for record in samples if isinstance(record.get("temperature_raw"), (int, float))]
        indicated = [record["temperature_c_uncalibrated"] for record in samples if isinstance(record.get("temperature_c_uncalibrated"), (int, float))]
        weighted = assessment["weighted_busy_pct"]
        stress_rows.append([run["run_id"], fmt(assessment["target_elapsed_s"], 3), len(samples), fmt(assessment["observed_sample_span_s"], 3),
                            f"{fmt(weighted['core0'])}/{fmt(weighted['core1'])}", str(assessment["near_100_percent_confirmed"]),
                            f"{min(raw)}/{max(raw)}" if raw else "—", f"{fmt(min(indicated), 3)}/{fmt(max(indicated), 3)}" if indicated else "—"])
    text += [table(["Run", "target s", "n", "sample span s", "CPU0/1 %", "≥360s near100 confirmed", "raw min/max", "uncal °C min/max"], stress_rows),
        f"Recorded sensor method: `{info.get('temperature_method', 'UNKNOWN')}`. "
        f"{legacy_sensor_note}The sensor API is undocumented and its temperature accuracy is unsupported. "
        "Raw readings are uncalibrated indications. The reported °C values are the firmware's conversion, not independently calibrated die temperature. Ambient temperature was "
        f"{board.get('ambient_temperature_method', 'NOT MEASURED')}. Retained intervals, raw ticks and all sensor samples are available in [samples.csv](samples.csv). "
        "The confirmation predicate requires a completed ≥360-second window, ≥360 seconds of retained sample span, valid CPU hooks and ≥95% weighted busy time on both cores.", "",
        "Stress iteration counters are unsigned 32-bit modular progress indicators, incremented in batches of 256; they can wrap modulo 2^32. "
        "They are not throughput totals. Timed windows and retained raw per-core tick observations support the sustained-load assessment.", "",
        "## SPI and portable kernels", ""]
    all_temperatures = [record["temperature_raw"] for run in measured for record in run.get("target", [])
                        if record.get("type") in ("sample", "state") and isinstance(record.get("temperature_raw"), (int, float))]
    if all_temperatures:
        insertion = next(index for index, line in enumerate(text) if line == "## SPI and portable kernels")
        text[insertion:insertion] = [f"Across this capture, **{len(all_temperatures)}** retained in-window samples and outside-kernel/SPI snapshots "
                                    f"span raw sensor codes **{min(all_temperatures)}–{max(all_temperatures)}**. "
                                    "These indications show the recorded trend without supplying an accuracy or calibration claim.", ""]
    spi_rows = [[row["workload"], row["repetitions"], row["samples"], row["bytes_per_sample"], fmt(row["median_us"] / 1000),
                 fmt(row["p95_us"] / 1000), fmt(row["p99_us"] / 1000), fmt(row["max_us"] / 1000), fmt(row["median_goodput_bytes_per_s"] / 1e6)]
                for row in summary["spi"]]
    text += [table(["SPI workload", "reps", "n", "bytes/frame", "median ms", "p95 ms", "p99 ms", "max ms", "median MB/s"], spi_rows),
        "SPI uses precomputed RGB565 eight-row stripes, includes commands and completion waits, and excludes artwork generation. "
        "It measures this transfer path, not an abstract SPI clock-rate ceiling.", ""]
    kernel_rows = [[row["kernel"], "large" if row["large"] else "realistic", row["repetitions"], row["samples"],
                    row["median_us"], row["p95_us"], row["p99_us"], row["max_us"], row["working_bytes"], row["output_bytes"],
                    fmt(row["median_compute_us_per_output_byte"], 3), row["checksum_consistent_across_repetitions"]]
                   for row in summary["kernels"]]
    text += [table(["Kernel", "size", "reps", "n", "median µs", "p95 µs", "p99 µs", "max µs", "working B", "output B", "µs/output B", "checksum stable"], kernel_rows),
        "Kernel timings pool exact chronological samples and include RTOS preemption. Output inspection is excluded. "
        "Working bytes describe kernel state; they are not the bytes that a remote node would need per request. "
        "The same seed and checksum checks support reproduction, while float/fixed-point precision comparisons remain separate validation evidence. "
        "Physical S3/C3 comparative execution is NOT RUN unless separately evidenced; a successful target build is not a hardware measurement.", "",
        "## Actual scene and allocation receipts", ""]
    receipt_rows = []
    for (workload, scope), runs in grouped.items():
        receipts = []
        for record in runs[0].get("setup_target", []):
            if record.get("type") == "scene_settings":
                receipt = clean_receipt(record)
                if receipt.get("scene") == "visions":
                    keys = ("actual_base_effect", "actual_layered", "history_requested", "history_ready", "history_format", "history_payload_bytes",
                            "tunnel_allocated", "tunnel_payload_bytes", "fire_allocated", "fire_payload_bytes", "native_fullscreen_at_receipt")
                else:
                    keys = tuple(receipt)
                receipts.append(", ".join(f"{key}={receipt[key]}" for key in keys if key in receipt))
        if receipts:
            receipt_rows.append([workload, scope, "; ".join(receipts)])
    text += [table(["Workload", "scope", "actual configuration before measurement"], receipt_rows),
        "Visions receipts are emitted after configuration and before the next frame. For native fullscreen trails the implementation uses "
        "bounded geometric paths and releases pixel history/tunnel on its first fullscreen frame; receipt-time allocated bytes must not be presented "
        "as measured fullscreen history occupancy. Sampled heap provides the in-window evidence. These paths are not numerically identical rendering kernels.", "",
        "## Wireless scope", "",
        "Radio rows measure isolated controller initialization and its effect on local scenes. They perform no peer discovery, association or traffic. "
        "Radio-off acknowledgements record requested software state; disable/deinitialize return statuses and actual RF emissions are not independently exported or measured. "
        "This is a requested-state baseline, not independently verified RF-off operation. "
        "No measured goodput, RTT, jitter/loss under traffic, or Wi-Fi/BLE/ESP-NOW coexistence envelope is established by these rows.", ""]
    radio_rows = []
    for row in summary["scenes"]:
        if row["workload"].startswith("radio-init/"):
            radio_rows.append([row["workload"], row["repetitions"], fmt(row["completed_frames_per_s"]),
                               f"{number(row, 'core0_weighted_busy_pct', digits=1)}/{number(row, 'core1_weighted_busy_pct', digits=1)}",
                               row.get("heap_free_min_bytes"), row.get("heap_largest_min_bytes")])
    text += [table(["Initialized mode/local scene", "reps", "FPS", "CPU0/1 %", "min free B", "min largest B"], radio_rows),
        "## I2S driver event capability", ""]
    event_rows = []
    for run in measured:
        for record in run.get("target", []):
            if record.get("type") == "i2s_events":
                event_rows.append([run["run_id"], run["workload"], record.get("enabled", "UNKNOWN"), record.get("queue_available", "UNKNOWN"), record.get("observed", "UNKNOWN"), record.get("tx_underflow_indications", "UNKNOWN"),
                    record.get("dma_descriptor_faults", "UNKNOWN"), record.get("tx_done_events", "UNKNOWN"),
                    record.get("queue_full_observations", "UNKNOWN"), record.get("drain_limit_hits", "UNKNOWN"),
                    record.get("max_drain_interval_us", "UNKNOWN")])
    if event_rows:
        text += [table(["Run", "Workload", "enabled", "available", "observed", "TX_Q_OVF", "DMA_ERROR", "TX_DONE", "queue-full observations", "drain-limit hits", "max drain gap µs"], event_rows),
            "These are directly observed legacy I2S driver indications. TX_Q_OVF means the driver's free-TX-buffer queue was full; "
            "the raw schema's legacy `tx_underflow_indications` field counts that enum and is not a measured analog gap count. DMA_ERROR is a descriptor fault. "
            "Assignment to a measurement window is by event-drain completion time, not ISR generation time. Finite driver queues and polling can lose indications; "
            "queue/drain diagnostics bound visibility but do not establish an exact loss count. Zero indications do not prove analog continuity.", ""]
    else:
        text += [f"Driver event queue capability reported: `{info.get('i2s_event_queue_enabled', 'UNKNOWN / not exposed in this image')}`. "
            "No direct I2S event records occur in this capture. TX_Q_OVF and DMA_ERROR counts are unknown; "
            "the primary image's write-error/short-write counters and inferred late periods do not substitute for them. "
            "A supplementary events image must retain its own source/firmware identity and evidence folder.", ""]
    text += [
        "## Evidence-based recommendations", ""]
    ordinary = [row for row in summary["scenes"] if not row["workload"].startswith(("sustained/", "touch/", "instrumentation/off"))]
    if ordinary:
        slowest = min(ordinary, key=lambda row: row["completed_frames_per_s"])
        text += [f"* Use scene-specific display budgets: the lowest completed cadence among the captured ordinary scopes is "
                 f"**{slowest['completed_frames_per_s']:.2f} FPS** for `{slowest['workload']}` across {slowest['repetitions']} repetitions. "
                 "Use the measured tail/maximum durations and actual path when choosing VIBEWARE visual complexity; do not apply the menu rate to layered fullscreen effects."]
    callbacks = [row for row in summary["metrics"] if row["name"] == "audio_callback_us" and row["max"] is not None]
    if callbacks:
        worst = max(callbacks, key=lambda row: row["max"])
        text += [f"* The largest observed main audio callback is **{worst['max']} µs** in `{worst['workload']}` "
                 f"(n={worst['count']}, nominal block period {fmt(period, 3)} µs). Keep callback, lock, overlay, mix and blocking enqueue "
                 "as separate budgets; verify correlated per-block deadlines and analog continuity before claiming an underrun-free audio envelope."]
    if summary["spi"]:
        transfer = summary["spi"][0]
        text += [f"* Budget full RGB565 transfers using the measured stripe path: median **{transfer['median_us'] / 1000:.2f} ms** "
                 f"and p99 **{transfer['p99_us'] / 1000:.2f} ms** for {transfer['bytes_per_sample']} bytes "
                 f"(n={transfer['samples']}). Artwork computation adds to or interleaves with this transfer work according to the path."]
    if summary["kernels"]:
        candidates = sorted((row for row in summary["kernels"] if row["median_compute_us_per_output_byte"] is not None),
                            key=lambda row: row["median_compute_us_per_output_byte"], reverse=True)[:5]
        labels = ", ".join(f"`{row['kernel']}` ({'large' if row['large'] else 'realistic'}: {row['median_compute_us_per_output_byte']:.3f} µs/output B)" for row in candidates)
        text += [f"* Prioritize measured high-compute/low-output kernels for owned-peer offload experiments: {labels}. "
                 "These output-only ratios nominate experiments; they do not establish a wireless speedup. Preserve hard audio deadlines locally until the complete link/remote timing is measured."]
    text += ["* Offload break-even requires `local compute > remote compute + measured RTT + (input bytes + output bytes) / measured goodput` in consistent units. "
             "Measure input/state serialization, link tails/loss and remote service time on each candidate peer. No numeric minimum useful ratio can be established from controller-init-only radio tests.",
             "* Reserve memory using the observed free/largest-block minima and allocation receipts for the intended combination of effects. "
             "Saved-palette gradients, history format, fullscreen path and radio initialization are workload settings, not incidental details. "
             "Repeat matching scene transitions and long sessions before diagnosing a leak or guaranteeing a memory margin.", "",
        "## Missing evidence and reproduction", ""]
    text += [table(["Workload class", "status", "reason"], [[case["name"], "NOT RUN", case.get("reason", "Unspecified")] for case in missing_cases]),
        f"Current unfinished planned repetitions: {pending}. Missing observations stay absent rather than receiving theoretical estimates.", "",
        "Reproduce with the captured plan, identified board, exact firmware digest and matching source/toolchain receipts. "
        "The harness never flashes. Owner authorization, private backup verification and the flash receipt are separate evidence. "
        "The on-board identity does not independently attest the firmware binary digest; preserve the flash/build chain.", "",
        "```powershell", f"python tools/perf/capture.py analyze evidence/physical/{label}/results.json",
        f"python tools/perf/summarize.py evidence/physical/{label}/results.json",
        f"python tools/perf/report.py evidence/physical/{label}/results.json --label {label}", "```", "",
        "Artifacts: [results.json](results.json), [raw target JSONL](target.jsonl), [raw serial bytes](serial-rx.bin), "
        "[timestamped serial events](serial-events.jsonl), [metrics.csv](metrics.csv), [samples.csv](samples.csv), "
        "[summary.json](summary.json), [summary scenes](summary-scenes.csv), [summary metrics](summary-metrics.csv), "
        "[summary kernels](summary-kernels.csv), [artifact checksums](artifact-hashes.json).", ""]
    if plots:
        text += ["## Plots", ""]
        for plot in plots:
            text += [f"![{pathlib.Path(plot).stem}]({plot})", ""]
    return "\n".join(text).rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=pathlib.Path)
    parser.add_argument("--label", help="Capture label; defaults to input directory name")
    parser.add_argument("--output", type=pathlib.Path, help="Editable Markdown path; defaults beside results")
    args = parser.parse_args()
    payload = args.results.read_bytes()
    result = json.loads(payload)
    if result.get("schema") != "cyd-perf-capture/1" or result.get("evidence_class") != "physical_measured":
        raise ValueError("Report requires physical cyd-perf-capture/1 evidence")
    output = args.output or args.results.parent / "REPORT.md"
    plots = sorted(pathlib.Path(os.path.relpath(path, output.parent)).as_posix()
                   for directory in ("figures", "plots") for path in args.results.parent.glob(directory + "/*.png"))
    output.parent.mkdir(parents=True, exist_ok=True)
    rendered = generate(result, args.label or args.results.parent.name, hashlib.sha256(payload).hexdigest(), plots)
    if output.parent.resolve() != args.results.parent.resolve():
        prefix = pathlib.Path(os.path.relpath(args.results.parent, output.parent)).as_posix()
        rendered = re.sub(r"\]\(([-a-zA-Z0-9_.]+\.(?:json|jsonl|bin|csv))\)", lambda match: f"]({prefix}/{match.group(1)})", rendered)
    output.write_text(rendered, encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
