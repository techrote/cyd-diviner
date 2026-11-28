"""Finite protocol fixtures, not measured wireless performance."""
import copy
import hashlib
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from capture import METRIC_NAMES
from link_analyze import analyze, report, main, value_range


def profile(run_id):
    bins = [0] * 32
    bins[4] = 100
    records = [{"type": "metric", "run_id": run_id, "name": name, "count": 100,
                "sum": 1000, "min": 10, "max": 10, "histogram": bins} for name in METRIC_NAMES]
    records += [{"type": "sample", "run_id": run_id, "elapsed_ms": 3000, "cpu0_busy_ticks": 50,
                 "cpu0_total_ticks": 1000, "core0_busy_pct": 5, "cpu1_busy_ticks": 900,
                 "cpu1_total_ticks": 1000, "core1_busy_pct": 90, "heap_free_bytes": 70000,
                 "heap_largest_bytes": 30000, "temperature_raw": 128},
                {"type": "end", "run_id": run_id, "complete": True, "elapsed_us": 10000000,
                 "frames": 300, "samples": 1, "samples_dropped": 0, "audio_completed_blocks": 1875}]
    return records


def run(run_id, idle=False):
    settings = {"topology": "direct_espnow", "local_workload": "menu", "transport": "espnow",
                "pattern": "none" if idle else "rtt", "payload_bytes": 0 if idle else 64,
                "packets_per_second": 0 if idle else 50, "mode": "idle" if idle else "rtt"}
    value = {"run_id": run_id, "workload": "fixture/idle" if idle else "fixture/rtt", "status": "MEASURED",
        "profile_status": "MEASURED", "repetition": 1, "settings": settings, "cyd_profile": profile(run_id),
        "setup_target": [{"type": "scene_settings", "scene": "drone", "delay_allocated": True}],
        "topology_info": {name: {"actual_channel": 6, "power_save_enum": 0} for name in ("cyd", "peer")}}
    if not idle:
        bins = [0] * 32
        bins[4] = 128
        value["cyd_link"] = [{"type": "link_summary", "run_id": run_id, "complete": True,
            "duration_us": 10000000, "rtt_count": 128, "rtt_min_us": 10, "rtt_max_us": 10,
            "rtt_sum_us": 1280, "rtt_histogram_counts": bins, "tx_api_accepted_packets": 200,
            "tx_payload_bytes": 12800, "rx_unique_packets": 128, "rx_payload_bytes": 8192,
            "rtt_adjacent_abs_difference_count": 127, "rtt_adjacent_abs_difference_sum_us": 0,
            "rtt_adjacent_abs_difference_max_us": 0}]
        value["peer_link"] = [{"type": "link_summary", "run_id": run_id, "complete": True,
            "duration_us": 0, "rtt_count": 0, "rtt_histogram_counts": [0] * 32,
            "rx_unique_packets": 180, "rx_payload_bytes": 11520, "too_old": 0}]
    return value


def capture(runs):
    endpoints = {name: {"identity": {"source_sha": letter * 40, "source_digest": letter * 64,
        "firmware_sha256": letter * 64, "profile": name + "-fixture"},
        "target_info": [{"type": "info", "cpu0_hook": True, "cpu1_hook": True,
            "cpu_mhz": 240, "psram_bytes": 0}]} for name, letter in (("cyd", "a"), ("peer", "b"))}
    return {"schema": "cyd-perf-link-capture/1", "evidence_class": "physical_measured", "status": "FAILED",
        "execution_complete": True, "endpoints": endpoints, "plan": {"warmup_ms": 2000, "duration_ms": 10000}, "runs": runs}


class LinkAnalysisTests(unittest.TestCase):
    def test_even_repetition_median_averages_middle_values(self):
        self.assertEqual(value_range([10, 30])["median"], 20)
        self.assertEqual(value_range([10, 30, 50])["median"], 30)
        self.assertIsNone(value_range([])["median"])

    def test_raw_panic_and_later_startup_do_not_become_hardware_unsupported_or_zero_rtt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / "cyd").mkdir()
            data = b"prefix\nGuru Meditation Error: Core 0 panic'ed (StoreProhibited).\nDiviner CYD: LCD ready\n"
            (root / "cyd/serial-rx.redacted.bin").write_bytes(data)
            (root / "cyd/serial-events.redacted.jsonl").write_text(json.dumps({"event": "rx", "offset": 0, "bytes": len(data), "host_elapsed_s": 4.2}) + "\n")
            failure = run("failed")
            failure.update(status="FAILED", error="ble_mode_required")
            failure.pop("cyd_link")
            failure.pop("peer_link")
            result = analyze(capture([failure]), root)
            self.assertTrue(result["failure_context"]["panic_observed"])
            self.assertTrue(result["failure_context"]["startup_or_reset_after_panic"])
            self.assertEqual(result["raw_runtime_diagnostics"][0]["raw_byte_offset"], len(b"prefix\n"))
            self.assertIsNone(result["groups"][0]["rtt"])
            self.assertEqual(result["groups"][0]["measured_runs"], 0)
            self.assertIn("not a BLE hardware-unsupported result", report(result, "c" * 64, "results.json"))

    def test_offline_cli_preserves_status_and_hashes_written_artifacts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source = root / "fixture.json"
            source.write_text(json.dumps(capture([run("one")])) + "\n", encoding="utf-8")
            output = root / "analysis"
            with mock.patch("sys.argv", ["link_analyze.py", str(source), "--output", str(output)]):
                main()
            manifest = json.loads((output / "link-analysis-manifest.json").read_text())
            self.assertEqual(manifest["capture_status"], "FAILED")
            self.assertEqual(manifest["input_sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
            for name, receipt in manifest["outputs"].items():
                self.assertEqual(receipt["sha256"], hashlib.sha256((output / name).read_bytes()).hexdigest())

    def test_matching_sessions_pool_histograms_and_match_idle_without_fake_idle_rtt(self):
        result = analyze(capture([run("idle", True), run("one"), run("two")]))
        self.assertEqual(result["capture_status"], "FAILED")
        self.assertEqual(len(result["groups"]), 2)
        control, active = result["groups"]
        self.assertIsNone(control["rtt"])
        self.assertIsNone(control["received_payload_bytes_per_second"]["median"])
        self.assertEqual(active["rtt"]["n"], 256)
        self.assertEqual(active["rtt"]["p99_us_upper"], 10)
        self.assertEqual(active["accepted_not_observed_fraction"], .1)
        self.assertEqual(active["fps_difference_from_matched_idle"], 0)
        self.assertEqual(active["cyd_core0_busy_pct"], 5)
        self.assertIsNone(active["peer_cpu_utilization"])

    def test_failed_profile_stays_separate_and_allocation_context_does_not_pool(self):
        first, other, failure = run("one"), run("other"), run("failed")
        other["setup_target"][0]["delay_allocated"] = False
        failure.update(status="FAILED", error="no received traffic")
        failure.pop("cyd_link")
        failure.pop("peer_link")
        result = analyze(capture([first, other, failure]))
        self.assertEqual(len(result["groups"]), 2)
        self.assertEqual(result["runs"][-1]["profile"]["status"], "MEASURED")
        group = next(value for value in result["groups"] if value["attempted_runs"] == 2)
        self.assertEqual(group["measured_runs"], 1)
        self.assertEqual(group["profile_valid_runs_including_failed_links"], 2)
        rendered = report(result, "c" * 64, "results.json")
        self.assertIn("FAILED/MEASURED", rendered)
        self.assertIn("not isolated RF loss", rendered)
        self.assertIn("64-sequence", rendered)

    def test_mismatched_endpoint_id_and_corrupt_histogram_cannot_be_called_measured(self):
        bad = run("one")
        bad["peer_link"][0]["run_id"] = "different"
        with self.assertRaises(ValueError):
            analyze(capture([bad]))
        bad = run("two")
        bad["cyd_link"][0]["rtt_histogram_counts"][4] -= 1
        with self.assertRaises(RuntimeError):
            analyze(capture([bad]))


if __name__ == "__main__":
    unittest.main()
