"""Three-port orchestration fixtures; no serial devices or RF access."""
import copy
import json
import pathlib
import tempfile
import types
import unittest
from unittest import mock

from link_capture import SecretGuard
from sdr_metal_capture import SnapshotDecoder
from sdr_metal_dual_capture import capture, dual_plan, execute_dual_run, read_identities
from test_sdr_metal_capture import TXFixture, snapshot, wire


class Clock:
    def __init__(self): self.now = 0
    def __call__(self): self.now += .001; return self.now


class TX(TXFixture):
    def __init__(self, events):
        super().__init__(); self.events = events; self.dispatches = 0
    def send(self, command, private=False):
        self.events.append("tx"); self.dispatches += 1
        super().send(command, private=private)
    def read(self, nonblocking=False):
        super().read(nonblocking=nonblocking)
        for record in self.records:
            if record.get("type") == "tx_result": record["direct_power_domain_owned_after"] = False


class RX:
    def __init__(self, name, events, tx, delay=0, missing_ack=False, bad_canary=False):
        self.name, self.events, self.tx, self.delay = name, events, tx, delay
        self.decoder, self.stage, self.chunks = SnapshotDecoder(), None, []
        self.missing_ack, self.bad_canary = missing_ack, bad_canary
    @property
    def records(self): return self.decoder.records
    def request(self, command, *_args, **_kwargs):
        assert command == "metal temperature"
        self.events.append(self.name + ":temperature")
        return {"type": "temperature", "temperature_c_driver": 25, "install_status": 0,
            "enable_status": 0, "read_status": 0, "disable_status": 0, "uninstall_status": 0}
    def send(self, command):
        parts = command.split()
        if parts[1] == "read":
            header, payload = snapshot(self.run_id, parts[2], rep=0, source_rep=self.repeats, count=self.count)
            self.chunks.append(wire(header) + payload + b"\n"); return
        self.events.append(self.name + ":arm")
        self.mode, self.run_id = parts[1:3]
        self.count, self.repeats, self.rate = int(parts[4]), int(parts[5]), int(parts[3])
        ack = {"type": "armed", "id": self.run_id, "mode": self.mode, "sample_count": self.count,
            "nominal_rate_hz": self.rate, "repeats_requested": self.repeats, "arm_delay_us": int(parts[6]),
            "inter_snapshot_gap_us": int(parts[7]), "timeout_us": int(parts[8])}
        if not self.missing_ack: self.decoder.feed(wire(ack))
        self.stage = "pending"
    def read(self, nonblocking=False):
        assert nonblocking
        if self.stage == "pending" and (self.tx.dispatches or self.missing_ack):
            if self.delay: self.delay -= 1; return b""
            self.stage = None
            payloads = []
            if self.mode == "capture":
                for rep in range(1, self.repeats + 1):
                    header, payload = snapshot(self.run_id, rep=rep, source_rep=rep, count=self.count)
                    if self.bad_canary: header["tail_canary_ok"] = False
                    payloads += [wire(header), payload, b"\n"]
            end = wire({"type": "run_end", "id": self.run_id, "mode": self.mode, "ok": True,
                "captures_completed": self.repeats, "sample_count_each": self.count, "nominal_rate_hz": self.rate,
                "last_owner_restored": True, "last_canary_ok": True, "transfer_failures_delta": 0,
                "loop_elapsed_us": 1000, "capture_observation_us_sum": 4 * self.repeats})
            data = b"".join(payloads) + end
            # Different split points demonstrate that one RX never supplies the other's bytes.
            self.chunks += [data[:37], data[37:103], data[103:]]
        if self.chunks:
            data = self.chunks.pop(0); self.decoder.feed(data); return data
        return b""


class DualTests(unittest.TestCase):
    def setup_run(self, root, missing=False, bad=False):
        plan = dual_plan()
        case = next(c for c in plan["workloads"] if c["tx_enabled"] and c["tx_mode"] == "gate" and c["baud"] == 0 and c["mask_segment_us"] == 0)
        case.update(samples=16, rx_repeats=1, duration_us=200000)
        events = []; tx = TX(events)
        ports = {"tx": tx, "rx": RX("rx", events, tx), "rx2": RX("rx2", events, tx, delay=3, missing_ack=missing, bad_canary=bad)}
        run = {"run_id": "d001_1", "settings": case}
        folders = {name: root / name for name in ("rx", "rx2")}
        return plan, run, ports, events, folders

    def test_both_arms_precede_exactly_one_tx_and_both_split_raw_payloads_complete(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch("sdr_metal_dual_capture.time.perf_counter", Clock()), mock.patch("sdr_metal_dual_capture.time.sleep"):
            root = pathlib.Path(tmp); plan, run, ports, events, folders = self.setup_run(root)
            self.assertTrue(execute_dual_run(run, ports, plan, root / "plan.json", folders, SecretGuard()))
            self.assertEqual(ports["tx"].dispatches, 1)
            self.assertLess(events.index("rx:arm"), events.index("tx"))
            self.assertLess(events.index("rx2:arm"), events.index("tx"))
            for name in ("rx", "rx2"):
                own = run["receivers"][name]
                self.assertEqual(own["status"], "ACQUIRED")
                self.assertEqual(own["snapshots"][0]["header"]["source_id"], "d001_1")
                self.assertTrue((folders[name] / own["snapshots"][0]["file"]).is_file())
            self.assertIsNone(run["physical_rf_verified"])
            self.assertEqual(run["temperature_after"]["rx2"]["status"], "RECORDED")

    def test_one_receiver_canary_failure_keeps_other_complete_and_does_not_retransmit(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch("sdr_metal_dual_capture.time.perf_counter", Clock()), mock.patch("sdr_metal_dual_capture.time.sleep"):
            root = pathlib.Path(tmp); plan, run, ports, events, folders = self.setup_run(root, bad=True)
            self.assertFalse(execute_dual_run(run, ports, plan, root / "plan.json", folders, SecretGuard()))
            self.assertEqual(run["receivers"]["rx"]["status"], "ACQUIRED")
            self.assertEqual(run["receivers"]["rx2"]["status"], "FAILED")
            self.assertFalse(run["receivers"]["rx2"]["snapshots"][0]["header"]["tail_canary_ok"])
            self.assertEqual(ports["tx"].dispatches, 1)
            self.assertTrue((folders["rx2"] / "snapshot-0001.native32le").is_file())

    def test_missing_second_arm_ack_never_authorizes_tx(self):
        # Missing arm ACK leaves the endpoint uncertain; raw-only drain must not send a query.
        with tempfile.TemporaryDirectory() as tmp, mock.patch("sdr_metal_dual_capture.time.perf_counter", Clock()), mock.patch("sdr_metal_dual_capture.time.sleep"), mock.patch("sdr_metal_dual_capture.SDRPort.read", return_value=b""):
            root = pathlib.Path(tmp); plan, run, ports, events, folders = self.setup_run(root, missing=True)
            self.assertFalse(execute_dual_run(run, ports, plan, root / "plan.json", folders, SecretGuard()))
            self.assertEqual(ports["tx"].dispatches, 0)
            self.assertIn("TX NOT RUN", run["failures"]["tx"])

    def test_duplicate_board_ports_or_macs_rejected_before_open(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp); image = root / "image.bin"; image.write_bytes(b"fixture")
            import hashlib
            expected = {"profile": "s3-metal-snapshot", "source_sha": "a" * 40, "source_digest": "b" * 64, "mac": "1c2904bec3a0"}
            entry = {"expected_info": expected, "firmware_file": str(image), "firmware_sha256": hashlib.sha256(b"fixture").hexdigest(), "port": "COM22", "baud": 115200}
            identities = {"schema": "cyd-sdr-metal-dual-identities/1", "rx": entry, "rx2": copy.deepcopy(entry), "tx": copy.deepcopy(entry)}
            identities["tx"]["expected_info"].update(profile="s3-sdr-metal-tx", tx_source_digest="c" * 64, mac="1c2904becc48")
            identities["tx"]["port"] = "COM13"
            path = root / "ids.json"; path.write_text(json.dumps(identities))
            with self.assertRaisesRegex(ValueError, "Three distinct"): read_identities(path)

    def test_constructor_failure_preserves_original_error_and_parent_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp); plan = dual_plan()
            planpath = root / "plan.json"; planpath.write_text(json.dumps(plan))
            args = types.SimpleNamespace(identities=root / "ids.json", plan=planpath, output=root / "capture", rf_authorized=True)
            with mock.patch("sdr_metal_dual_capture.read_identities", return_value={"tx": {}}), mock.patch("sdr_metal_dual_capture.SDRPort", side_effect=OSError("fixture port unavailable")):
                with self.assertRaisesRegex(OSError, "fixture port unavailable"): capture(args)
            result = json.loads((args.output / "results.json").read_text())
            self.assertEqual(result["status"], "FAILED")
            self.assertIn("fixture port unavailable", result["error"])
            self.assertTrue((args.output / "artifact-hashes.json").is_file())

    def test_closed_children_are_reusable_single_rx_results_and_bind_shared_tx_logs(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch("sdr_metal_dual_capture.time.perf_counter", Clock()), mock.patch("sdr_metal_dual_capture.time.sleep"):
            import hashlib
            root = pathlib.Path(tmp); plan, _run, _ports, events, _folders = self.setup_run(root)
            plan["workloads"] = [_run["settings"]]
            planpath = root / "plan.json"; planpath.write_text(json.dumps(plan))
            identities = {n: {"expected_info": {"profile": "classic-sdr-metal-tx" if n == "tx" else "s3-metal-snapshot", "cpu_mhz": 240}}
                          for n in ("tx", "rx", "rx2")}
            class ClosedTX(TX):
                def __init__(self, name, identity, folder, guard):
                    super().__init__(events); self.identity, self.folder = identity, folder; folder.mkdir()
                def request(self, command, *args, **kwargs):
                    if command == "info": return {**self.identity["expected_info"], "type": "info", "temperature_raw": 130,
                        "capacity_symbols": 64, "trace_capacity": plan["transmitter"]["trace_capacity"], "trace_stride": 0}
                    for prefix, kind in (("reserve ", "reserved"), ("observe ", "observe"), ("init ", "init_path")):
                        if command.startswith(prefix): return {"type": kind, "ok": True}
                    return super().request(command, *args, **kwargs)
                def close(self): (self.folder / "rx.bin").write_bytes(b"shared TX records")
            shared = {}
            def tx_factory(*args): shared["tx"] = ClosedTX(*args); return shared["tx"]
            class ClosedRX(RX):
                def __init__(self, name, identity, folder, guard):
                    super().__init__(name, events, shared["tx"]); self.identity, self.folder = identity, folder; folder.mkdir()
                def request(self, command, *args, **kwargs):
                    if command == "metal info": return {**self.identity["expected_info"], "type": "info"}
                    if command.startswith("metal init "): return {"type": "init", "ok": True, "requested": "phy", "actual_mode": "direct_PHY", "error_code": 0}
                    if command.startswith("metal tune "): return {"type": "tune", "ok": True, **{k: plan["receiver"][k] for k in ("frequency_mhz", "offset_khz", "manual_gain_index")}}
                    return super().request(command, *args, **kwargs)
                def close(self): (self.folder / "rx.bin").write_bytes(self.name.encode())
            args = types.SimpleNamespace(identities=root / "ids.json", plan=planpath, output=root / "capture", rf_authorized=True)
            with mock.patch("sdr_metal_dual_capture.read_identities", return_value=identities), mock.patch("sdr_metal_dual_capture.SDRPort", side_effect=tx_factory), mock.patch("sdr_metal_dual_capture.MetalRX", ClosedRX):
                self.assertEqual(capture(args), 0)
            parent = json.loads((args.output / "results.json").read_text())
            self.assertEqual(parent["status"], "CAPTURE COMPLETE")
            for name in ("rx", "rx2"):
                folder = args.output / name
                child = json.loads((folder / "results.json").read_text())
                self.assertEqual(child["schema"], "cyd-sdr-metal-capture/1")
                self.assertEqual(child["status"], "CAPTURE COMPLETE")
                self.assertEqual(child["runs"][0]["shared_exposure_id"], "d001_1")
                self.assertEqual(child["shared_tx_artifacts_sha256"]["tx/rx.bin"], hashlib.sha256(b"shared TX records").hexdigest())
                for path, expected in json.loads((folder / "artifact-hashes.json").read_text()).items():
                    self.assertEqual(hashlib.sha256((folder / path).read_bytes()).hexdigest(), expected)


if __name__ == "__main__": unittest.main()
