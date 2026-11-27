"""Analysis guardrails: differing scopes, unsupported readings, and missing evidence."""
import copy
import unittest

from report import generate, number
from summarize import summarize


def measured(name="visions/ui/gate", history_ready=True):
    bins = [0] * 32
    bins[7] = 100
    return {"run_id": "r1", "workload": name, "repetition": 1, "status": "MEASURED",
            "duration_ms_requested": 3000, "warmup_ms_requested": 1000,
            "settings": {"name": name, "layered": True},
            "setup_target": [{"type": "scene_settings", "scene": "visions", "history_ready": history_ready,
                              "host_received_elapsed_s": 1.0}],
            "target": [{"type": "end", "run_id": "r1", "complete": True, "frames": 60,
                        "elapsed_us": 3000000, "histograms_enabled": True},
                       {"type": "metric", "name": "audio_callback_us", "unit": "us", "count": 100,
                        "sum": 10000, "min": 100, "max": 100, "histogram": bins},
                       {"type": "sample", "temperature_raw": 128, "cpu0_busy_ticks": 10,
                        "cpu0_total_ticks": 100, "core0_busy_pct": None}]}


def capture(runs):
    return {"schema": "cyd-perf-capture/1", "evidence_class": "physical_measured", "source_sha": "a" * 40,
            "firmware_sha256": "b" * 64, "profile": "cyd-profile", "status": "CAPTURE COMPLETE",
            "plan": {"repetitions": len(runs), "duration_ms": 3000, "warmup_ms": 1000,
                     "workloads": [{"name": "visions/ui/gate"}]}, "runs": runs,
            "target_info": [{"type": "info", "temperature_method": "recorded_legacy_method"}]}


class ReportTests(unittest.TestCase):
    def test_human_bounds_cap_at_observed_max_without_mutating_raw_histogram(self):
        metric = {"p95_upper": 65536, "p99_upper": 65536, "max": 35914}
        self.assertEqual(number(metric, "p95_upper", 1000), "35.91")
        self.assertEqual(metric["p95_upper"], 65536)
        self.assertEqual(number(metric, "max", 1000), "35.91")

    def test_actual_allocation_and_duration_differences_split_pooling_scope(self):
        first, second = measured(), measured(history_ready=False)
        summary = summarize(capture([first, second]))
        self.assertEqual(len(summary["scenes"]), 2)
        self.assertEqual([row["repetitions"] for row in summary["scenes"]], [1, 1])
        second = copy.deepcopy(first)
        second["duration_ms_requested"] = 10000
        self.assertEqual(len(summarize(capture([first, second]))["scenes"]), 2)

    def test_host_receive_time_does_not_split_matching_repetitions_or_invent_cpu(self):
        first, second = measured(), measured()
        second["setup_target"][0]["host_received_elapsed_s"] = 50.0
        summary = summarize(capture([first, second]))
        self.assertEqual(len(summary["scenes"]), 1)
        self.assertEqual(summary["scenes"][0]["frames"], 120)
        self.assertEqual(summary["metrics"][0]["count"], 200)
        self.assertEqual(summary["metrics"][0]["p99_upper"], 128)
        self.assertIsNone(summary["scenes"][0]["core0_weighted_busy_pct"])
        self.assertEqual(summary["scenes"][0]["temperature_method"], "recorded_legacy_method")

    def test_corrupt_histogram_and_false_completion_not_reported_as_measured(self):
        run = measured()
        run["target"][1]["histogram"][7] -= 1
        with self.assertRaises(ValueError):
            summarize(capture([run]))
        run = measured()
        run["target"][0]["complete"] = False
        with self.assertRaises(ValueError):
            summarize(capture([run]))

    def test_preflight_missing_stress_wireless_and_analog_remain_unknown(self):
        rendered = generate(capture([measured("menu/landscape/theme")]), "preflight-test", "c" * 64)
        self.assertIn("short preflight", rendered)
        self.assertIn("No measured goodput", rendered)
        self.assertIn("No direct I2S event records", rendered)
        self.assertIn("uncalibrated", rendered)
        self.assertIn("observed polls/s", rendered)
        self.assertIn("No controlled physical contact stimulus", rendered)
        self.assertNotIn("**360.00", rendered)
        self.assertNotIn("wireless speedup achieved", rendered)


if __name__ == "__main__":
    unittest.main()
