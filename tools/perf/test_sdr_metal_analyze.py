"""Native word and timing-scope checks; no physical observations synthesized."""
import struct
import unittest

try:
    import numpy as np
    from sdr_metal_analyze import decode_words, iq_statistics, rx_rates
except ImportError:
    np = None


@unittest.skipIf(np is None, "Optional NumPy analysis dependency is not installed")
class MetalAnalysisTests(unittest.TestCase):
    def test_signed10_fields_and_upper_metadata_are_preserved(self):
        expected_i, expected_q, metadata = [-512, -1, 0, 511], [511, 0, -512, -1], [0, 1, 4095, 123]
        payload = b"".join(struct.pack("<I", (i & 1023) | ((q & 1023) << 10) | (m << 20))
                           for i, q, m in zip(expected_i, expected_q, metadata))
        i, q, m = decode_words(payload)
        self.assertEqual(i.tolist(), expected_i)
        self.assertEqual(q.tolist(), expected_q)
        self.assertEqual(m.tolist(), metadata)

    def test_native_frequency_is_explicitly_clock_derived(self):
        rate = 16000000
        time = np.arange(4096) / rate
        i = np.round(100 * np.cos(2 * np.pi * 1000000 * time)).astype(np.int32)
        q = np.round(100 * np.sin(2 * np.pi * 1000000 * time)).astype(np.int32)
        words = ((i & 1023) | ((q & 1023) << 10)).astype("<u4")
        signal = iq_statistics(words.tobytes(), rate)
        self.assertAlmostEqual(signal["hann_ac_peak_frequency_from_nominal_clock_hz"], 1000000)
        self.assertGreater(signal["lag1_complex_coherence"], .99)
        self.assertEqual(signal["at_signed10_field_limit_fraction"], 0)

    def test_usb_export_loop_and_no_export_occupancy_are_not_nominal_hardware_rate(self):
        base = {"mode": "bench", "loop_elapsed_us": 1000, "capture_observation_us_sum": 80,
                "captures_completed": 10, "sample_count_each": 100, "nominal_rate_hz": 16000000}
        bench = rx_rates(base)
        exported = rx_rates({**base, "mode": "capture", "loop_elapsed_us": 50000})
        self.assertEqual(bench["nominal_sample_horizon_sum_us"], 62.5)
        self.assertEqual(bench["native_samples_per_loop_second"], 1000000)
        self.assertEqual(bench["native_samples_per_observed_capture_second"], 12500000)
        self.assertEqual(exported["native_samples_per_loop_second"], 20000)
        self.assertIn("not continuous RF duty", bench["scope"])


if __name__ == "__main__": unittest.main()
