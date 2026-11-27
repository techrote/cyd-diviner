"""Native binary framing and finite metal orchestration fixtures, no hardware."""
import binascii
import copy
import json
import pathlib
import struct
import tempfile
import unittest
from unittest import mock

from link_capture import SecretGuard
from sdr_metal_capture import SnapshotDecoder, default_plan, execute_run, validate_plan, validate_snapshot
from test_link_mission import Clock


def wire(record): return b"METAL " + json.dumps({"schema": 1, **record}).encode() + b"\n"


def snapshot(run_id="m001_1", request_id=None, rep=1, source_rep=1, count=16):
    payload = (b"METAL \n\0" * ((count * 4 + 7) // 8))[:count * 4]
    record = {"schema": 1, "type": "snapshot", "id": request_id or run_id, "repetition": rep,
        "source_id": run_id, "source_repetition": source_rep, "complete": True, "hardware_done": True,
        "sample_count": count, "raw_bytes": count * 4, "raw_format": "LE32_I10_Q10_metadata12_native",
        "crc32": f"{binascii.crc32(payload) & 0xffffffff:08x}", "nominal_rate_hz": 16000000,
        "trigger_before_us": 1000, "done_observed_us": 1004, "capture_cycles_mod32": 960,
        "registers_restored": True, "tail_canary_ok": True, "unchanged_sentinel_words": 0,
        "owner_before": "4", "owner_after": "4", "lanes_before": "3", "lanes_after": "3"}
    return record, payload


class SnapshotFramingTests(unittest.TestCase):
    def test_every_split_uses_exact_binary_length_even_for_embedded_text(self):
        record, payload = snapshot()
        stream = wire(record) + payload + b"\n" + wire({"type": "run_end", "id": "m001_1", "ok": True})
        for split in range(1, len(stream)):
            decoded = SnapshotDecoder()
            decoded.feed(stream[:split]); decoded.feed(stream[split:])
            self.assertEqual(len(decoded.snapshots), 1)
            self.assertTrue(decoded.snapshots[0]["crc_ok"])
            self.assertEqual([r["type"] for r in decoded.records], ["snapshot", "run_end"])
            self.assertIsNone(decoded.binary_header)

    def test_bad_crc_or_missing_lf_is_retained_and_never_reinterpreted_as_json(self):
        record, payload = snapshot()
        kept = []
        decoder = SnapshotDecoder(lambda receipt, data: kept.append((receipt, data)))
        with self.assertRaisesRegex(ValueError, "CRC32"):
            decoder.feed(wire(record) + bytes([payload[0] ^ 1]) + payload[1:] + b"\n")
        self.assertFalse(kept[0][0]["crc_ok"])
        with self.assertRaisesRegex(ValueError, "following LF"):
            SnapshotDecoder().feed(wire(record) + payload + b"X")
        partial = SnapshotDecoder(); partial.feed(wire(record) + payload[:-1])
        self.assertEqual(partial.snapshots, [])
        self.assertIsNotNone(partial.binary_header)

    def test_canary_restore_and_source_ids_are_not_assumed_from_crc(self):
        header, _ = snapshot()
        receipt = {"header": header, "crc_ok": True, "following_lf_ok": True}
        case = {"samples": 16, "rate_hz": 16000000, "rx_repeats": 1}
        validate_snapshot(receipt, "m001_1", case)
        for change in ({"source_id": "old"}, {"tail_canary_ok": False}, {"owner_after": "9"}, {"source_repetition": 0}):
            invalid = copy.deepcopy(receipt); invalid["header"].update(change)
            with self.assertRaises(RuntimeError): validate_snapshot(invalid, "m001_1", case)
        plan = default_plan(with_tx=True); validate_plan(plan)
        plan["workloads"][-1]["mask_segment_us"] = 1
        with self.assertRaisesRegex(ValueError, "capacity"): validate_plan(plan)


class TXFixture:
    def __init__(self):
        self.records, self.sent, self.pending = [], [], None
        self.identity = {"expected_info": {"profile": "classic-sdr-metal-tx"}}
    def request(self, command, *_args, **kwargs):
        self.sent.append(command.split()[0])
        if command == "info": return {"type": "info", "temperature_raw": 128}
        if command.startswith("permit "): assert kwargs["private"]; return {"type": "permit", "ok": True}
        return {"type": "ready"}
    def write(self, payload): self.upload = payload
    def log(self, *_args, **_kwargs): pass
    def until(self, *_args, **_kwargs): return {"type": "loaded", "bits": len(self.upload) * 8}
    def send(self, command, private=False): assert private; self.pending = command
    def read(self, nonblocking=False):
        assert nonblocking
        if self.pending:
            self.pending = None
            self.records.extend([{"type": "tx_result", "backend_available": True, "mode": "gate", "requested_baud": 0,
                "requested_duration_us": 200000, "requested_mask_segment_us": 0, "backoff0_quarter_db": 96,
                "backoff1_quarter_db": 112, "unpaced": True, "trace_stride": 0, "init_path": "experimental_direct_PHY",
                "pattern_bits": 64, "processed_bits": 1000, "trace_count": 0, "segment_records": 0,
                "restored_word_readback": 32, "restored_word_requested": 32, "radio_stop_api": "esp_phy_disable_void"},
                {"type": "tx_end", "trace_records": 0, "segment_records": 0}])


class RXFixture:
    def __init__(self): self.decoder, self.stage, self.sent = SnapshotDecoder(), None, []
    @property
    def records(self): return self.decoder.records
    def request(self, command, *_args, **_kwargs):
        parts = command.split(); self.case_mode, self.run_id = parts[1], parts[2]
        self.count, self.repeats = int(parts[4]), int(parts[5])
        record = {"type": "armed", "id": self.run_id, "mode": self.case_mode, "sample_count": self.count,
            "nominal_rate_hz": int(parts[3]), "repeats_requested": self.repeats, "arm_delay_us": int(parts[6]),
            "inter_snapshot_gap_us": int(parts[7]), "timeout_us": int(parts[8])}
        self.decoder.feed(wire(record)); self.stage = "run"; return self.records[-1]
    def send(self, command): self.sent.append(command); self.stage = "read"; self.read_id = command.split()[2]
    def read(self):
        if self.stage == "run":
            self.stage = None
            if self.case_mode == "capture":
                header, payload = snapshot(self.run_id, count=self.count)
                self.decoder.feed(wire(header) + payload + b"\n")
            self.decoder.feed(wire({"type": "run_end", "id": self.run_id, "mode": self.case_mode, "ok": True,
                "captures_completed": self.repeats, "sample_count_each": self.count, "nominal_rate_hz": 16000000,
                "last_owner_restored": True, "last_canary_ok": True, "transfer_failures_delta": 0}))
        elif self.stage == "read":
            self.stage = None
            header, payload = snapshot(self.run_id, self.read_id, rep=0, source_rep=self.repeats, count=self.count)
            self.decoder.feed(wire(header) + payload + b"\n")


class MetalOrchestrationTests(unittest.TestCase):
    def test_bench_deferred_read_and_coupled_tx_are_separate_explicit_contexts(self):
        for with_tx in (False, True):
            plan = default_plan(with_tx=True)
            case = next(c for c in plan["workloads"] if c["tx_enabled"] and c["tx_mode"] == "gate" and not c["baud"] and not c["mask_segment_us"]) if with_tx else plan["workloads"][0]
            case = {**case, "samples": 16, "rx_repeats": 1 if with_tx else 4}
            run = {"run_id": "m001_1", "settings": case}
            tx, rx = TXFixture(), RXFixture()
            with tempfile.TemporaryDirectory() as temporary, mock.patch("sdr_metal_capture.time.perf_counter", Clock()):
                root = pathlib.Path(temporary)
                execute_run(run, tx, rx, plan, root / "plan.json", root / "case", SecretGuard())
                self.assertEqual(run["status"], "ACQUIRED")
                self.assertIsNone(run["physical_rf_verified"])
                self.assertEqual(len(run["snapshots"]), 1)
                self.assertTrue((root / "case" / run["snapshots"][0]["file"]).is_file())
                self.assertEqual(bool(rx.sent), not with_tx)
                self.assertEqual("permit" in tx.sent, with_tx)
                if not with_tx:
                    self.assertEqual(run["snapshots"][0]["header"]["source_repetition"], 4)
                    self.assertEqual(run["snapshots"][0]["header"]["repetition"], 0)


if __name__ == "__main__": unittest.main()
