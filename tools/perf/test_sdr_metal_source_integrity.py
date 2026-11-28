"""Source CRC checkpoints, intended words and phase-zero schedules; no RF."""
import binascii
import copy
import struct
import pathlib
import tempfile
import unittest
from unittest import mock

from sdr_metal_capture import (SourceIntegrityError, expected_source_crcs,
    default_plan, execute_run, validate_tx_source_integrity, validate_tx_transport)
import test_sdr_metal_fast as fast_fixture
import test_sdr_metal_capture as single_fixture
import test_sdr_metal_dual_capture as dual_fixture
from sdr_metal_dual_capture import execute_dual_run
from link_capture import SecretGuard


def source_batch(fast=False, ask=False):
    records, before, after = fast_fixture.fast_records()
    uploaded = b"\x55"  # MSB-first0,1,0,1,0,1,0,1.
    for info in (before, after):
        info.update(source_integrity_supported=True, gate_mask=1 << 18, amplitude_mask=255 << 10)
    after.update(loaded_bits=8, loaded_crc32=binascii.crc32(uploaded) & 0xffffffff)
    result = records[0]
    result.update(initialized_word=(1 << 18) | 0x155, observed_backoff1_word=(1 << 18) | 0x17555)
    result["restored_word_requested"] = result["restored_word_readback"] = result["initialized_word"]
    end = records[-1]
    end.update(source_integrity_required=True, source_integrity_driver_accepted=True, source_integrity_ok=True,
               restored_word_requested=result["initialized_word"], restored_word_readback=result["initialized_word"],
               tone_disable_word_readback=result["initialized_word"] & ~(1 << 18))
    if not fast:
        result.update(mode="ask" if ask else "gate", loop_variant="observed_paced_masked_or_library")
        records.pop(1); end.update(fast_clock_required=False, fast_clock_driver_accepted=None)
    expected = expected_source_crcs(uploaded, result, before)
    record = {"type": "tx_source_integrity", "ok": True, "timed_source_integrity_ok": True, "retained_source_integrity_ok": True,
        "after_radio_checked": True, "after_tone_checked": True, "preloop_checked": True, "loop_started": True,
        "teardown_checked": True, "delta_required": fast, "packed_address": 0x3fc90000, "packed_bytes": 1,
        "words_address": 0x3fc90100, "words_bytes": 32, "deltas_address": 0x3fc90200 if fast else 0,
        "deltas_bytes": 32 if fast else 0, "schedule_populated_after_startup_calibration": fast,
        "schedule_population_scope": "after_all_SDK_startup_and_tone_setup_before_timed_loop",
        "buffer_word_byte_order": "native_little_endian_u32", "packed_bit_order": "MSB_first_per_byte"}
    for stage in ("expected", "before_radio", "after_radio", "after_tone", "before_loop", "after_loop", "after_teardown"):
        record["packed_" + stage + "_crc32"] = expected["packed_crc32"]
    for stage in ("expected", "before_loop", "after_loop", "after_teardown"):
        record["words_" + stage + "_crc32"] = expected["words_crc32"]
    for stage in ("after_build", "before_loop", "after_loop", "after_teardown"):
        record["deltas_" + stage + "_crc32"] = expected["deltas_crc32"] if fast else 0
    records.insert(1, record)
    return records, before, after, uploaded


class SourceIntegrityTests(unittest.TestCase):
    def test_host_word_crc_matches_explicit_gate_ASK_and_partial_bit_vectors(self):
        records, before, _, uploaded = source_batch()
        result = records[0]; base = result["initialized_word"]
        # Explicit intended LE32 levels, independent of the helper's bit loop.
        words = [base & ~(1 << 18), base, base & ~(1 << 18), base,
                 base & ~(1 << 18), base, base & ~(1 << 18), base]
        self.assertEqual(expected_source_crcs(uploaded, result, before)["words_crc32"], binascii.crc32(struct.pack("<8I", *words)) & 0xffffffff)
        result["mode"] = "ask"
        high = (base & ~(255 << 10)) | (result["observed_backoff1_word"] & (255 << 10))
        self.assertEqual(expected_source_crcs(uploaded, result, before)["words_crc32"], binascii.crc32(struct.pack("<8I", *([base, high] * 4))) & 0xffffffff)
        result.update(mode="gate", pattern_bits=9)
        a = expected_source_crcs(b"\x55\x80", result, before); b = expected_source_crcs(b"\x55\xff", result, before)
        self.assertEqual(a["words_crc32"], b["words_crc32"])
        self.assertNotEqual(a["packed_crc32"], b["packed_crc32"])

    def test_valid_gate_ASK_and_fast_batches_require_new_source_receipt(self):
        for fast, ask in ((False, False), (False, True), (True, False)):
            records, before, after, uploaded = source_batch(fast, ask)
            proof = validate_tx_transport(records, before, after, uploaded)
            self.assertEqual(proof["source_integrity"]["status"], "VALIDATED")
            if fast:
                # At7MHz the exact phase-zero8-word delta ring is34,34,34,35,34,34,35,34.
                expected = binascii.crc32(struct.pack("<8I", 34, 34, 34, 35, 34, 34, 35, 34)) & 0xffffffff
                self.assertEqual(proof["source_integrity"]["host_expected"]["deltas_crc32"], expected)

    def test_missing_duplicate_unaccepted_or_bad_order_source_record_rejected(self):
        for mutation in ("missing", "duplicate", "order", "required", "accepted", "terminal_ok"):
            records, before, after, uploaded = source_batch()
            if mutation == "missing": records.pop(1)
            elif mutation == "duplicate": records.insert(1, dict(records[1]))
            elif mutation == "order": records[0], records[1] = records[1], records[0]
            else: records[-1][{"required": "source_integrity_required", "accepted": "source_integrity_driver_accepted", "terminal_ok": "source_integrity_ok"}[mutation]] = False
            with self.subTest(mutation=mutation), self.assertRaises(RuntimeError): validate_tx_transport(records, before, after, uploaded)
        records, before, after, uploaded = source_batch(True)
        records[1], records[2] = records[2], records[1]
        with self.assertRaises(RuntimeError): validate_tx_transport(records, before, after, uploaded)

    def test_postteardown_change_is_failed_without_attributing_RF_BER(self):
        records, before, after, uploaded = source_batch(True)
        source = records[1]; source["words_after_teardown_crc32"] ^= 1
        source.update(ok=False, retained_source_integrity_ok=False)
        records[-1]["source_integrity_ok"] = False
        with self.assertRaises(SourceIntegrityError) as caught: validate_tx_transport(records, before, after, uploaded)
        proof = caught.exception.validation
        self.assertTrue(proof["timed_checkpoint_CRCs_match"])
        self.assertFalse(proof["retained_checkpoint_CRCs_match"])
        self.assertIn("TEARDOWN_ONLY_CHANGE", proof["failure_scope"])
        self.assertIn("no RF BER attribution", str(caught.exception))

    def test_during_loop_change_or_self_consistent_wrong_intended_mapping_is_failed(self):
        records, before, after, uploaded = source_batch()
        records[1]["packed_after_loop_crc32"] ^= 1
        records[1].update(ok=False, timed_source_integrity_ok=False, retained_source_integrity_ok=False)
        with self.assertRaises(SourceIntegrityError) as caught: validate_tx_transport(records, before, after, uploaded)
        self.assertFalse(caught.exception.validation["timed_checkpoint_CRCs_match"])
        records, before, after, uploaded = source_batch()
        for key in ("words_expected_crc32", "words_before_loop_crc32", "words_after_loop_crc32", "words_after_teardown_crc32"): records[1][key] ^= 1
        with self.assertRaises(SourceIntegrityError) as caught: validate_tx_transport(records, before, after, uploaded)
        self.assertTrue(caught.exception.validation["target_reported_timed_integrity"])
        self.assertFalse(caught.exception.validation["timed_checkpoint_CRCs_match"])

    def test_stage_flags_identity_byte_order_addresses_lengths_and_upload_are_not_assumed(self):
        for change in ({"loop_started": False}, {"after_tone_checked": False}, {"packed_bytes": 2},
                       {"words_address": 0x3fc90000}, {"deltas_address": 7}, {"words_address": 7},
                       {"packed_bit_order": "LSB_first"}, {"buffer_word_byte_order": "big_endian"}):
            records, before, after, uploaded = source_batch(); records[1].update(change)
            with self.subTest(change=change), self.assertRaises(RuntimeError): validate_tx_transport(records, before, after, uploaded)
        records, before, after, uploaded = source_batch()
        with self.assertRaises(RuntimeError): validate_tx_transport(records, before, after)
        with self.assertRaises(RuntimeError): validate_tx_transport(records, before, after, b"\xaa")
        records, before, after, uploaded = source_batch(True)
        records[1]["deltas_after_build_crc32"] ^= 1
        records[1]["deltas_before_loop_crc32"] ^= 1; records[1]["deltas_after_loop_crc32"] ^= 1; records[1]["deltas_after_teardown_crc32"] ^= 1
        with self.assertRaises(SourceIntegrityError): validate_tx_transport(records, before, after, uploaded)

    def test_earlier_capability_scope_cannot_silently_gain_source_proof(self):
        from test_sdr_metal_transport import batch, info
        proof = validate_tx_transport(batch(), info(), info(13, 7600))
        self.assertIsNone(proof["source_integrity"])
        records, before, after, uploaded = source_batch()
        del before["source_integrity_supported"]; del after["source_integrity_supported"]
        with self.assertRaises(RuntimeError): validate_tx_transport(records, before, after, uploaded)

    def test_single_and_dual_pass_exact_upload_and_retain_teardown_failure_scope(self):
        for dual in (False, True):
            for teardown_failure in (False, True):
                with self.subTest(dual=dual, teardown_failure=teardown_failure), tempfile.TemporaryDirectory() as tmp:
                    root = pathlib.Path(tmp)
                    batch, before, after, uploaded = source_batch()
                    batch[0].update(backoff0_quarter_db=96, backoff1_quarter_db=112, init_path="experimental_direct_PHY")
                    if teardown_failure:
                        batch[1]["words_after_teardown_crc32"] ^= 1
                        batch[1].update(ok=False, retained_source_integrity_ok=False)
                        batch[-1]["source_integrity_ok"] = False
                    class TX(single_fixture.TXFixture):
                        def __init__(self):
                            super().__init__(); self.records.append(before); self.dispatches = 0
                        def request(self, command, *args, **kwargs):
                            if command == "info": self.records.append(after); return after
                            return super().request(command, *args, **kwargs)
                        def send(self, command, private=False):
                            self.dispatches += 1; super().send(command, private)
                        def read(self, nonblocking=False):
                            if self.pending:
                                self.pending = None; self.records.extend(copy.deepcopy(batch))
                    tx = TX(); plan = default_plan(True); plan["temperature_queries"] = False
                    case = next(c for c in plan["workloads"] if c["tx_enabled"])
                    case.update(tx_mode="gate", baud=7000000, duration_us=1000, mask_segment_us=0, bit_count=8,
                                samples=16, rx_repeats=1, arm_delay_us=500000)
                    run = {"run_id": "d001_1" if dual else "m001_1", "settings": case}
                    if dual:
                        events = []
                        ports = {"tx": tx, "rx": dual_fixture.RX("rx", events, tx), "rx2": dual_fixture.RX("rx2", events, tx)}
                        with mock.patch("sdr_metal_dual_capture.time.perf_counter", dual_fixture.Clock()), mock.patch("sdr_metal_dual_capture.time.sleep"):
                            ok = execute_dual_run(run, ports, plan, root / "plan.json", {n: root / n for n in ("rx", "rx2")}, SecretGuard())
                        self.assertEqual(ok, not teardown_failure)
                        self.assertEqual(run["receivers"]["rx"]["status"], "ACQUIRED")
                    else:
                        rx = single_fixture.RXFixture()
                        if teardown_failure:
                            with self.assertRaises(SourceIntegrityError): execute_run(run, tx, rx, plan, root / "plan.json", root / "case", SecretGuard())
                        else:
                            execute_run(run, tx, rx, plan, root / "plan.json", root / "case", SecretGuard())
                            self.assertEqual(run["status"], "ACQUIRED")
                    self.assertEqual(tx.upload, uploaded)
                    if teardown_failure:
                        self.assertIn("TEARDOWN_ONLY_CHANGE", run["transmitter_source_validation"]["failure_scope"])
                    else:
                        self.assertEqual(run["transmitter_output_validation"]["source_integrity"]["status"], "VALIDATED")


if __name__ == "__main__": unittest.main()
