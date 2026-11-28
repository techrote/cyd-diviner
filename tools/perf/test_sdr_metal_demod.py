"""Native conversion and declared trial policy fixtures; no RF or serial access."""
import argparse
import array
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

try:
    import numpy as np
except ImportError:
    np = None

if np is not None:
    from sdr_metal_demod import (analyze, candidate_centres, digest, native_i8,
        received_quality, select_trial, trial_score)
    from test_sdr_metal_envelope import fixture, native, ook
    import test_sdr_analyze as source_fixture


@unittest.skipIf(np is None, "Optional NumPy analysis dependency unavailable")
class MetalDemodTests(unittest.TestCase):
    def test_signed_native_shift_is_exact_and_preserves_native_q_sign(self):
        data = native([-512, -1, 0, 511], [511, 0, -1, -512], [0, 4095, 7, 101])
        out, receipt = native_i8(data)
        self.assertEqual(np.frombuffer(out, dtype=np.int8).tolist(), [-128, 127, -1, 0, 0, -1, 127, -128])
        self.assertEqual(receipt["metadata12_unique_values"], 4)

    def test_clock_candidates_are_declared_and_finite_not_payload_adapted(self):
        settings = {"baud": 0}; tx = {"processed_bits": 1000000, "elapsed_cycles": 240000000}
        rows = candidate_centres(settings, tx, 240, [1, 1.004, 1.008])
        self.assertEqual([row["symbol_rate"] for row in rows], [1000000, 1004000, 1008000])
        self.assertEqual(candidate_centres({"baud": 360000}, tx, 240, [1, 2])[0]["symbol_rate"], 360000)
        for values in ([0], [-1], [float("nan")], [float("inf")]):
            with self.subTest(values=values), self.assertRaises(ValueError): candidate_centres(settings, tx, 240, values)

    def test_selection_uses_header_config_count_then_training_only_and_ties_are_stable(self):
        stimulus = {"source_rate_hz": 2000, "channels": 2, "oversample": 4, "payload_bits": 256, "stream_id": 1}
        frame = {**stimulus, "header_crc_valid": True, "training_fit_error": .2, "payload_hex": "ff" * 32}
        wrong = {**frame, "channels": 4, "training_fit_error": 0}
        self.assertEqual(trial_score([frame, wrong], stimulus)["matching_header_config_frames"], 1)
        scores = [dict(trial_score([frame], stimulus), payload_bit_errors=0),
                  dict(trial_score([frame, {**frame, "training_fit_error": .1}], stimulus), payload_bit_errors=99999),
                  dict(trial_score([frame, {**frame, "training_fit_error": .1}], stimulus), payload_bit_errors=0)]
        self.assertEqual(select_trial(scores), 1)
        self.assertIsNone(select_trial([trial_score([], stimulus)]))
        with self.assertRaises(ValueError): trial_score([{**frame, "training_fit_error": float("nan")}], stimulus)

    def test_ring_oracle_keeps_all_bit_errors_and_deduplicates_quality_only(self):
        packed, stimulus, frame, reference = source_fixture.SDRAnalysisTests().fixture()
        damaged = bytearray.fromhex(frame["payload_hex"]); damaged[1] ^= 4
        frame["payload_hex"] = damaged.hex()
        frames, quality = received_quality([frame, frame], reference * 2,
            {"packed": packed, "metadata": stimulus, "reference": reference, "boxcar": reference})
        self.assertEqual(quality["all_known_frames_BER_counts"]["payload_bit_errors"], 2)
        self.assertEqual(quality["unique_known_sequences_BER_counts"]["payload_bit_errors"], 1)
        self.assertTrue(frames[1]["duplicate_for_quality"])
        self.assertTrue(quality["vs_generated_ideal_boxcar"][0]["perfect_sample_match"])

    def setup_capture(self, root):
        path, result, hashes = fixture(root / "physical", ook(), status="FAILED")
        packed, stimulus, frame, reference = source_fixture.SDRAnalysisTests().fixture()
        meta = {**stimulus, "schema": "greenfield_sdr_emit/1", "evidence_class": "GENERATED_STIMULUS", "no_RF_measurement": True,
                "packed_bytes": len(packed), "pcm_bytes": len(reference) * 2}
        source = root / "stimuli"; source.mkdir()
        files = {}
        for suffix, data in (("-symbols.bits", packed), ("-reference.pcm", reference.tobytes()), ("-boxcar.pcm", reference.tobytes())):
            p = source / ("fixture" + suffix); p.write_bytes(data); files[p.name] = digest(p)
        manifest = source / "manifest.json"
        manifest.write_text(json.dumps({"schema": "cyd-sdr-metal-stimuli/1", "records": [{"prefix": "fixture", "metadata": meta, "files": files}]}))
        uploaded = path.parent / "m001_1/uploaded-bits.bin"; uploaded.write_bytes(packed)
        result["runs"][0]["uploaded_bits_sha256"] = digest(uploaded)
        result["runs"][0]["settings"]["baud"] = 0
        path.write_text(json.dumps(result)); hashes.update({"results.json": digest(path), "m001_1/uploaded-bits.bin": digest(uploaded)})
        (path.parent / "artifact-hashes.json").write_text(json.dumps(hashes))
        core = root / "fixture-core"; core.write_bytes(b"not executed")
        args = argparse.Namespace(results=path, output=root / "analysis", stimulus_manifest=manifest, core=core,
            core_sha256=digest(core), wsl=False, unpaced_multipliers=[1, 1.004, 1.008], timeout=2)
        return args, frame, reference

    def test_orchestration_retains_three_trials_and_failed_capture_scope_without_wav(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); args, frame, reference = self.setup_capture(root)
            before = {p.relative_to(args.results.parent).as_posix(): digest(p) for p in args.results.parent.rglob("*") if p.is_file()}
            calls = []
            def core(_binary, _wsl, arguments, folder, _timeout):
                calls.append(arguments)
                index = len(calls) - 1
                frames = [] if index == 0 else [{**frame, "schema": "greenfield_sdr_frame/1", "header_crc_valid": True,
                    "payload_crc_present": False, "training_fit_error": .2 if index == 1 else .1}]
                samples = reference if frames else array.array("h")
                Path(arguments[4]).write_bytes(samples.tobytes())
                Path(arguments[5]).write_text("".join(json.dumps(f) + "\n" for f in frames))
                receipt = {"schema": "greenfield_sdr_demod/1", "evidence_class": "HOST_DEMODULATION", "input_iq_samples": 4096,
                    "sample_rate_hz": float(arguments[2]), "requested_symbol_rate": float(arguments[3]), "stream_id": 1,
                    "frames_decoded": len(frames), "pcm_bytes": len(samples) * 2}
                (folder / "core-stdout.json").write_text(json.dumps(receipt)); (folder / "core-stderr.log").write_bytes(b"")
                return (0 if frames else 3), receipt, ["fixture-no-RF"]
            with mock.patch("sdr_metal_demod.invoke", side_effect=core): self.assertEqual(analyze(args), 0)
            result = json.loads((args.output / "results.json").read_text())
            self.assertEqual(result["capture_status"], "FAILED")
            snap = result["runs"][0]["snapshots"][0]
            self.assertEqual(len(snap["trials"]), 3)
            self.assertEqual(snap["selected_trial_index"], 2)
            self.assertEqual(len(calls), 3)
            self.assertFalse(list(args.output.rglob("*.wav")))
            self.assertEqual(before, {p.relative_to(args.results.parent).as_posix(): digest(p) for p in args.results.parent.rglob("*") if p.is_file()})
            for name, sha in json.loads((args.output / "manifest.json").read_text())["outputs"].items(): self.assertEqual(digest(args.output / name), sha)

    def test_source_hash_or_inside_input_output_is_rejected_before_core_or_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            args, _, _ = self.setup_capture(Path(temporary)); args.output = args.results.parent / "analysis"
            with self.assertRaisesRegex(ValueError, "outside"): analyze(args)
            args.output = Path(temporary) / "analysis"; args.core_sha256 = "0" * 64
            with self.assertRaisesRegex(ValueError, "ELF digest"): analyze(args)
            self.assertFalse(args.output.exists())


if __name__ == "__main__": unittest.main()
