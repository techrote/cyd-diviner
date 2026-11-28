"""New deferred TX output proof fixtures; no serial or RF access."""
import copy
import pathlib
import tempfile
import unittest
from unittest import mock

from link_capture import SecretGuard
from sdr_metal_capture import (TX_OUTPUT_TRANSPORT, TX_OUTPUT_U32, TX_OUTPUT_U64, default_plan,
                               execute_run, tx_output_transport_info, validate_tx_transport)
from test_link_mission import Clock
from test_sdr_metal_capture import RXFixture, TXFixture


def stats(lines, byte_count):
    result = {k: 0 for k in TX_OUTPUT_U32 + TX_OUTPUT_U64}
    result.update(output_lines_attempted=lines, output_lines_accepted=lines, output_write_calls=lines,
                  output_bytes_requested=byte_count, output_bytes_accepted=byte_count,
                  output_resync_pending=False)
    return result


def info(lines=5, byte_count=1000):
    return {"type": "info", "profile": "s3-sdr-metal-tx", "source_sha": "1" * 40,
            "tx_source_digest": "2" * 64, "word_helpers_digest": "3" * 64, "mac": "123456789abc",
            "output_transport": TX_OUTPUT_TRANSPORT, "output_line_capacity_bytes": 4096,
            "output_line_timeout_us": 5000000, "gate_mask": 1 << 26, **stats(lines, byte_count)}


def terminal(traces=0, segments=0):
    return {"type": "tx_end", "trace_records": traces, "segment_records": segments,
            "tx_result_driver_accepted": True, "trace_records_driver_accepted": traces,
            "segment_records_driver_accepted": segments, "all_prior_records_driver_accepted": True,
            "prior_output_drained": True, "backend_available": True, "restored_word_requested": 32,
            "restored_word_readback": 32, "restored_word_matches": True,
            "tone_disable_requested": True, "continuous_tone_disable_requested": True,
            "tone_disable_word_readback": 32, "tone_enable_bit_after_stop_request": False,
            "teardown_status": 0, "radio_ready_after_teardown": False, "wifi_driver_owned_after": False,
            "default_event_loop_owned_after": False, "direct_power_domain_owned_after": False,
            **stats(12, 6000)}


def batch(traces=2, segments=1):
    result = {"type": "tx_result", "trace_count": traces, "segment_records": segments,
              "restored_word_requested": 32, "restored_word_readback": 32, "teardown_status": 0,
              "wifi_driver_owned_after": False, "default_event_loop_owned_after": False,
              "direct_power_domain_owned_after": False, "radio_stop_api": "esp_phy_disable_void"}
    return [result, *({"type": "tx_trace", "symbol": n} for n in range(traces)),
            *({"type": "tx_segment", "index": n} for n in range(segments)), terminal(traces, segments)]


class TXOutputProofTests(unittest.TestCase):
    def validate(self, records=None, before=None, after=None):
        return validate_tx_transport(batch() if records is None else records,
                                     info() if before is None else before,
                                     info(13, 7600) if after is None else after)

    def test_legacy_receipts_keep_their_original_validation_scope(self):
        old = [{"type": "tx_result"}, {"type": "tx_end", "trace_records": 0, "segment_records": 0}]
        self.assertEqual(validate_tx_transport(old, {"type": "info", "protocol": 2})["status"], "NOT_APPLICABLE")

    def test_complete_batch_and_successful_short_zero_retries_are_valid(self):
        self.assertEqual(self.validate()["status"], "VALIDATED")
        records, before, after = batch(), info(), info(13, 7600)
        records[-1].update(output_write_calls=40, output_short_writes=7, output_zero_writes=3)
        after.update(output_write_calls=41, output_short_writes=7, output_zero_writes=3)
        self.assertEqual(self.validate(records, before, after)["status"], "VALIDATED")

    def test_new_receipts_cannot_downgrade_to_legacy_without_capability(self):
        with self.assertRaisesRegex(RuntimeError, "capability"):
            validate_tx_transport(batch(), {"type": "info"}, info(13, 7600))
        with self.assertRaisesRegex(RuntimeError, "disappeared"):
            tx_output_transport_info([info(), {"type": "info"}])
        self.assertEqual(tx_output_transport_info([info()]), info())

    def test_post_terminal_info_is_required_and_identity_must_match(self):
        with self.assertRaisesRegex(RuntimeError, "post-terminal"):
            validate_tx_transport(batch(), info())
        with self.assertRaisesRegex(RuntimeError, "identity"):
            self.validate(after={**info(13, 7600), "tx_source_digest": "4" * 64})

    def test_missing_or_false_new_acceptance_fields_fail(self):
        for key in ("tx_result_driver_accepted", "all_prior_records_driver_accepted", "prior_output_drained",
                    "restored_word_matches", "tone_disable_requested", "continuous_tone_disable_requested"):
            for value in (None, False):
                with self.subTest(key=key, value=value), self.assertRaises(RuntimeError):
                    records = batch(); records[-1][key] = value; self.validate(records)

    def test_counts_types_order_and_missing_host_records_fail(self):
        changes = [{"trace_records_driver_accepted": 1}, {"segment_records_driver_accepted": True},
                   {"trace_records": 3}, {"segment_records_driver_accepted": None}]
        for change in changes:
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                records = batch(); records[-1].update(change); self.validate(records)
        for mutate in (lambda r: r.pop(1), lambda r: r.reverse(), lambda r: r.append(copy.deepcopy(r[-1]))):
            with self.subTest(mutate=mutate), self.assertRaises(RuntimeError):
                records = batch(); mutate(records); self.validate(records)

    def test_output_failure_missing_and_pending_counters_fail(self):
        for key in ("output_lines_failed", "output_format_failures", "output_write_errors", "output_flush_failures"):
            for location in ("before", "terminal", "after"):
                with self.subTest(key=key, location=location), self.assertRaises(RuntimeError):
                    records, before, after = batch(), info(), info(13, 7600)
                    target = before if location == "before" else after if location == "after" else records[-1]
                    target[key] = 1; self.validate(records, before, after)
        for key in TX_OUTPUT_U32 + TX_OUTPUT_U64 + ("output_resync_pending",):
            with self.subTest(missing=key), self.assertRaises(RuntimeError):
                records = batch(); del records[-1][key]; self.validate(records)
        for change in ({"output_resync_pending": True}, {"output_bytes_accepted": 5999},
                       {"output_lines_accepted": 11}, {"output_short_writes": 20}, {"output_zero_writes": 1}):
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                records = batch(); records[-1].update(change); self.validate(records)

    def test_post_terminal_final_flush_failure_and_counter_reset_fail(self):
        with self.assertRaisesRegex(RuntimeError, "after"):
            self.validate(after={**info(13, 7600), "output_flush_failures": 1})
        with self.assertRaisesRegex(RuntimeError, "regressed"):
            self.validate(after=info(11, 5500))
        with self.assertRaisesRegex(RuntimeError, "progress"):
            self.validate(after=info(12, 6000))

    def test_stop_proof_and_cleanup_are_both_required(self):
        for change in ({"radio_ready_after_teardown": True}, {"tone_enable_bit_after_stop_request": True},
                       {"tone_disable_word_readback": (1 << 26) | 32}, {"restored_word_readback": 33},
                       {"teardown_status": 1}, {"wifi_driver_owned_after": True},
                       {"default_event_loop_owned_after": None}, {"direct_power_domain_owned_after": True}):
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                records = batch(); records[-1].update(change); self.validate(records)


class NewTXFixture(TXFixture):
    def __init__(self, flush_failure=False):
        super().__init__(); self.records.append(info()); self.flush_failure = flush_failure
        self.identity["expected_info"]["profile"] = "s3-sdr-metal-tx"

    def request(self, command, *args, **kwargs):
        if command == "temperature":
            self.sent.append(command)
            return {"type": "temperature", "temperature_c_driver": 25, "install_status": 0,
                    "enable_status": 0, "read_status": 0, "disable_status": 0, "uninstall_status": 0}
        if command == "info":
            self.sent.append(command)
            record = info(13, 7600)
            if self.flush_failure: record["output_flush_failures"] = 1
            self.records.append(record); return record
        return super().request(command, *args, **kwargs)

    def read(self, nonblocking=False):
        pending = self.pending
        super().read(nonblocking)
        if pending: self.records[-1] = terminal()


class NewTransportOrchestrationTests(unittest.TestCase):
    def test_execute_queries_final_info_after_terminal_and_retains_proof(self):
        for flush_failure in (False, True):
            plan = default_plan(with_tx=True)
            case = next(c for c in plan["workloads"] if c["tx_enabled"] and c["tx_mode"] == "gate" and not c["baud"] and not c["mask_segment_us"])
            run = {"run_id": "m001_1", "settings": {**case, "samples": 16, "rx_repeats": 1}}
            tx, rx = NewTXFixture(flush_failure), RXFixture()
            with tempfile.TemporaryDirectory() as tmp, mock.patch("sdr_metal_capture.time.perf_counter", Clock()):
                root = pathlib.Path(tmp)
                if flush_failure:
                    with self.assertRaisesRegex(RuntimeError, "after"):
                        execute_run(run, tx, rx, plan, root / "plan.json", root / "case", SecretGuard())
                    self.assertNotEqual(run.get("status"), "ACQUIRED")
                else:
                    execute_run(run, tx, rx, plan, root / "plan.json", root / "case", SecretGuard())
                    self.assertEqual(run["status"], "ACQUIRED")
                    self.assertEqual(run["transmitter_output_validation"]["status"], "VALIDATED")
                self.assertEqual(tx.sent.count("info"), 1)
                self.assertIn("transmitter_output_after", run)


if __name__ == '__main__': unittest.main()
