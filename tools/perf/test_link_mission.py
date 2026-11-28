"""Serial-protocol fixtures only: no simulated measurements are delivered."""
import argparse
import copy
import hashlib
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from capture import METRIC_NAMES
from link_capture import capture, default_plan, derive_link


class LinkFirmware:
    fail_first_arm = False
    failed = False
    strict_control_phases = False
    commands = []

    def __init__(self, **_):
        self.chunks = []
        self.mode = "off"
        self.reported_role = "idle"
        self.protocol = "none"
        self.pattern = "none"
        self.run_id = "none"
        self.duration = 1
        self.warmup = 0
        self.complete = True
        self.heavy = False

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
        data = b"PERF " + json.dumps({"schema": 1, **record}).encode() + b"\n"
        self.chunks.extend((data[:37], data[37:]))

    def ack(self, action, okay=True):
        self.emit({"type": "link_ack", "action": action, "ok": okay, "code": 0 if okay else 12, "operation": "fixture_stage"})

    def write(self, data):
        command = data.decode().strip().split()
        LinkFirmware.commands.append((self.port, " ".join(command)))
        if command[:2] == ["perf", "info"]:
            cyd = self.port == "COM21"
            self.emit({"type": "info", "source_sha": ("a" if cyd else "b") * 40,
                       "source_digest": ("c" if cyd else "d") * 64, "profile": "cyd-link" if cyd else "s3-peer"})
        elif command[:3] == ["perf", "link", "info"]:
            if self.strict_control_phases and self.heavy:
                self.emit({"type": "error", "message": "fixture: bounded telemetry allocation exhausted under DSP"})
                return len(data)
            self.emit({"type": "link_info", "mode": self.mode, "mode_ready": self.mode != "off",
                       "sta_mac": "020000000001" if self.port == "COM21" else "020000000002",
                       "bt_mac": "020000000003" if self.port == "COM21" else "020000000004",
                       "ip": "192.168.4.2" if self.port == "COM21" else "192.168.4.1", "actual_channel": 6,
                       "tcp_connected": (self.reported_role == "responder" or not self.complete) if self.strict_control_phases else True,
                       "ble_connected": True, "ble_ready": True, "ble_compiled": True,
                       "ble_mtu": 247, "ble_actual_payload_limit": 200})
        elif command[:3] == ["perf", "link", "mode"]:
            self.mode = command[3]
            self.ack("mode")
        elif command[:3] == ["perf", "link", "serve"]:
            self.run_id, self.protocol = command[3:5]
            self.reported_role = "responder"
            self.ack("serve")
        elif command[:3] == ["perf", "link", "arm"]:
            if self.fail_first_arm and not LinkFirmware.failed:
                LinkFirmware.failed = True
                self.ack("arm", False)
            else:
                self.protocol, self.pattern = command[3:5]
                self.reported_role = "initiator"
                self.ack("arm")
        elif command[:2] == ["perf", "run"]:
            self.run_id, self.warmup, self.duration = command[2], int(command[3]), int(command[4])
            self.emit({"type": "run", "run_id": self.run_id})
        elif command == ["perf", "export"]:
            self.complete = True
            bins = [0] * 32
            bins[4] = 1
            for name in METRIC_NAMES:
                self.emit({"type": "metric", "run_id": self.run_id, "name": name, "unit": "us", "count": 1,
                           "sum": 10, "min": 10, "max": 10, "p50_upper": 16, "p95_upper": 16,
                           "p99_upper": 16, "histogram": bins})
            self.emit({"type": "end", "run_id": self.run_id, "complete": True, "elapsed_us": self.duration * 1000,
                       "frames": 1, "samples": 0, "samples_dropped": 0})
        elif command[:3] == ["perf", "link", "export"]:
            if self.strict_control_phases and self.heavy:
                self.emit({"type": "error", "message": "fixture: bounded telemetry allocation exhausted under DSP"})
                return len(data)
            initiator = self.reported_role == "initiator"
            bins = [0] * 32
            bins[4] = 8 if initiator and self.pattern == "rtt" else 0
            self.emit({"type": "link_summary", "run_id": self.run_id, "role": self.reported_role,
                       "complete": True, "duration_us": self.duration * 1000 if initiator else 0,
                       "tx_api_accepted_packets": 10 if initiator else 0, "tx_payload_bytes": 640 if initiator else 0,
                       "rx_unique_packets": 8, "rx_payload_bytes": 512, "rtt_count": sum(bins),
                       "rtt_min_us": 10, "rtt_max_us": 10, "rtt_sum_us": 80, "rtt_histogram_counts": bins,
                       "wrong_token": 0})
        elif command[:2] == ["perf", "link"]:
            if command[2] == "idle":
                self.complete = False
            self.ack(command[2])
        elif command[:2] == ["perf", "scene"]:
            if command[2] == "menu":
                self.heavy = False
            self.emit({"type": "scene_settings", "scene": command[2]})
            self.emit({"type": "ack", "action": "fixture_control", "ok": True})
        elif command[:2] == ["perf", "combined"]:
            self.heavy = True
            self.emit({"type": "scene_settings", "scene": command[2], "delay_allocated": True})
            self.emit({"type": "ack", "action": "fixture_control", "ok": True})
        else:
            self.emit({"type": "ack", "action": "fixture_control", "ok": True})
        return len(data)


class Clock:
    def __init__(self):
        self.value = 0

    def __call__(self):
        self.value += .02
        return self.value


def arguments(root, fail_first=False):
    private = root / "private.json"
    private.write_text(json.dumps({"schema": "cyd-perf-link-private/1",
        "direct_softap": {"ssid": "fixture-owned-softap", "password": "fixture-password-only"},
        "through_ap": {"ssid": "fixture-owned-hotspot", "password": "fixture-password-only"}}))
    identities = {"schema": "cyd-perf-link-identities/1"}
    for name, letter, port, tree in (("cyd", "a", "COM21", "c"), ("peer", "b", "COM22", "d")):
        firmware = root / (name + ".bin")
        firmware.write_bytes(b"test image fixture; not firmware")
        identities[name] = {"board": name + " fixture", "port": port, "baud": 115200,
            "profile": "cyd-link" if name == "cyd" else "s3-peer", "source_sha": letter * 40,
            "source_digest": tree * 64, "firmware_file": str(firmware),
            "firmware_sha256": hashlib.sha256(firmware.read_bytes()).hexdigest()}
    identity_file = root / "identities.json"
    identity_file.write_text(json.dumps(identities))
    plan = default_plan(include_ble=True)
    plan.update(repetitions=1, warmup_ms=0, duration_ms=1, reply_drain_ms=0)
    selected = (("direct_espnow", "rtt"), ("direct_espnow", "espnow_paced"),
                ("direct_softap", "tcp_goodput"), ("through_ap", "udp_paced"),
                ("direct_ble", "idle_connected"), ("direct_ble", "rtt"))
    plan["workloads"] = [next(case for case in plan["workloads"] if (case["topology"], case["mode"]) == pair and case["local_workload"] == "menu") for pair in selected]
    plan_file = root / "plan.json"
    plan_file.write_text(json.dumps(plan))
    return argparse.Namespace(provision_private=private, identities=identity_file, plan=plan_file, output=root / "output")


class LinkMissionTests(unittest.TestCase):
    def test_tcp_readiness_and_metadata_do_not_require_heavy_dsp_heap(self):
        """Model retained complete=true and a formatter that fails under DSP.

        A repeated completed TCP case must clear completion before readiness,
        establish its connection in menu, then release DSP only after profiling.
        These fixtures check control/correlation, not target performance.
        """
        with tempfile.TemporaryDirectory() as temporary:
            args = arguments(pathlib.Path(temporary))
            plan = json.loads(args.plan.read_text())
            case = next(case for case in default_plan()["workloads"] if case["topology"] == "direct_softap"
                        and case["mode"] == "tcp_goodput" and case["local_workload"] == "full_gate_drone")
            plan.update(workloads=[case], repetitions=2)
            args.plan.write_text(json.dumps(plan))
            LinkFirmware.fail_first_arm = False
            LinkFirmware.strict_control_phases, LinkFirmware.commands = True, []
            try:
                with mock.patch.dict("sys.modules", {"serial": mock.Mock(Serial=LinkFirmware)}), mock.patch("link_capture.time.perf_counter", Clock()):
                    self.assertEqual(capture(args), 0)
            finally:
                LinkFirmware.strict_control_phases = False
            result = json.loads((args.output / "results.json").read_text())
            self.assertEqual(len(result["runs"]), 2)
            for run in result["runs"]:
                self.assertEqual(run["status"], "MEASURED")
                self.assertEqual(run["peer_link"][-1]["run_id"], run["run_id"])
                self.assertEqual(run["peer_link"][-1]["rx_unique_packets"], 8)
                self.assertTrue(run["connection_info"]["cyd"]["tcp_connected"])
                self.assertTrue(any(record.get("delay_allocated") for record in run["setup_target"]))
                self.assertFalse(any(record.get("delay_allocated") for record in run["prewarm_setup_target"]["cyd"]))
                self.assertEqual([record["scene"] for record in run["post_window_cleanup_target"]["cyd"]
                                  if record["type"] == "scene_settings"], ["menu"])
                self.assertIn("not workload heap", run["post_info_scope"])
                self.assertFalse(any(record["type"] == "scene_settings" for record in run["cyd_profile"]))
            commands = [command for endpoint, command in LinkFirmware.commands if endpoint == "COM21"]
            for index, command in enumerate(commands):
                if command.startswith("perf link arm "):
                    self.assertLess(commands[:index].index("perf link idle"), index)
                if command == "perf link export":
                    self.assertEqual(commands[index - 1], "perf scene menu")

    def test_dual_board_protocol_and_failed_case_continuation(self):
        for fail_first in (False, True):
            with tempfile.TemporaryDirectory() as temporary:
                root = pathlib.Path(temporary)
                args = arguments(root)
                LinkFirmware.fail_first_arm, LinkFirmware.failed = fail_first, False
                with mock.patch.dict("sys.modules", {"serial": mock.Mock(Serial=LinkFirmware)}), mock.patch("link_capture.time.perf_counter", Clock()):
                    status = capture(args)
                result = json.loads((args.output / "results.json").read_text())
                self.assertEqual(status, 2 if fail_first else 0)
                self.assertEqual(result["status"], "FAILED" if fail_first else "CAPTURE COMPLETE")
                self.assertEqual([run["status"] for run in result["runs"]], ["FAILED" if fail_first else "MEASURED"] + ["MEASURED"] * 5)
                self.assertTrue(result["runs"][-1]["derived"]["traffic_established"])
                self.assertEqual(result["runs"][-1]["peer_link"][-1]["wrong_token"], 0)
                self.assertTrue((args.output / "profile-metrics.csv").exists())
                self.assertTrue((args.output / "artifact-hashes.json").exists())
                for path in args.output.rglob("*"):
                    if path.is_file():
                        self.assertNotIn(b"fixture-password-only", path.read_bytes())

    def test_zero_traffic_is_not_zero_loss_and_histogram_counts_are_checked(self):
        base = {"complete": True, "duration_us": 10000000, "rtt_histogram_counts": [0] * 32,
                "rtt_count": 0, "tx_api_accepted_packets": 10, "tx_payload_bytes": 640,
                "rx_unique_packets": 0, "rx_payload_bytes": 0}
        derived = derive_link(base, copy.deepcopy(base), 10000, {"pattern": "rtt"})
        self.assertFalse(derived["traffic_established"])
        self.assertIsNone(derived["rtt_p99_us_upper"])
        self.assertEqual(derived["accepted_packet_loss_fraction"], 1)
        broken = copy.deepcopy(base)
        broken["rtt_count"] = 1
        with self.assertRaises(RuntimeError):
            derive_link(broken, base, 10000, {"pattern": "rtt"})


if __name__ == "__main__":
    unittest.main()
