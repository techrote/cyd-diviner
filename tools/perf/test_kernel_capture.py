"""Offline kernel protocol checks. Synthetic UART records stay in temp fixtures."""
import argparse
import contextlib
import io
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from capture import Capture, percentile
from kernel_capture import KERNELS, OUTPUT_BYTES, execute, validate_identity, validate_kernel

SHA = "a" * 40
DIGEST = "b" * 64


def kernel_record(name="osc-f32", large=0, seed=23063, region="internal"):
    samples = [7 + index % 11 for index in range(256)]
    return {"schema": 1, "type": "kernel", "kernel": name, "seed": seed, "large": large,
            "warmup": 3, "samples": 256, "samples_us": samples, "finite": True,
            "sum_us": sum(samples), "min_us": min(samples), "max_us": max(samples),
            "median_us": percentile(samples, 50), "p95_us": percentile(samples, 95),
            "p99_us": percentile(samples, 99), "output_bytes": OUTPUT_BYTES[name][large],
            "memory_requested": region, "memory_region": region,
            "memory_caps": ("MALLOC_CAP_INTERNAL" if region == "internal" else "MALLOC_CAP_SPIRAM") + "|MALLOC_CAP_8BIT",
            "checksum_fnv1a": KERNELS.index(name) * 2 + large + 1}


class KernelValidationTests(unittest.TestCase):
    def test_raw_samples_dimensions_and_explicit_region(self):
        record = kernel_record()
        validate_kernel(record, "osc-f32", 0, 23063, "internal")
        for key, value in (("sum_us", 1), ("samples", 255), ("output_bytes", 512),
                           ("memory_region", "psram"), ("finite", False)):
            changed = {**record, key: value}
            with self.assertRaises(ValueError):
                validate_kernel(changed, "osc-f32", 0, 23063, "internal")
        changed = {**record, "samples_us": [1.5] * 256}
        with self.assertRaises(ValueError):
            validate_kernel(changed, "osc-f32", 0, 23063, "internal")

    def test_legacy_inference_is_explicit_and_internal_only(self):
        record = kernel_record()
        for key in ("memory_requested", "memory_region", "memory_caps"):
            record.pop(key)
        with self.assertRaises(ValueError):
            validate_kernel(record, "osc-f32", 0, 23063, "internal")
        validate_kernel(record, "osc-f32", 0, 23063, "internal", True)
        with self.assertRaises(ValueError):
            validate_kernel(record, "osc-f32", 0, 23063, "psram", True)


class KernelSerial:
    peer_enabled = False
    def __init__(self, **_):
        self.chunks = []

    def open(self):
        pass

    def close(self):
        pass

    @property
    def in_waiting(self):
        return len(self.chunks[0]) if self.chunks else 0

    def read(self, _):
        return self.chunks.pop(0) if self.chunks else b""

    def emit(self, record):
        self.chunks.append(("PERF " + json.dumps({"schema": 1, **record}) + "\n").encode())

    def write(self, data):
        command = data.decode().strip()
        if command == "perf info":
            self.emit({"type": "info", "source_sha": SHA, "source_digest": DIGEST, "profile": "kernel-s3-n4r2",
                       "serial_baud": 115200, "chip": "ESP32-S3", "flash_bytes": 4194304,
                       "psram_bytes": 2097152, "standalone": True, "link_peer_compiled": self.peer_enabled})
        elif command == "perf link mode off 0":
            self.emit({"type": "link_ack", "action": "mode", "ok": True})
        elif command == "perf link info":
            self.emit({"type": "link_info", "mode": "off", "active": False})
        elif command == "perf kernel list":
            self.emit({"type": "kernel_list", "kernels": list(KERNELS)})
        elif command == "perf sample":
            self.emit({"type": "state", "temperature_c_indication": 36.25, "temperature_raw": None})
        elif command.startswith("perf kernel "):
            fields = command.split()
            self.emit(kernel_record(fields[2], int(fields[5]), int(fields[3]), fields[6]))
        else:
            self.emit({"type": "kernel_error", "error": "fixture received unsupported command"})
        return len(data)


def instant_wait(capture, *_args, **_kwargs):
    while capture.serial.in_waiting:
        capture.read()


class KernelMatrixTests(unittest.TestCase):
    def test_both_memory_regions_matrix_and_raw_csv(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            board = {"board": "Synthetic protocol fixture; not physical evidence", "chip_model": "ESP32-S3 (QFN56)",
                     "port": "FAKE", "flash_bytes": 4194304, "psram_bytes": 2097152}
            (root / "board.json").write_text(json.dumps(board))
            (root / "firmware.bin").write_bytes(b"NOT FIRMWARE")
            args = argparse.Namespace(port="FAKE", baud=115200, board=root / "board.json", firmware=root / "firmware.bin",
                                      profile="kernel-s3-n4r2", source_sha=SHA, source_digest=DIGEST, seed=23063,
                                      memory="both", legacy_internal=False, output=root / "capture")
            with mock.patch.dict("sys.modules", {"serial": mock.Mock(Serial=KernelSerial)}), mock.patch.object(Capture, "wait", instant_wait), mock.patch.object(KernelSerial, "peer_enabled", True), contextlib.redirect_stdout(io.StringIO()):
                execute(args)
            result = json.loads((args.output / "results.json").read_text())
            self.assertEqual(result["status"], "CAPTURE COMPLETE")
            self.assertEqual(len(result["runs"]), 132)
            self.assertEqual(len(result["plan"]["workloads"]), 44)
            self.assertEqual(result["plan"]["schema"], "cyd-perf-plan/1")
            self.assertEqual({run["status"] for run in result["runs"]}, {"MEASURED"})
            self.assertEqual(len((args.output / "kernel-timings.csv").read_text().splitlines()), 132 * 256 + 1)
            self.assertEqual(len((args.output / "kernel-sensors.csv").read_text().splitlines()), 132 * 2 + 1)
            self.assertTrue((args.output / "serial-rx.bin").exists())
            self.assertEqual(result["link_baseline"][0]["mode"], "off")
            info = result["target_info"][0]
            with self.assertRaises(ValueError):
                validate_identity({**info, "psram_bytes": 0}, args, board)
            validate_identity({**info, "native_usb": True, "usb_tx_buffer_configured": True}, args, board)
            with self.assertRaises(ValueError):
                validate_identity({**info, "native_usb": True, "usb_tx_buffer_configured": False}, args, board)
            args.legacy_internal = True
            with self.assertRaises(ValueError):
                validate_identity(info, args, board)


if __name__ == "__main__":
    unittest.main()
