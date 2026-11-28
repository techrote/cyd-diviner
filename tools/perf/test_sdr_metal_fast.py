"""Independent literal rational-clock receipt and transport fixtures; no RF."""
import copy
import unittest

from sdr_metal_capture import (default_plan, require_fast_capability, validate_plan,
                               validate_tx_fast_clock, validate_tx_transport)
from test_sdr_metal_transport import info, terminal


def fast_records():
    before = {**info(), "cpu_mhz": 240, "fast_paced_supported": True,
              "fast_schedule_helpers_digest": "4" * 64, "fast_schedule_bytes_reserved": 0}
    after = {**info(13, 7600), "cpu_mhz": 240, "fast_paced_supported": True,
             "fast_schedule_helpers_digest": "4" * 64, "fast_schedule_bytes_reserved": 32}
    # Literal example:8 source words/ring at7MHz vs240MHz,6998 words
    # completed before finite1ms.874 full rings leave phase5,000,000;
    # partial6 adds205 cycles. Schedule239930 is one cycle before floor239931.
    result = {"type": "tx_result", "mode": "fast", "loop_variant": "fast_rational_DRAM_paced",
        "requested_baud": 7000000, "processed_bits": 6998, "requested_duration_us": 1000,
        "pattern_bits": 8, "elapsed_cycles": 240001, "unpaced": False, "backend_available": True,
        "requested_mask_segment_us": 0, "trace_stride": 0, "trace_count": 0, "segment_records": 0,
        "skipped_paced_slots": 0, "late_writes": None, "late_cycles_max": None,
        "achieved_writes_per_s": 6998 * 240000000 / 240001, "mean_loop_cycles_per_write": 240001 / 6998,
        "restored_word_requested": 32, "restored_word_readback": 32, "teardown_status": 0,
        "wifi_driver_owned_after": False, "default_event_loop_owned_after": False,
        "direct_power_domain_owned_after": False, "radio_stop_api": "esp_phy_disable_void"}
    clock = {"type": "tx_fast_clock", "requested_baud": 7000000, "target_duration_cycles": 240000,
        "source_words_completed": 6998, "ideal_words_by_target_end": 7000, "target_tail_words_not_transmitted": 2,
        "target_words_ahead_due_rounding": 0, "schedule_next_start_cycles": 239930,
        "ideal_next_start_cycles_floor": 239931, "end_clock_error_cycles": 70, "end_ideal_backlog_cycles": 70,
        "ring_length_symbols": 8, "ring_cycles_floor": 274, "ring_remainder": 2000000, "ring_phase_at_end": 5000000,
        "precomputed_delta_min_cycles": 34, "precomputed_delta_max_cycles": 35, "ring_boundary_extra_cycles_max": 1,
        "schedule_bytes_reserved": 32, "schedule_memory": "internal_8bit_DRAM", "floor_reference_early_cycles_max": 1,
        "real_reference_early_cycles_strict_upper_bound": 2, "per_word_lateness_observed": False,
        "max_transient_backlog_cycles": None}
    end = {**terminal(), "fast_clock_required": True, "fast_clock_driver_accepted": True}
    return [result, clock, end], before, after


class FastReceiptTests(unittest.TestCase):
    def test_fractional_ring_carry_and_unsent_tail_are_valid_measurements(self):
        records, before, after = fast_records()
        result = validate_tx_transport(records, before, after)
        self.assertEqual(result["fast_clock"]["status"], "VALIDATED")
        self.assertEqual(result["fast_clock"]["target_tail_words_not_transmitted"], 2)
        self.assertEqual(result["fast_clock"]["floor_reference_early_cycles_at_end"], 1)

    def test_negative_endpoint_error_can_be_normal_final_wait(self):
        records, before, after = fast_records()
        records[0].update(requested_baud=1104001, processed_bits=1105, elapsed_cycles=240010,
            achieved_writes_per_s=1105 * 240000000 / 240010, mean_loop_cycles_per_write=240010 / 1105)
        records[1].update(requested_baud=1104001, source_words_completed=1105, ideal_words_by_target_end=1105,
            target_tail_words_not_transmitted=0, schedule_next_start_cycles=240216, ideal_next_start_cycles_floor=240217,
            end_clock_error_cycles=-207, end_ideal_backlog_cycles=0, ring_cycles_floor=1739, ring_remainder=142261,
            ring_phase_at_end=864001, precomputed_delta_min_cycles=217, precomputed_delta_max_cycles=218)
        self.assertEqual(validate_tx_transport(records, before, after)["fast_clock"]["status"], "VALIDATED")

    def test_fast_mode_plan_cannot_enable_mask_trace_or_zero_rate(self):
        plan = default_plan(with_tx=True)
        case = next(c for c in plan["workloads"] if c["tx_enabled"])
        case.update(tx_mode="fast", baud=1104000, mask_segment_us=0)
        plan["workloads"] = [case]; validate_plan(plan)
        for changed in (("baud", 0), ("mask_segment_us", 1), ("observe", 1)):
            other = copy.deepcopy(plan)
            if changed[0] == "observe": other["transmitter"]["trace_stride"] = changed[1]
            else: other["workloads"][0][changed[0]] = changed[1]
            with self.subTest(changed=changed), self.assertRaises(ValueError): validate_plan(other)

    def test_unknown_classic_or_missing_fast_capability_is_rejected_before_dispatch(self):
        _, before, _ = fast_records(); require_fast_capability(before)
        for change in ({"profile": "classic-sdr-metal-tx"}, {"fast_paced_supported": False},
                       {"cpu_mhz": 160}, {"fast_schedule_helpers_digest": "missing"}, {"output_line_capacity_bytes": 2048}):
            with self.subTest(change=change), self.assertRaises(RuntimeError): require_fast_capability({**before, **change})
        with self.assertRaises(RuntimeError): require_fast_capability(None)

    def test_fast_clock_missing_duplicate_out_of_order_or_unaccepted_is_not_complete(self):
        for mutation in ("missing", "duplicate", "order", "required_false", "accepted_false"):
            records, before, after = fast_records()
            if mutation == "missing": records.pop(1)
            elif mutation == "duplicate": records.insert(1, dict(records[1]))
            elif mutation == "order": records[0], records[1] = records[1], records[0]
            elif mutation == "required_false": records[-1]["fast_clock_required"] = False
            else: records[-1]["fast_clock_driver_accepted"] = False
            with self.subTest(mutation=mutation), self.assertRaises(RuntimeError): validate_tx_transport(records, before, after)

    def test_schedule_count_rate_phase_rounding_and_memory_corruptions_reject(self):
        changes = ({"source_words_completed": 6999}, {"requested_baud": 7000001}, {"target_duration_cycles": 240001},
            {"target_tail_words_not_transmitted": 0}, {"ring_remainder": 0}, {"ring_phase_at_end": 0},
            {"schedule_next_start_cycles": 239931}, {"ideal_next_start_cycles_floor": 239932},
            {"end_clock_error_cycles": 69}, {"precomputed_delta_max_cycles": 34}, {"schedule_bytes_reserved": 28},
            {"floor_reference_early_cycles_max": True}, {"per_word_lateness_observed": True}, {"max_transient_backlog_cycles": 0})
        for change in changes:
            records, before, after = fast_records(); records[1].update(change)
            with self.subTest(change=change), self.assertRaises(RuntimeError): validate_tx_transport(records, before, after)
        for change in ({"skipped_paced_slots": 1}, {"late_writes": 0}, {"unpaced": True},
                       {"trace_stride": 1}, {"requested_mask_segment_us": 1}, {"achieved_writes_per_s": 1}):
            records, before, after = fast_records(); records[0].update(change)
            with self.subTest(change=change), self.assertRaises(RuntimeError): validate_tx_transport(records, before, after)

    def test_legacy_nonfast_batch_retains_scope_and_rejects_stale_fast_record(self):
        from test_sdr_metal_transport import batch
        self.assertEqual(validate_tx_transport(batch(), info(), info(13, 7600))["status"], "VALIDATED")
        records, before, after = fast_records(); records[0]["mode"] = "gate"
        with self.assertRaises(RuntimeError): validate_tx_transport(records, before, after)
        records, _, _ = fast_records()
        with self.assertRaises(RuntimeError): validate_tx_transport(records, {"type": "info"}, {"type": "info"})


if __name__ == "__main__": unittest.main()
