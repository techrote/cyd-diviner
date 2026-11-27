"""Independent digital fixtures; none is a physical RF/latency measurement."""
import binascii
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

import numpy as np
from sdr_metal_envelope import Options, analyze_payload, load_capture, main, verified_snapshots


def native(i, q, metadata=None):
    metadata = [0] * len(i) if metadata is None else metadata
    return b"".join(struct.pack("<I", (int(a) & 1023) | ((int(b) & 1023) << 10) | ((int(m) & 4095) << 20))
                    for a, b, m in zip(i, q, metadata))


def ook(n=4096, rate=16000000, baud=1000000, phase=.125):
    sample = np.arange(n)
    level = (np.floor(sample * baud / rate + phase).astype(int) & 1) != 0
    amplitude = np.where(level, 200., 5.)
    carrier_phase = 2 * np.pi * 1250000 * sample / rate
    i = np.round(amplitude * np.cos(carrier_phase)).astype(int)
    q = np.round(amplitude * np.sin(carrier_phase)).astype(int)
    return native(i, q, np.arange(n) % 4096)


def fixture(root, payload, status="CAPTURE COMPLETE", mode="capture"):
    run_id = "m001_1"
    folder = root / run_id; folder.mkdir(parents=True)
    raw = folder / "snapshot-0001.native32le"; raw.write_bytes(payload)
    sha = hashlib.sha256(payload).hexdigest(); n = len(payload) // 4
    control = 0x80010000 | n
    header = {"schema": 1, "type": "snapshot", "id": run_id, "repetition": 1 if mode == "capture" else 0,
        "source_id": run_id, "source_repetition": 1 if mode == "capture" else 20,
        "sample_count": n, "raw_bytes": n * 4, "raw_format": "LE32_I10_Q10_metadata12_native",
        "crc32": f"{binascii.crc32(payload) & 0xffffffff:08x}", "nominal_rate_hz": 16000000,
        "complete": True, "hardware_done": True, "registers_restored": True, "tail_canary_ok": True,
        "unchanged_sentinel_words": 0, "owner_before": "00000000", "owner_after": "00000000",
        "lanes_before": "00000000", "lanes_after": "00000000", "control": f"{control:08x}",
        "status": f"{control | (1 << 18):08x}"}
    result = {"schema": "cyd-sdr-metal-capture/1", "evidence_class": "physical_snapshot_acquisition", "status": status,
        "identities": {"fixture_only": True}, "target_info": {"tx": {"cpu_mhz": 240}}, "runs": [{
            "run_id": run_id, "workload": "FIXTURE_NOT_RF", "status": "ACQUIRED",
            "settings": {"samples": n, "rate_hz": 16000000, "rx_mode": mode, "rx_repeats": 1 if mode == "capture" else 20,
                "tx_enabled": True, "pattern": "alternating", "baud": 1000000, "bit_count": 64, "mask_segment_us": 0},
            "transmitter_result": {"processed_bits": 1000000, "elapsed_cycles": 240000000, "skipped_paced_slots": 0},
            "snapshots": [{"file": raw.name, "sha256": sha, "crc_ok": True, "following_lf_ok": True, "header": header}]}]}
    receipt = root / "results.json"
    receipt.write_text(json.dumps(result), encoding="utf-8")
    hashes = {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in [raw, receipt]}
    (root / "artifact-hashes.json").write_text(json.dumps(hashes), encoding="utf-8")
    return receipt, result, hashes


class EnvelopeNumericalTests(unittest.TestCase):
    def test_native_signed10_metadata_and_field_limits(self):
        i, q = [-512, -1, 0, 511], [511, 0, -512, -1]
        payload = native(i, q, [0, 4095, 123, 4094])
        result, _, _, edges = analyze_payload(payload, 16000000)
        self.assertEqual(result["power_mean_code_squared"], sum(a*a+b*b for a, b in zip(i, q)) / 4)
        self.assertEqual(result["metadata12_unique_values"], 4)
        self.assertEqual(result["signed10_field_limit_fraction"], .75)
        self.assertFalse(result["ac_power_spectrum"]["available"])
        self.assertFalse(edges)

    def test_constant_DC_power_is_not_a_modulation_peak(self):
        result, _, _, _ = analyze_payload(native([300]*4096, [-200]*4096), 16000000)
        self.assertEqual(result["power_mean_code_squared"], 130000)
        self.assertEqual(result["power_variance_code4"], 0)
        self.assertIsNone(result["ac_power_spectrum"]["peak_frequency_hz_from_nominal_clock"])
        self.assertFalse(result["edge_qualification"]["qualified"])

    def test_rotating_constant_amplitude_carrier_does_not_become_OOK(self):
        sample = np.arange(4096)
        payload = native(np.round(300*np.cos(2*np.pi*sample/16)), np.round(300*np.sin(2*np.pi*sample/16)))
        result, _, _, edges = analyze_payload(payload, 16000000)
        self.assertLess(result["q90_q10_ratio_with_one_code_squared_denominator_floor"], 1.02)
        self.assertIn("power_quantile_ratio_below_threshold", result["edge_qualification"]["reasons"])
        self.assertFalse(edges)

    def test_carrier_plus_IQ_DC_can_beat_but_is_not_qualified_as_two_level_OOK(self):
        sample = np.arange(4096)
        payload = native(np.round(180+180*np.cos(2*np.pi*sample/16)), np.round(180*np.sin(2*np.pi*sample/16)))
        result, _, _, edges = analyze_payload(payload, 16000000)
        self.assertEqual(result["ac_power_spectrum"]["peak_frequency_hz_from_nominal_clock"], 1000000)
        self.assertIn("robust_level_separation_below_threshold", result["edge_qualification"]["reasons"])
        self.assertFalse(edges)

    def test_known_OOK_fundamental_duty_and_complete_threshold_intervals(self):
        result, freq, psd, edges = analyze_payload(ook(), 16000000)
        spectrum = result["ac_power_spectrum"]
        self.assertEqual(spectrum["peak_frequency_hz_from_nominal_clock"], 500000)
        self.assertEqual(spectrum["bin_spacing_hz_from_nominal_clock"], 16000000/4096)
        self.assertAlmostEqual(spectrum["equivalent_noise_bandwidth_hz_from_nominal_clock"] /
                               spectrum["bin_spacing_hz_from_nominal_clock"], 1.5, places=3)
        self.assertTrue(result["edge_qualification"]["qualified"])
        self.assertAlmostEqual(result["qualified_threshold_duty_estimate"], .5, places=3)
        intervals = result["qualified_threshold_intervals"]
        self.assertEqual(intervals["high_width"]["samples"]["median"], 16)
        self.assertEqual(intervals["low_width"]["samples"]["median"], 16)
        self.assertEqual(intervals["rising_period"]["samples"]["median"], 32)
        self.assertGreater(len(edges), 200)
        self.assertEqual(len(freq), len(psd))

    def test_independent_fractional_source_phases(self):
        for phase in (.03125, .125, .249, .5, .731, .999):
            with self.subTest(phase=phase):
                result, _, _, _ = analyze_payload(ook(phase=phase), 16000000)
                self.assertTrue(result["edge_qualification"]["qualified"])
                self.assertEqual(result["qualified_threshold_intervals"]["rising_period"]["samples"]["median"], 32)

    def test_noise_does_not_get_periodic_edge_statistics(self):
        rng = np.random.default_rng(23063)
        payload = native(np.round(rng.normal(0, 25, 16380)), np.round(rng.normal(0, 25, 16380)))
        result, _, _, edges = analyze_payload(payload, 80000000)
        self.assertIn("AC_spectral_peak_concentration_below_threshold", result["edge_qualification"]["reasons"])
        self.assertIsNone(result["qualified_threshold_intervals"])
        self.assertFalse(edges)

    def test_one_envelope_step_is_not_a_period_measurement(self):
        payload = native([0]*2048+[200]*2048, [0]*4096)
        result, _, _, _ = analyze_payload(payload, 16000000)
        self.assertIn("too_few_AC_peak_cycles_in_snapshot", result["edge_qualification"]["reasons"])
        self.assertIsNone(result["qualified_threshold_duty_estimate"])

    def test_aliased_frequency_references_are_explicit_and_skips_are_not_hidden(self):
        settings = {"tx_enabled": True, "pattern": "alternating", "baud": 20000000, "mask_segment_us": 1, "bit_count": 63}
        tx = {"processed_bits": 20000000, "elapsed_cycles": 240000000, "skipped_paced_slots": 20}
        result, _, _, _ = analyze_payload(ook(baud=20000000), 16000000, settings=settings, tx=tx, cpu_hz=240000000)
        self.assertEqual(result["alternating_references"][0]["alias_hz_from_nominal_RX_clock"], 6000000)
        self.assertEqual(result["ac_power_spectrum"]["peak_frequency_hz_from_nominal_clock"], 6000000)
        self.assertTrue(any("Skipped" in warning for warning in result["warnings"]))
        self.assertTrue(any("Odd" in warning for warning in result["warnings"]))
        self.assertFalse(result["RF_origin_verified"])
        self.assertFalse(result["payload_BER_measured"])

    def test_unpaced_TX_has_no_requested_baud_reference_and_DC_alias_is_labelled(self):
        settings = {"tx_enabled": True, "pattern": "alternating", "baud": 0}
        tx = {"processed_bits": 32000000, "elapsed_cycles": 240000000}
        result, _, _, _ = analyze_payload(native([200]*4096, [0]*4096), 16000000,
            settings=settings, tx=tx, cpu_hz=240000000)
        self.assertEqual(len(result["alternating_references"]), 1)
        self.assertTrue(result["alternating_references"][0]["alias_to_DC"])
        self.assertIsNone(result["ac_power_spectrum"]["peak_frequency_hz_from_nominal_clock"])

    def test_invalid_options_rate_and_partial_words_rejected(self):
        with self.assertRaises(ValueError): analyze_payload(ook(), 123)
        with self.assertRaises(ValueError): analyze_payload(b"123", 16000000)
        with self.assertRaises(ValueError): Options(minimum_ac_peak_fraction=float("nan")).validate()
        with self.assertRaises(ValueError): Options(hysteresis_low_fraction=.9).validate()


class ClosedCaptureIntegrityTests(unittest.TestCase):
    def test_complete_CLI_retains_failed_scope_and_only_last_bench_snapshot(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); raw = root / "physical"; output = root / "analysis"
            receipt, result, hashes = fixture(raw, ook(), status="FAILED", mode="bench")
            main([str(receipt), "--output", str(output), "--write-spectra", "--write-edges"])
            summary = json.loads((output / "results.json").read_text())
            self.assertEqual(summary["capture_status"], "FAILED")
            self.assertEqual(len(summary["runs"][0]["snapshots"]), 1)
            self.assertEqual(summary["runs"][0]["snapshots"][0]["source_header"]["source_repetition"], 20)
            self.assertTrue((output / "manifest.json").is_file())
            self.assertTrue(list(output.rglob("*-power-AC-spectrum.csv")))
            self.assertTrue(list(output.rglob("*-qualified-threshold-edges.csv")))
            for name, sha in hashes.items(): self.assertEqual(hashlib.sha256((raw/name).read_bytes()).hexdigest(), sha)
            with self.assertRaises(FileExistsError): main([str(receipt), "--output", str(output)])

    def test_changed_raw_or_active_source_never_creates_analysis(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); receipt, _, _ = fixture(root / "raw", ook())
            raw = receipt.parent / "m001_1/snapshot-0001.native32le"; raw.write_bytes(raw.read_bytes()[:-4])
            with self.assertRaisesRegex(ValueError, "hash mismatch"):
                main([str(receipt), "--output", str(root / "analysis")])
            self.assertFalse((root / "analysis").exists())
        with tempfile.TemporaryDirectory() as tmp:
            receipt, _, _ = fixture(Path(tmp), ook(), status="IN PROGRESS")
            with self.assertRaisesRegex(ValueError, "Closed"): load_capture(receipt)

    def test_CRC_owner_completion_count_and_identity_checked_independently_of_hash(self):
        for field, value in (("crc32", "00000000"), ("owner_after", "9"), ("tail_canary_ok", False),
                             ("status", "80010000"), ("control", "80010001"), ("control", "80001000"), ("source_id", "old")):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp); receipt, result, hashes = fixture(root, ook())
                result["runs"][0]["snapshots"][0]["header"][field] = value
                # Exercise independently of manifest hashing: even a metadata
                # change blessed with a new manifest must pass hardware/CRC proof.
                with self.assertRaises(ValueError): list(verified_snapshots(root, result, hashes))

    def test_acquired_capture_cannot_duplicate_or_omit_snapshots(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); _, result, hashes = fixture(root, ook())
            result["runs"][0]["snapshots"] *= 2
            with self.assertRaisesRegex(ValueError, "snapshot count/files"):
                list(verified_snapshots(root, result, hashes))

    def test_output_cannot_mutate_capture_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); receipt, _, _ = fixture(root, ook())
            with self.assertRaisesRegex(ValueError, "outside"):
                main([str(receipt), "--output", str(root / "analysis")])


if __name__ == "__main__": unittest.main()
