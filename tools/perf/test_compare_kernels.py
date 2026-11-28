"""Independent numeric comparison guardrails; synthetic fixtures only."""
import copy
import unittest

from compare_kernels import compare


def item(region="internal", timing=10, checksum=1):
    return {"kernel": "osc-f32", "large": 0, "memory_region": region, "repetitions": 3,
            "n": 768, "median_us": timing, "p95_us": timing + 1, "p99_us": timing + 2, "max_us": timing + 3,
            "workload": {"algorithm": "fixture", "precision": "float32", "work_unit": "samples",
                         "seed": 23063, "large": 0, "warmup": 3, "samples": 256,
                         "working_bytes": 1024, "output_bytes": 256, "work_units_per_step": 128},
            "outputs": [{"checksum_fnv1a": checksum, "output_min": -1, "output_max": 1, "output_sum": 0}] * 3}


class ComparisonTests(unittest.TestCase):
    def test_regions_remain_separate_and_ratio_uses_observed_step_times(self):
        result = compare({"kernels": [item()]}, {"kernels": [item(timing=5), item("psram", timing=20)]})
        self.assertEqual(len(result), 2)
        self.assertEqual(result[0]["cyd_median_div_s3_median"], 2)
        self.assertEqual(result[1]["cyd_median_div_s3_median"], .5)
        self.assertTrue(result[0]["fingerprints_match"])
        self.assertEqual(result[0]["numeric_summary_differences"]["output_sum"]["relative_to_abs_cyd"], 0)

    def test_workload_mismatch_never_produces_a_performance_ratio(self):
        right = item(timing=5)
        right["workload"]["seed"] = 7
        result = compare({"kernels": [item()]}, {"kernels": [right]})[0]
        self.assertEqual(result["status"], "NOT COMPARABLE: workload differs")
        self.assertNotIn("cyd_median_div_s3_median", result)

    def test_numeric_differences_reported_without_inventing_tolerance_or_equality(self):
        right = item(checksum=2)
        right["outputs"] = copy.deepcopy(right["outputs"])
        for output in right["outputs"]:
            output["output_max"] = 1.0001
        result = compare({"kernels": [item()]}, {"kernels": [right]})[0]
        self.assertFalse(result["fingerprints_match"])
        self.assertAlmostEqual(result["numeric_summary_differences"]["output_max"]["absolute_difference"], .0001)
        self.assertNotIn("numerically_equivalent", result)


if __name__ == "__main__":
    unittest.main()
