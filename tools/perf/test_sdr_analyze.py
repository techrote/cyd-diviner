"""Known-source bit mapping and evidence-class guards; no RF measurements."""
import argparse
import array
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from sdr_analyze import digest

from sdr_analyze import analyze, compare_frames, expected_payloads, frame_touches_erasure


class SDRAnalysisTests(unittest.TestCase):
    def test_incomplete_acquisition_is_skipped_without_inventing_sidecars_or_promoting_status(self):
        for status in ("FAILED", "ACQUIRED"):
            with tempfile.TemporaryDirectory() as temporary:
                root = pathlib.Path(temporary)
                capture_folder = root / "raw"
                capture_folder.mkdir()
                source = capture_folder / "results.json"
                capture = {"schema": "cyd-sdr-capture/1", "evidence_class": "physical_iq_acquisition",
                    "status": "FAILED", "identities": {}, "runs": [{"run_id": "s008_1", "workload": "fixture incomplete",
                        "settings": {"baud": 11500}, "status": status, "error": "fixture malformed TX JSON"}]}
                source.write_text(json.dumps(capture))
                (capture_folder / "artifact-hashes.json").write_text(json.dumps({"results.json": digest(source)}))
                before = {path.name: digest(path) for path in capture_folder.iterdir()}
                core = root / "fixture-core"
                core.write_bytes(b"not executed")
                args = argparse.Namespace(results=source, output=root / "analysis", core=core,
                    wsl=False, stream_id=1, timeout=1, stimulus_root=None)
                with mock.patch("sdr_analyze.invoke") as invoke:
                    self.assertEqual(analyze(args), 0)
                    invoke.assert_not_called()
                output = json.loads((args.output / "results.json").read_text())
                self.assertEqual(output["capture_status"], "FAILED")
                self.assertEqual(output["status"], "ANALYSIS COMPLETE")
                self.assertEqual(output["runs"][0]["acquisition_status"], status)
                self.assertEqual(output["runs"][0]["acquisition_error"], "fixture malformed TX JSON")
                self.assertEqual(output["runs"][0]["demodulation_status"], "NOT RUN: INCOMPLETE ACQUISITION")
                self.assertEqual(before, {path.name: digest(path) for path in capture_folder.iterdir()})
                self.assertFalse(any(path.name == "iq-erasures.json" for path in args.output.rglob("*")))

    def fixture(self):
        payload = bytes(range(32))
        reversed_bits = bytes(int(f"{value:08b}"[::-1], 2) for value in payload)
        packed = bytes.fromhex("55555555d391c5a7") + b"\0\0\x01\x31\x01\0" + reversed_bits
        stimulus = {"payload_bits": 256, "frame_symbols": 368, "frame_count": 1, "symbols_total": 368,
                    "source_rate_hz": 2000, "channels": 2, "oversample": 4, "stream_id": 1}
        frame = {"sequence": 0, "first_iq_sample": 100, "payload_bits": 256, "recovered_samples_per_symbol": 10,
                 "channels": 2, "oversample": 4, "stream_id": 1, "source_rate_hz": 2000,
                 "pcm_written": True, "pcm_offset_frames": 0, "payload_hex": payload.hex()}
        reference = array.array("h", range(64))
        return packed, stimulus, frame, reference

    def test_lsb_payload_is_compared_to_msb_uploaded_symbols_without_hiding_bit_errors(self):
        packed, stimulus, frame, reference = self.fixture()
        self.assertEqual(expected_payloads(packed, stimulus)[0], bytes(range(32)))
        damaged = bytearray.fromhex(frame["payload_hex"])
        damaged[5] ^= 0x20
        frame["payload_hex"] = damaged.hex()
        erasures = [{"file_sample_offset": 120, "samples": 10}]
        rows, ideal, actual = compare_frames([frame], reference, packed,
            {"stimulus": stimulus, "pcm": reference}, erasures)
        self.assertEqual(rows[0]["payload_bit_errors"], 1)
        self.assertTrue(rows[0]["touches_host_IQ_erasure"])
        self.assertEqual(ideal, actual)
        self.assertFalse(frame_touches_erasure(frame, [{"file_sample_offset": 5000, "samples": 10}]))

    def test_differing_decoded_config_and_duplicates_cannot_inflate_known_quality(self):
        packed, stimulus, frame, reference = self.fixture()
        rows, ideal, _ = compare_frames([frame, frame], reference * 2, packed,
            {"stimulus": stimulus, "pcm": reference}, [])
        self.assertTrue(rows[1]["duplicate_for_quality"])
        self.assertEqual(len(ideal), 64)
        wrong = {**frame, "channels": 4}
        rows, ideal, _ = compare_frames([wrong], reference, packed,
            {"stimulus": stimulus, "pcm": reference}, [])
        self.assertTrue(rows[0]["source_config_mismatch"])
        self.assertFalse(rows[0]["known_source_sequence"])
        self.assertEqual(len(ideal), 0)

    def test_offline_models_and_open_captures_cannot_be_published_as_measured_rf(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = pathlib.Path(temporary) / "results.json"
            for item in ({"schema": "greenfield_sdr_iq_model/1", "evidence_class": "OFFLINE_MODEL"},
                         {"schema": "cyd-sdr-capture/1", "evidence_class": "physical_iq_acquisition", "status": "IN PROGRESS"}):
                path.write_text(json.dumps(item))
                with self.assertRaises(ValueError):
                    analyze(argparse.Namespace(results=path))


if __name__ == "__main__":
    unittest.main()
