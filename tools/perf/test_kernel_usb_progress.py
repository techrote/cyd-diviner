"""Partial USB record recovery fixtures, never target timing simulations."""
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from capture import Capture
from kernel_capture import await_record, validate_kernel
from test_kernel_capture import KernelSerial, kernel_record


class Clock:
    def __init__(self):
        self.now = 0

    def __call__(self):
        self.now += .05
        return self.now


class StalledUSB(KernelSerial):
    malformed = False
    empty = False
    never_release = False

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.writes = []
        self.tail = b""

    def write(self, payload):
        self.writes.append(payload)
        if payload.startswith(b"perf kernel "):
            if not self.empty:
                wire = ("PERF " + json.dumps(kernel_record()) + "\n").encode()
                self.chunks.append(wire[:250])
                self.tail = b"BROKEN_JSON}\n" if self.malformed else wire[250:]
        elif payload == b"perf sample\n" and not self.never_release:
            self.chunks.append(self.tail)
            self.tail = b""
            self.emit({"type": "state", "temperature_raw": None})
        return len(payload)


class USBProgressTests(unittest.TestCase):
    def run_capture(self, serial_type, action):
        with tempfile.TemporaryDirectory() as temporary:
            output = pathlib.Path(temporary) / "capture"
            with mock.patch.dict("sys.modules", {"serial": mock.Mock(Serial=serial_type)}), mock.patch("kernel_capture.time.perf_counter", Clock()):
                capture = Capture("FIXTURE", 115200, output)
                try:
                    capture.command("perf kernel osc-f32 23063 256 0 internal")
                    action(capture)
                finally:
                    writes = list(capture.serial.writes)
                    capture.close()
            raw = (output / "serial-rx.bin").read_bytes()
            events = [json.loads(line) for line in (output / "serial-events.jsonl").read_text().splitlines()]
            return writes, raw, events

    def test_partial_record_released_by_one_logged_post_record_poll(self):
        def action(capture):
            records = await_record(capture, "kernel", after=0, timeout=5, usb_progress_poll=True)
            record = next(item for item in records if item["type"] == "kernel")
            validate_kernel(record, "osc-f32", 0, 23063, "internal")
        writes, raw, events = self.run_capture(StalledUSB, action)
        self.assertEqual(writes.count(b"perf sample\n"), 1)
        recoveries = [event for event in events if event["event"] == "usb_progress_poll"]
        self.assertEqual(len(recoveries), 1)
        self.assertGreaterEqual(recoveries[0]["stalled_for_s"], .75)
        self.assertEqual(recoveries[0]["awaited_record"], "kernel")
        self.assertIn(b'"samples_us"', raw)

    def test_malformed_completion_is_retained_and_fails_instead_of_resync_discard(self):
        class Malformed(StalledUSB):
            malformed = True
        def action(capture):
            with self.assertRaisesRegex(RuntimeError, "Malformed target telemetry"):
                await_record(capture, "kernel", after=0, timeout=5, usb_progress_poll=True)
            self.assertTrue(capture.errors)
        _, raw, _ = self.run_capture(Malformed, action)
        self.assertIn(b"BROKEN_JSON", raw)

    def test_no_partial_type_never_pokes_and_unreleased_partial_is_bounded_to_three(self):
        class Empty(StalledUSB):
            empty = True
        def action(capture):
            with self.assertRaises(TimeoutError):
                await_record(capture, "kernel", after=0, timeout=5, usb_progress_poll=True)
        writes, _, _ = self.run_capture(Empty, action)
        self.assertNotIn(b"perf sample\n", writes)
        class Unreleased(StalledUSB):
            never_release = True
        writes, _, events = self.run_capture(Unreleased, action)
        self.assertEqual(writes.count(b"perf sample\n"), 3)
        times = [event["host_elapsed_s"] for event in events if event["event"] == "usb_progress_poll"]
        self.assertTrue(all(later - earlier >= 1 for earlier, later in zip(times, times[1:])))


if __name__ == "__main__":
    unittest.main()
