"""Protocol/analysis fixtures only: never simulate delivered hardware results."""
import argparse
import copy
import hashlib
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from audio_mvp_capture import capture, default_plan, validate_plan, validate_run
from audio_mvp_analyze import analyze
from test_link_mission import Clock


def configuration(case):
    block = case["frames_per_packet"] * case["channels"] * (2 if case["codec"] == "pcm16" else case["osr"] // 8)
    return {"codec": case["codec"], "channels": case["channels"], "sample_rate_hz": case["sample_hz"],
        "frames_per_block": case["frames_per_packet"], "pdm_osr": case["osr"], "phy_mbps_requested": case["rate_mbps"],
        "phy_config_status": 0, "channel": 6, "seed": 23063, "block_payload_bytes": block, "fragments_per_block": 1,
        "packet_header_bytes": 20, "max_packet_bytes": 250, "playout_delay_blocks": 2, "rx_slots": 8,
        "rx_callback_queue_depth": 8, "tx_inflight_limit": 1, "worker_core": 1, "worker_priority": 3,
        "pacing": "fixture_rational", "transport": "ESP_NOW_broadcast_packets", "mac_ack_required": False,
        "application_retransmission": False, "sink": "digital_reconstruction_only", "i2s_output": False,
        "raw_continuous_RF": False, "clock_scope": "receiver_worker_admission_relative; absolute_oneway_age_unknown"}


def summary(case, role, run_id="a001_1", duration=100):
    blocks = case["sample_hz"] * duration // (1000 * case["frames_per_packet"])
    rx = role == "rx"
    record = {"schema": 1, "type": "audio_summary", "run_id": run_id, "role": role, "complete": True, "timed_out": False,
        "duration_us": duration * 1000, "target_start_us": 0 if rx else 1000000, "target_end_us": 0 if rx else 1000000 + duration * 1000,
        **configuration(case), "source_total_blocks": blocks, "source_encoded_blocks": 0 if rx else blocks,
        "skipped_source_blocks": 0, "expired_prepared_blocks": 0, "encoder_reset_discontinuities": 0,
        "tx_packet_attempts": 0 if rx else blocks, "tx_api_accepted_packets": 0 if rx else blocks,
        "tx_api_no_mem": 0, "tx_api_other_errors": 0, "tx_wire_bytes_accepted": 0 if rx else blocks * 84,
        "rx_callback_queue_drops": 0, "rx_wrong_mac": 0, "rx_expired_callback_packets": 0, "rx_callback_age_max_us": 0,
        "rendered_quality_blocks": blocks if rx else 0, "concealed_quality_blocks": 5 if rx else 0,
        "first_rendered_sequence": 0, "last_rendered_sequence": blocks - 1 if rx else 0, "slot_dwell_max_us": 0,
        "rx_accepted_packets": blocks - 5 if rx else 0, "rx_completed_blocks": blocks - 5 if rx else 0,
        "rx_invalid_packets": 0, "rx_crc_errors": 0, "rx_wrong_session": 0, "rx_wrong_config": 0,
        "rx_duplicate_packets": 0, "rx_stale_packets": 0, "rx_future_packets": 0,
        "rx_concealed_frames": 5 * case["frames_per_packet"] if rx else 0, "rx_incomplete_blocks": 0,
        "rx_skipped_playout_blocks": 0, "rx_late_playout_blocks": 0, "rx_render_lateness_max_us": 0,
        "quality": [{"channel": c, "samples": blocks * case["frames_per_packet"],
                     "reference_energy": blocks * case["frames_per_packet"] * 10000,
                     "error_energy": 5 * case["frames_per_packet"] * 100,
                     "error_abs_max": 100} for c in range(case["channels"])] if rx else []}
    return record


class AudioFirmware:
    commands = []
    fail_first_rx, failed = False, False

    def __init__(self, **_):
        self.chunks, self.active, self.role, self.run_id = [], False, "idle", "none"
        self.case = default_plan()["workloads"][0]
        self.duration, self.fail = 100, False

    def open(self): pass
    def close(self): pass
    @property
    def in_waiting(self): return len(self.chunks[0]) if self.chunks else 0
    def read(self, _): return self.chunks.pop(0) if self.chunks else b""
    def emit(self, record):
        wire = b"PERF " + json.dumps({"schema": 1, **record}).encode() + b"\n"
        self.chunks.extend((wire[:70], wire[70:]))

    def write(self, data):
        command = data.decode().strip()
        AudioFirmware.commands.append((self.port, command))
        parts = command.split()
        if command == "perf info":
            self.emit({"type": "info", "source_sha": "a" * 40, "source_digest": "b" * 64, "profile": "fixture-audio"})
        elif command == "perf audio info":
            self.emit({"type": "audio_info", "configured": True, "session_authorized": True,
                "role": self.role, "active": self.active, "radio_initialized": self.active,
                "sta_mac": "020000000001" if self.port == "COM21" else "020000000002",
                "actual_channel": 6, "channel_query_status": 0, "power_save_enum": 0, "power_save_status": 0,
                **configuration(self.case)})
        elif parts[:3] == ["perf", "audio", "config"]:
            self.case = {"codec": parts[3], "channels": int(parts[4]), "sample_hz": int(parts[5]),
                "frames_per_packet": int(parts[6]), "osr": int(parts[7]), "rate_mbps": int(parts[8])}
            self.emit({"type": "audio_ack", "action": "config", "ok": True})
        elif parts[:3] in (["perf", "audio", "serve"], ["perf", "audio", "run"]):
            self.role, self.run_id, self.duration, self.active = "rx" if parts[2] == "serve" else "tx", parts[3], int(parts[4]), True
            self.fail = False
            if self.role == "rx" and self.fail_first_rx and not AudioFirmware.failed:
                self.fail = AudioFirmware.failed = True
            self.emit({"type": "audio_ack", "action": parts[2], "ok": True})
            self.emit({"type": "audio_run", "run_id": self.run_id, "role": self.role})
        elif command == "perf audio export":
            record = summary(self.case, self.role, self.run_id, self.duration)
            if self.fail:
                record.update(complete=False, timed_out=True, rx_accepted_packets=0, rx_completed_blocks=0)
            self.emit(record)
        elif parts[:2] == ["perf", "audio"]:
            if parts[2] == "stop": self.active = False
            self.emit({"type": "audio_ack", "action": parts[2], "ok": True})
        else:
            self.emit({"type": "ack", "action": "fixture", "ok": True})
        return len(data)


def arguments(root):
    plan = default_plan()
    plan.update(duration_ms=100, repetitions=1)
    plan["workloads"] = plan["workloads"][:2]
    path = root / "plan.json"
    path.write_text(json.dumps(plan))
    image = root / "fixture.bin"
    image.write_bytes(b"not firmware")
    identities = {"schema": "cyd-perf-link-identities/1"}
    for name, port in (("cyd", "COM21"), ("peer", "COM22")):
        identities[name] = {"board": "fixture only", "port": port, "profile": "fixture-audio", "source_sha": "a" * 40,
            "source_digest": "b" * 64, "firmware_file": str(image), "firmware_sha256": hashlib.sha256(image.read_bytes()).hexdigest()}
    identity_path = root / "identities.json"
    identity_path.write_text(json.dumps(identities))
    return argparse.Namespace(plan=path, identities=identity_path, output=root / "capture")


class AudioCaptureTests(unittest.TestCase):
    def test_default_is_pcm_only_with_explicit_optional_pdm_and_reverse(self):
        plan = default_plan()
        validate_plan(plan)
        self.assertEqual(len(plan["workloads"]) * plan["repetitions"], 24)
        self.assertEqual({case["codec"] for case in plan["workloads"]}, {"pcm16"})
        self.assertEqual(len(default_plan(reverse=True, include_pdm=True)["workloads"]), 40)
        plan["channel"] = 11
        with self.assertRaises(ValueError): validate_plan(plan)

    def test_rx_first_no_stream_queries_failed_case_continuation_and_private_session(self):
        for fail in (False, True):
            with tempfile.TemporaryDirectory() as temporary:
                args = arguments(pathlib.Path(temporary))
                AudioFirmware.commands, AudioFirmware.fail_first_rx, AudioFirmware.failed = [], fail, False
                with mock.patch.dict("sys.modules", {"serial": mock.Mock(Serial=AudioFirmware)}), mock.patch("audio_mvp_capture.time.perf_counter", Clock()), mock.patch("audio_mvp_capture.secrets.token_hex", return_value="cafebabe"):
                    self.assertEqual(capture(args), 2 if fail else 0)
                result = json.loads((args.output / "results.json").read_text())
                self.assertEqual(result["status"], "FAILED" if fail else "CAPTURE COMPLETE")
                self.assertEqual(result["runs"][-1]["status"], "MEASURED")
                self.assertIsNone(result["runs"][-1]["derived"]["one_way_latency_us"])
                commands = [command for _, command in AudioFirmware.commands]
                for index, command in enumerate(commands):
                    if command.startswith("perf audio run "):
                        self.assertTrue(any(value.startswith("perf audio serve ") for value in commands[:index]))
                        next_export = next(i for i in range(index + 1, len(commands)) if commands[i] == "perf audio export")
                        self.assertFalse(any(value == "perf audio info" for value in commands[index + 1:next_export]))
                for path in args.output.rglob("*"):
                    if path.is_file(): self.assertNotIn(b"cafebabe", path.read_bytes())
                analysis = analyze(result)
                self.assertEqual(len(analysis["groups"]), 1 if fail else 2)
                self.assertEqual(analysis["capture_status"], result["status"])

    def test_incomplete_zero_traffic_wrong_window_or_quality_cannot_be_measured(self):
        case = default_plan()["workloads"][0]
        run = {"run_id": "a001_1", "settings": case, "sender_summary": summary(case, "tx"), "receiver_summary": summary(case, "rx")}
        self.assertGreater(validate_run(run, 100, 23063)["source_nonconcealed_rendered_fraction"], 0)
        for endpoint, change in (("receiver", {"complete": False}), ("receiver", {"rx_accepted_packets": 0}),
            ("sender", {"run_id": "stale"}), ("receiver", {"phy_config_status": 1}),
            ("sender", {"source_total_blocks": 1}), ("receiver", {"quality": []})):
            invalid = copy.deepcopy(run)
            invalid[endpoint + "_summary"].update(change)
            with self.assertRaises(RuntimeError): validate_run(invalid, 100, 23063)


if __name__ == "__main__":
    unittest.main()
