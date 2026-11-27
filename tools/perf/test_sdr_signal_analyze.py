"""Valid-only signal math fixtures; no delivered RF performance claims."""
import pathlib
import tempfile
import unittest

try:
    import numpy as np
    from sdr_signal_analyze import analyze_signal, load_valid_iq
except ImportError:
    np = None
from test_sdr_capture import frame, END, START


@unittest.skipIf(np is None, "Optional NumPy analysis dependency is not installed")
class SignalAnalysisTests(unittest.TestCase):
    def test_raw_frame_loader_never_inserts_host_erasure_zeros(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = pathlib.Path(temporary) / "iq.bin"
            path.write_bytes(START + frame(0, 0, b"\x01\x02\x03\x04")
                             + frame(1, 6, b"\x05\x06\x07\x08") + END)
            indices, iq, integrity = load_valid_iq(path)
            self.assertEqual(list(indices), [0, 1, 6, 7])
            self.assertEqual(iq.tolist(), [[1, 2], [3, 4], [5, 6], [7, 8]])
            self.assertEqual(integrity["missing_output_samples_between_valid_frames"], 4)

    def test_gapped_known_ook_has_measured_contrast_and_expected_envelope_periodicity(self):
        rate, baud = 10000, 1000
        all_indices = np.arange(rate, dtype=np.int64)
        time = all_indices / rate
        high = (time >= .2) & (time < .7) & ((np.floor(time * baud).astype(int) % 2) == 1)
        amplitude = np.where(high, 30., 1.)
        iq = np.column_stack((np.round(amplitude * np.cos(2 * np.pi * 1100 * time)),
                              np.round(amplitude * np.sin(2 * np.pi * 1100 * time)))).astype(np.int8)
        valid = (all_indices % 256) < 200
        metrics, _, _, _ = analyze_signal(all_indices[valid], iq[valid], rate, baud)
        self.assertEqual(metrics["valid_IQ_samples"], int(np.sum(valid)))
        self.assertGreater(metrics["peak_window_mean_power_to_baseline_ratio"], 100)
        self.assertGreater(metrics["best_expected_frequency_window"]["r2"], .6)
        self.assertAlmostEqual(metrics["best_scanned_frequency_in_selected_window"]["frequency_hz"], 500, delta=3)
        self.assertEqual(metrics["clipped_IQ_pair_fraction"], 0)

    def test_quantized_zero_baseline_leaves_contrast_and_periodicity_unavailable(self):
        indices = np.arange(1000, dtype=np.int64)
        metrics, _, _, _ = analyze_signal(indices, np.zeros((1000, 2), dtype=np.int8), 10000, 1000)
        self.assertIsNone(metrics["peak_window_mean_power_to_baseline_ratio"])
        self.assertIsNone(metrics["best_expected_frequency_window"])
        self.assertEqual(metrics["above_threshold_valid_samples"], 0)


if __name__ == "__main__":
    unittest.main()
