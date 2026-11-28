import json
import pathlib
import tempfile
import unittest

from mission_index import build_index


class IndexTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)

    def capture(self, name, result):
        folder = self.root / name; folder.mkdir()
        (folder / "results.json").write_text(json.dumps(result))

    def test_shared_transmission_is_not_doubled_and_partial_receivers_are_preserved(self):
        self.capture("dual", {"schema": "cyd-sdr-metal-dual-capture/1", "status": "FAILED",
            "identities": {"schema": "cyd-sdr-metal-dual-identities/1", "tx": {"firmware_sha256": "tx", "expected_info": {"source_sha": "A"}},
                           "rx": {"expected_info": {"source_sha": "B"}}},
            "runs": [{"run_id": "one", "status": "ACQUIRED", "settings": {"tx_enabled": True},
                      "receivers": {"rx": {"status": "ACQUIRED"}, "rx2": {"status": "ACQUIRED"}}},
                     {"run_id": "two", "status": "FAILED", "settings": {"tx_enabled": True},
                      "receivers": {"rx": {"status": "ACQUIRED"}, "rx2": {"status": "FAILED"}}}]})
        result, _ = build_index(self.root, {}, "fixed")
        self.assertEqual(result["captures"][0]["complete_shared_sdr_tx_exposures"], 1)
        self.assertEqual(result["captures"][0]["independently_valid_receiver_bodies"], 3)
        self.assertFalse(result["runs"][1]["complete_workload_window"])
        self.assertEqual(result["runs"][1]["status"], "FAILED")
        self.assertEqual(result["runs"][0]["endpoint_identities"]["rx"]["source_sha"], "B")

    def test_unfinished_failed_row_and_diagnostics_are_not_measurements(self):
        self.capture("failed", {"schema": "cyd-perf-capture/1", "status": "FAILED", "source_sha": "source",
            "runs": [{"status": "MEASURED"}, {"status": "IN PROGRESS", "profile_status": "MEASURED"}]})
        self.capture("recovery", {"schema": "cyd-metal-readback-recovery/1", "status": "RECOVERED EXACT ORIGINAL CRC"})
        result, _ = build_index(self.root, {}, "fixed")
        self.assertEqual(sum(r["complete_workload_window"] for r in result["runs"]), 1)
        self.assertTrue(result["runs"][1]["unfinished_raw_row_in_closed_failed_capture"])
        self.assertEqual(result["captures"][1]["complete_shared_sdr_tx_exposures"], 0)
        self.assertTrue(result["captures"][1]["diagnostic_only"])

    def test_logical_cyd_role_keeps_actual_s3_identity(self):
        self.capture("audio", {"schema": "cyd-audio-mvp-capture/1", "status": "FAILED",
            "endpoints": {"cyd": {"identity": {"profile": "kernel-s3-n4r2-audio", "source_sha": "S3"}}},
            "metadata_policy": {"mode": "deferred"}, "runs": [{"status": "MEASURED"}]})
        result, _ = build_index(self.root, {}, "fixed")
        self.assertEqual(result["runs"][0]["endpoint_identities"]["cyd"]["profile"], "kernel-s3-n4r2-audio")
        self.assertEqual(result["runs"][0]["metadata_policy"], {"mode": "deferred"})

    def test_active_or_unknown_capture_cannot_be_published_as_terminal(self):
        self.capture("active", {"schema": "cyd-sdr-metal-dual-capture/1", "status": "IN PROGRESS"})
        with self.assertRaises(ValueError): build_index(self.root, {}, "fixed")
        (self.root / "active/results.json").write_text(json.dumps({"schema": "unknown", "status": "COMPLETE"}))
        with self.assertRaises(ValueError): build_index(self.root, {}, "fixed")


if __name__ == "__main__": unittest.main()
