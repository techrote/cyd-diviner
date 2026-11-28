"""Native-USB metadata/export recovery fixtures; no traffic or hardware."""
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from link_capture import Endpoint, SecretGuard
from test_link_mission import Clock


class StalledMetadata:
    never_release = False

    def __init__(self, **_):
        self.chunks, self.writes = [], []
        self.tail = b""
        self.generation = 0

    def open(self):
        pass

    def close(self):
        pass

    @property
    def in_waiting(self):
        return len(self.chunks[0]) if self.chunks else 0

    def read(self, _):
        return self.chunks.pop(0) if self.chunks else b""

    def write(self, payload):
        self.writes.append(payload)
        record = {"schema": 1, "type": "link_summary", "run_id": "l1", "complete": True, "padding": "x" * 300}
        if payload == b"perf link info\n":
            self.generation += 1
            info = {"schema": 1, "type": "link_info", "generation": self.generation, "mode_ready": True, "padding": "x" * 300}
            if self.tail:
                if not self.never_release:
                    self.chunks += [self.tail, b"PERF " + json.dumps(info).encode() + b"\n"]
                    self.tail = b""
                return len(payload)
            record = info
        wire = b"PERF " + json.dumps(record).encode() + b"\n"
        self.chunks.append(wire[:90])
        self.tail = wire[90:]
        return len(payload)


class LinkUSBProgressTests(unittest.TestCase):
    def fixture(self, serial_type, operation, native=True):
        with tempfile.TemporaryDirectory() as temporary:
            output = pathlib.Path(temporary) / "endpoint"
            with mock.patch.dict("sys.modules", {"serial": mock.Mock(Serial=serial_type)}), mock.patch("link_capture.time.perf_counter", Clock()):
                endpoint = Endpoint("peer", {"port": "FIXTURE"}, output, SecretGuard())
                endpoint.native_usb = native
                try:
                    operation(endpoint)
                finally:
                    writes = list(endpoint.serial.writes)
                    endpoint.close()
            events = [json.loads(line) for line in (output / "serial-events.redacted.jsonl").read_text().splitlines()]
            return writes, events

    def test_info_returns_latest_snapshot_and_consumes_recovery_metadata(self):
        def operation(endpoint):
            info = endpoint.info()
            self.assertEqual(info["generation"], 2)
            self.assertEqual(len([record for record in endpoint.records if record["type"] == "link_info"]), 2)
            self.assertFalse(endpoint.pending)
        writes, events = self.fixture(StalledMetadata, operation)
        self.assertEqual(writes, [b"perf link info\n", b"perf link info\n"])
        self.assertEqual(sum(event["event"] == "usb_progress_poll" for event in events), 1)

    def test_summary_keeps_run_id_and_drains_extra_info_before_returning(self):
        def operation(endpoint):
            records = endpoint.request("perf link export", {"link_summary"}, run_id="l1")
            self.assertEqual(records[0]["run_id"], "l1")
            self.assertEqual(records[-1]["type"], "link_info")
            self.assertFalse(endpoint.pending)
        _, events = self.fixture(StalledMetadata, operation)
        self.assertEqual([event["awaited_record"] for event in events if event["event"] == "usb_progress_poll"], ["link_summary"])

    def test_uart_private_and_quiet_window_waits_never_query(self):
        def uart(endpoint):
            with self.assertRaises(TimeoutError):
                endpoint.request("perf link info", {"link_info"}, timeout=2)
        writes, _ = self.fixture(StalledMetadata, uart, native=False)
        self.assertEqual(writes, [b"perf link info\n"])
        def quiet(endpoint):
            endpoint.pending.extend(b'PERF {"schema":1,"type":"link_info","padding":')
            with self.assertRaises(TimeoutError):
                endpoint.until({"run"}, 0, timeout=2)
            with self.assertRaises(TimeoutError):
                endpoint.until({"link_info"}, 0, timeout=2, private=True)
        writes, _ = self.fixture(StalledMetadata, quiet)
        self.assertEqual(writes, [])

    def test_unreleased_metadata_has_at_most_three_spaced_queries(self):
        class Never(StalledMetadata):
            never_release = True
        def operation(endpoint):
            with self.assertRaises(TimeoutError):
                endpoint.request("perf link info", {"link_info"}, timeout=5)
        writes, events = self.fixture(Never, operation)
        self.assertEqual(len(writes), 4)  # Original request plus three recoveries.
        times = [event["host_elapsed_s"] for event in events if event["event"] == "usb_progress_poll"]
        self.assertEqual(len(times), 3)
        self.assertTrue(all(b - a >= 1 for a, b in zip(times, times[1:])))


if __name__ == "__main__":
    unittest.main()
