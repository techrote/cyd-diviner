import binascii
import csv
import json
import pathlib
import random
import struct
import tempfile
import unittest
from unittest import mock

from capture import Capture, METRIC_NAMES, PCMDecoder, distribution, flatten_results, interpreted_records, parse_perf, stress_assessment, validate_end, validate_plan
from make_plan import plans


def frame(sequence, value=42):
    body = struct.pack("<I128h", sequence, *([value] * 128))
    return b"CYD1" + body + struct.pack("<H", binascii.crc_hqx(body, 0xFFFF))


class ProtocolTests(unittest.TestCase):
    def test_pcm_arbitrary_chunks_crc_noise_loss_wrap(self):
        damaged = bytearray(frame(0))
        damaged[81] ^= 1
        source = b"boot text\r\nC" + frame(0xFFFFFFFE) + frame(0xFFFFFFFF) + damaged + frame(1) + frame(1) + frame(2)
        decoder = PCMDecoder()
        rng = random.Random(451)
        while source:
            count = rng.randrange(1, 400)
            decoder.feed(source[:count])
            source = source[count:]
        self.assertEqual(decoder.frames, 4)
        self.assertEqual(decoder.corrupt, 1)
        self.assertEqual(decoder.missing, 1)
        self.assertEqual(decoder.duplicates, 1)
        self.assertEqual(decoder.discontinuities, 0)
        self.assertEqual(decoder.nonzero_samples, 4 * 128)

    def test_pcm_backward_sequence_is_discontinuity_not_billions_lost(self):
        decoder = PCMDecoder()
        decoder.feed(frame(500) + frame(0))
        self.assertEqual(decoder.discontinuities, 1)
        self.assertEqual(decoder.missing, 0)

    def test_partial_magic_survives_reads(self):
        decoder = PCMDecoder()
        decoder.feed(b"noiseCY")
        decoder.feed(frame(7)[2:30])
        decoder.feed(frame(7)[30:])
        self.assertEqual(decoder.frames, 1)
        self.assertEqual(decoder.discarded_bytes, 5)

    def test_host_arrivals_do_not_claim_target_cadence(self):
        decoder = PCMDecoder()
        decoder.feed(frame(0) + frame(1), 2.0)
        decoder.feed(frame(2), 2.02)
        summary = decoder.summary()
        self.assertEqual(summary["host_arrival_interval_us"]["samples"], 2)
        self.assertEqual(summary["host_arrival_interval_us"]["median"], 0)
        self.assertNotIn("dma_underruns", summary)

    def test_perf_schema_validation_and_unrelated_lines(self):
        self.assertIsNone(parse_perf(b"Diviner menu heap=13"))
        self.assertEqual(parse_perf(b'PERF {"schema":1,"type":"metric","name":"x"}') ["name"], "x")
        for line in (b'PERF {"schema":2,"type":"metric"}', b'PERF []', b'PERF malformed'):
            with self.assertRaises(ValueError):
                parse_perf(line)

    def test_nearest_rank_and_missing_data(self):
        self.assertIsNone(distribution([])["median"])
        self.assertEqual(distribution(list(range(1, 101)))["p99"], 99)
        self.assertEqual(distribution([9])["p99_support"], "fewer_than_100_samples")

    def test_plans_valid_and_stress_cannot_be_too_short(self):
        for plan in plans().values():
            validate_plan(plan)
        short = {"schema": "cyd-perf-plan/1", "workloads": [{"name": "stress", "stress_seconds": 359}]}
        with self.assertRaises(ValueError):
            validate_plan(short)

    def test_stress_needs_both_cores_observed(self):
        records = [{"type": "sample", "elapsed_ms": 0, "core0_busy_pct": 99,
                    "cpu0_busy_ticks": 99, "cpu0_total_ticks": 100},
                   {"type": "sample", "elapsed_ms": 364000, "core0_busy_pct": 99,
                    "cpu0_busy_ticks": 99, "cpu0_total_ticks": 100},
                   {"type": "end", "complete": True, "elapsed_us": 360000000}]
        self.assertFalse(stress_assessment(records, 360)["near_100_percent_confirmed"])
        for record in records[:2]:
            record.update(core1_busy_pct=98, cpu1_busy_ticks=98, cpu1_total_ticks=100)
        self.assertTrue(stress_assessment(records, 360)["near_100_percent_confirmed"])
        self.assertFalse(stress_assessment(records, 360, {"cpu0_hook": True, "cpu1_hook": False})["near_100_percent_confirmed"])
        records[-1]["elapsed_us"] = 359999999
        self.assertFalse(stress_assessment(records, 360)["near_100_percent_confirmed"])

    def test_missing_metrics_incomplete_and_empty_histograms_are_honest(self):
        records = [{"type": "metric", "run_id": "r1", "name": name, "count": 0,
                    "histogram": [0] * 32, "min": 0, "max": 0,
                    "p50_upper": 0, "p95_upper": 0, "p99_upper": 0} for name in METRIC_NAMES]
        records.append({"type": "end", "run_id": "r1", "complete": True,
                        "samples": 0, "elapsed_us": 10000000})
        validate_end(records, "r1", 10000)
        interpreted = interpreted_records(records)
        self.assertIsNone(interpreted[0]["p99_upper"])
        self.assertEqual(records[0]["p99_upper"], 0)  # Raw evidence not mutated.
        with self.assertRaises(RuntimeError):
            validate_end(records[1:], "r1", 10000)
        records[-1]["complete"] = False
        with self.assertRaises(RuntimeError):
            validate_end(records, "r1", 10000)

    def test_csv_preserves_units_histogram_bound_names_and_not_run(self):
        result = {"runs": [{"run_id": "r1", "workload": "scene", "repetition": 1, "status": "MEASURED",
            "target": [{"type": "metric", "name": "frame", "unit": "us", "p99_us_upper": 8192},
                       {"type": "sample", "temperature_c": None}]},
            {"workload": "radio", "status": "NOT RUN", "reason": "No peer"}]}
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            flatten_results(result, directory)
            with (directory / "metrics.csv").open(newline="", encoding="utf-8") as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(rows[0]["p99_us_upper"], "8192")
            self.assertEqual(rows[0]["unit"], "us")
            self.assertEqual(rows[1]["reason"], "No peer")


class FakeSerial:
    def __init__(self, **_):
        self.chunks = [b'PERF {"schema":1,"type":"run","run_id":"r1"}\n']
        self.writes = []

    def open(self):
        pass

    def close(self):
        pass

    @property
    def in_waiting(self):
        return len(self.chunks[0]) if self.chunks else 0

    def read(self, _):
        return self.chunks.pop(0) if self.chunks else b""

    def write(self, data):
        self.writes.append(data)
        if data == b"perf export\n":
            self.chunks.extend([b'PERF {"schema":1,"type":"end","run_id":"old"}\n',
                                b'PERF {"schema":1,"type":"end","run_id":"r', b'1"}\n'])
        return len(data)


class TransportTests(unittest.TestCase):
    def test_deferred_export_split_records_and_wrong_run_id(self):
        with tempfile.TemporaryDirectory() as temporary:
            with mock.patch.dict("sys.modules", {"serial": mock.Mock(Serial=FakeSerial)}):
                capture = Capture("TEST", 533333, pathlib.Path(temporary) / "capture")
            try:
                capture.until("run", run_id="r1", timeout=0.05)
                with self.assertRaises(TimeoutError):
                    capture.until("end", run_id="r1", timeout=0.01)
                capture.command("perf export")
                records = capture.until("end", run_id="r1", timeout=0.05)
                self.assertEqual(records[-1]["run_id"], "r1")
                self.assertFalse(capture.serial.dtr)
                self.assertFalse(capture.serial.rts)
            finally:
                capture.close()
            raw = (pathlib.Path(temporary) / "capture" / "serial-rx.bin").read_bytes()
            self.assertIn(b'"run_id":"old"', raw)
            events = [json.loads(line) for line in (pathlib.Path(temporary) / "capture" / "serial-events.jsonl").read_text().splitlines()]
            receives = [event for event in events if event["event"] == "rx"]
            self.assertEqual(sum(event["bytes"] for event in receives), len(raw))

    def test_target_kernel_error_aborts_and_preserves_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            with mock.patch.dict("sys.modules", {"serial": mock.Mock(Serial=FakeSerial)}):
                capture = Capture("TEST", 533333, pathlib.Path(temporary) / "capture")
            try:
                capture.serial.chunks = [b'PERF {"schema":1,"type":"kernel_error","error":"allocation failed"}\n']
                with self.assertRaisesRegex(RuntimeError, "allocation failed"):
                    capture.until("kernel", timeout=0.05)
            finally:
                capture.close()
            self.assertIn(b"allocation failed", (pathlib.Path(temporary) / "capture" / "serial-rx.bin").read_bytes())


if __name__ == "__main__":
    unittest.main()
