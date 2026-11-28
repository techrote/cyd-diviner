"""Fake-firmware protocol integration only; no simulated performance claims."""
import argparse
import binascii
import json
import pathlib
import struct
import tempfile
import unittest
from unittest import mock

from capture import Capture, METRIC_NAMES, capture_command, percentile


SOURCE = "a" * 40
TREE = "b" * 64


class FirmwareSerial:
    pcm_available = True

    def __init__(self, **_):
        self.chunks = []
        self.writes = []
        self.run_id = "none"
        self.duration = 10000
        self.warmup = 2000

    def open(self):
        pass

    def close(self):
        pass

    @property
    def in_waiting(self):
        return len(self.chunks[0]) if self.chunks else 0

    def read(self, _):
        return self.chunks.pop(0) if self.chunks else b""

    def emit(self, **record):
        self.chunks.append(("PERF " + json.dumps({"schema": 1, **record}) + "\n").encode())

    def write(self, data):
        self.writes.append(data)
        command = data.decode("ascii").strip()
        parts = command.split()
        if command == "perf info":
            self.emit(type="info", source_sha=SOURCE, profile="cyd-profile", source_digest=TREE,
                      serial_baud=533333, cpu0_hook=True, cpu1_hook=True)
        elif command == "perf kernel list":
            self.emit(type="kernel_list", kernels=["osc-f32"])
        elif command.startswith("perf kernel "):
            samples = [index % 13 + 4 for index in range(256)]
            self.emit(type="kernel", kernel=parts[2], seed=int(parts[3]), samples=int(parts[4]),
                      large=int(parts[5]), finite=True, samples_us=samples,
                      sum_us=sum(samples), min_us=min(samples), max_us=max(samples),
                      median_us=percentile(samples, 50), p95_us=percentile(samples, 95),
                      p99_us=percentile(samples, 99))
        elif command.startswith("perf spi "):
            self.emit(type="spi", samples_us=list(range(100, 100 + int(parts[2]))),
                      bytes_per_sample=320 * 240 * 2)
        elif command.startswith("perf run "):
            self.run_id = parts[2]
            self.warmup, self.duration = int(parts[3]), int(parts[4])
            self.emit(type="run", run_id=self.run_id, warmup_ms=self.warmup, duration_ms=self.duration)
        elif command == "perf sample":
            self.emit(type="state", run_id=self.run_id, active=False, samples=0, temperature_raw=110)
        elif command == "perf export":
            for name in sorted(METRIC_NAMES):
                self.emit(type="metric", run_id=self.run_id, name=name, unit="us", count=0,
                          sum=0, min=0, max=0, p50_upper=0, p95_upper=0, p99_upper=0,
                          histogram=[0] * 32)
            self.emit(type="end", run_id=self.run_id, complete=True, samples=0,
                      samples_dropped=0, elapsed_us=self.duration * 1000, frames=300)
        elif command == "pcm start":
            if self.pcm_available:
                for sequence in range(3):
                    body = struct.pack("<I128h", sequence, *([42] * 128))
                    self.chunks.append(b"CYD1" + body + struct.pack("<H", binascii.crc_hqx(body, 0xFFFF)))
        elif parts[:1] == ["perf"]:
            self.emit(type="ack", action=parts[1], ok=True)
        return len(data)


def instant_wait(capture, seconds, binary=False, decoder=None, ping=False, actions=None, sink=None):
    """Transport exercise only: intentionally skip elapsed wall-clock waiting."""
    for action in actions or []:
        capture.command(action["command"])
    while capture.serial.in_waiting:
        capture.read(binary=binary, decoder=decoder, sink=sink)


class MissionProtocolTests(unittest.TestCase):
    def run_plan(self, directory, workloads):
        root = pathlib.Path(directory)
        (root / "board.json").write_text(json.dumps({"board": "FAKE protocol fixture; no hardware"}))
        (root / "firmware.bin").write_bytes(b"NOT A FIRMWARE IMAGE")
        plan = {"schema": "cyd-perf-plan/1", "seed": 23063, "repetitions": 1,
                "warmup_ms": 0, "duration_ms": 10000, "workloads": workloads}
        (root / "plan.json").write_text(json.dumps(plan))
        args = argparse.Namespace(plan=root / "plan.json", source_sha=SOURCE, board=root / "board.json",
                                  tree_state="sha256:" + TREE, firmware=root / "firmware.bin", port="FAKE",
                                  baud=533333, profile="cyd-profile", output=root / "capture")
        with mock.patch.dict("sys.modules", {"serial": mock.Mock(Serial=FirmwareSerial)}), mock.patch.object(Capture, "wait", instant_wait):
            capture_command(args)
        return json.loads((root / "capture/results.json").read_text())

    def test_scene_kernel_spi_and_pcm_end_to_end(self):
        workloads = [
            {"name": "scene", "commands": ["perf enable 1", "perf radio off", "perf scene menu"]},
            {"name": "kernel", "kind": "kernel", "kernel": "osc-f32", "kernel_samples": 256,
             "large": False, "commands": ["perf scene menu"]},
            {"name": "spi", "kind": "spi", "samples": 3, "commands": ["perf scene menu"]},
            {"name": "pcm", "pcm": True, "commands": ["perf scene drone"]},
        ]
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_plan(directory, workloads)
            self.assertEqual(result["status"], "CAPTURE COMPLETE")
            self.assertEqual([run["status"] for run in result["runs"]], ["MEASURED"] * 4)
            self.assertEqual(result["runs"][-1]["pcm"]["valid_frames"], 3)
            self.assertEqual(result["runs"][-1]["pcm"]["crc_errors"], 0)
            self.assertEqual(result["runs"][0]["frames_per_second"], 30)
            self.assertTrue(result["target_source_digest_verified"])
            output = pathlib.Path(directory) / "capture"
            self.assertIn(b"CYD1", (output / "serial-rx.bin").read_bytes())
            self.assertTrue((output / "metrics.csv").exists())
            self.assertTrue((output / "artifact-hashes.json").exists())

    def test_pcm_absence_must_fail_with_raw_and_partial_results(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(FirmwareSerial, "pcm_available", False):
            with self.assertRaisesRegex(RuntimeError, "PCM|pcm"):
                self.run_plan(directory, [{"name": "pcm", "pcm": True, "commands": ["perf scene drone"]}])
            output = pathlib.Path(directory) / "capture"
            result = json.loads((output / "results.json").read_text())
            self.assertEqual(result["status"], "FAILED")
            self.assertIn("target_partial", result["runs"][0])
            self.assertTrue((output / "serial-rx.bin").exists())


if __name__ == "__main__":
    unittest.main()
