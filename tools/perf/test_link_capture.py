"""Private provisioning and stream-redaction tests; no hardware/network access."""
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from link_capture import Endpoint, SecretGuard, StreamingRedactor, default_plan, read_private, validate_plan


class SerialFixture:
    def __init__(self, **_):
        self.chunks = []
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
        self.chunks.append(b'PERF {"schema":1,"type":"ack","action":"link","ok":true}\n')
        return len(data)


class LinkPrivacyTests(unittest.TestCase):
    def test_stream_redaction_retains_length_across_every_chunk_boundary(self):
        secret = "test-private-credential"
        guard = SecretGuard()
        guard.add(secret)
        wire = b"start:" + secret.encode() + b":" + secret.encode().hex().encode() + b":end\n"
        for boundary in range(1, len(wire)):
            stream = StreamingRedactor(guard)
            redacted = stream.feed(wire[:boundary]) + stream.feed(wire[boundary:]) + stream.flush()
            self.assertEqual(len(redacted), len(wire))
            self.assertNotIn(secret.encode(), redacted)
            self.assertNotIn(secret.encode().hex().encode(), redacted)
            self.assertTrue(redacted.startswith(b"start:"))
            self.assertTrue(redacted.endswith(b":end\n"))

    def test_private_transaction_and_fragmented_sdk_echo_do_not_persist_secrets(self):
        ssid, password = "PRIVATE_TEST_NETWORK", "fixture-private-password"
        guard = SecretGuard()
        guard.add(ssid)
        guard.add(password)
        with tempfile.TemporaryDirectory() as temporary:
            output = pathlib.Path(temporary) / "endpoint"
            with mock.patch.dict("sys.modules", {"serial": mock.Mock(Serial=SerialFixture)}):
                endpoint = Endpoint("cyd", {"port": "COM21", "baud": 533333}, output, guard)
            try:
                private_command = f"perf link provision {ssid.encode().hex()} {password.encode().hex()}"
                endpoint.private_request(private_command, "credential_provision")
                sdk = f"unexpected SDK text {ssid} {password}\n".encode()
                endpoint.serial.chunks.extend([sdk[:17], sdk[17:35], sdk[35:]])
                for _ in range(3):
                    endpoint.read()
                endpoint.serial.chunks.append(b'PERF {"schema":1,"type":"link_info","mode_ready":true}\n')
                endpoint.read()
                self.assertTrue(endpoint.records[-1]["mode_ready"])
                self.assertEqual(endpoint.serial.writes[0], (private_command + "\n").encode())
            finally:
                endpoint.close()
            for path in output.iterdir():
                payload = path.read_bytes()
                for secret in (ssid, password, ssid.encode().hex(), password.encode().hex()):
                    self.assertNotIn(secret.encode(), payload)
            events = [json.loads(line) for line in (output / "serial-events.redacted.jsonl").read_text().splitlines()]
            self.assertFalse(any(event.get("command", "").startswith("perf link provision") for event in events))
            self.assertEqual(sum(event["bytes"] for event in events if event["event"] == "rx"),
                             len((output / "serial-rx.redacted.bin").read_bytes()))

    def test_sensitive_command_cannot_accidentally_take_logged_send_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            with mock.patch.dict("sys.modules", {"serial": mock.Mock(Serial=SerialFixture)}):
                endpoint = Endpoint("peer", {"port": "COM22"}, pathlib.Path(temporary) / "peer", SecretGuard())
            try:
                for command in ("perf link provision private", "perf link allow private"):
                    with self.assertRaises(ValueError):
                        endpoint.send(command)
                self.assertEqual(endpoint.serial.writes, [])
            finally:
                endpoint.close()

    def test_private_file_inside_any_repository_is_rejected_before_read(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / ".git").write_text("gitdir: irrelevant")
            with self.assertRaisesRegex(ValueError, "outside every Git"):
                read_private(root / "private.json", SecretGuard())

    def test_default_matrix_is_bounded_and_covers_all_owned_topologies(self):
        plan = default_plan()
        validate_plan(plan)
        self.assertEqual({case["topology"] for case in plan["workloads"]}, {"direct_espnow", "direct_softap", "through_ap"})
        self.assertEqual(len(plan["workloads"]) * plan["repetitions"], 66)
        plan["workloads"][0]["packets_per_second"] = 1001
        with self.assertRaises(ValueError):
            validate_plan(plan)


if __name__ == "__main__":
    unittest.main()
