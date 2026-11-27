#!/usr/bin/env python3
"""Owned two-board wireless characterisation over two serial control links.

Data traffic runs only between the UART-identified ESP boards. Provisioning TX
and RX are omitted from logs; public RX uses length-preserving secret redaction.
No flashing, credential persistence, network scanning or PC data-plane probes.
"""
from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import ipaddress
import json
import pathlib
import re
import secrets
import sys
import time
from typing import Any

from capture import interpreted_records, parse_perf, utc_now, validate_end

TOPOLOGIES = ("direct_espnow", "direct_softap", "through_ap", "direct_ble")


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class SecretGuard:
    def __init__(self) -> None:
        self.patterns: set[bytes] = set()

    def add(self, value: str) -> None:
        if not value:
            return
        literal = value.encode("utf-8")
        self.patterns.update((literal, literal.hex().encode(), literal.hex().upper().encode(),
                              json.dumps(value, ensure_ascii=True)[1:-1].encode()))

    @property
    def longest(self) -> int:
        return max(map(len, self.patterns), default=1)

    def redact_bytes(self, payload: bytes) -> bytes:
        for pattern in sorted(self.patterns, key=len, reverse=True):
            payload = payload.replace(pattern, b"*" * len(pattern))
        return payload

    def clean(self, value: Any) -> Any:
        if isinstance(value, dict):
            return {key: "[REDACTED]" if re.fullmatch(r"(?:ssid|password|passphrase|psk|token|session_token|credentials?)(?:_hex)?", key, re.I)
                    else self.clean(item) for key, item in value.items()}
        if isinstance(value, list):
            return [self.clean(item) for item in value]
        if isinstance(value, str):
            return self.redact_bytes(value.encode("utf-8")).decode("utf-8", errors="replace")
        return value


class StreamingRedactor:
    def __init__(self, guard: SecretGuard) -> None:
        self.guard = guard
        self.pending = b""

    def feed(self, payload: bytes) -> bytes:
        # Retain potential partial matches and retain already-redacted tails,
        # avoiding leakage when a full match straddles the flush boundary.
        self.pending = self.guard.redact_bytes(self.pending + payload)
        count = max(0, len(self.pending) - self.guard.longest + 1)
        output, self.pending = self.pending[:count], self.pending[count:]
        return output

    def flush(self) -> bytes:
        output, self.pending = self.guard.redact_bytes(self.pending), b""
        return output


def read_private(path: pathlib.Path, guard: SecretGuard) -> dict:
    resolved = path.resolve()
    if any((parent / ".git").exists() for parent in resolved.parents):
        raise ValueError("Provisioning file must be outside every Git repository")
    config = json.loads(resolved.read_text(encoding="utf-8-sig"))
    if config.get("schema") != "cyd-perf-link-private/1":
        raise ValueError("Expected cyd-perf-link-private/1 private configuration")
    for topology in ("direct_softap", "through_ap"):
        section = config.get(topology, {})
        ssid, password = section.get("ssid"), section.get("password")
        if not isinstance(ssid, str) or not isinstance(password, str):
            raise ValueError("Private topology configuration requires SSID/password strings")
        guard.add(ssid)
        guard.add(password)
        if not 1 <= len(ssid.encode("utf-8")) <= 32 or len(password.encode("utf-8")) > 64:
            raise ValueError("Private Wi-Fi field exceeds target byte limits")
        if password and len(password.encode("utf-8")) < 8:
            raise ValueError("Private Wi-Fi password must be empty or at least eight bytes")
    return config


def default_plan(include_ble: bool = False) -> dict:
    cases = []
    for topology in TOPOLOGIES:
        if topology == "direct_ble":
            if include_ble:
                for local in ("menu", "full_gate_drone"):
                    for mode, pattern, payload, pps in (("idle_connected", "none", 64, 1), ("rtt", "rtt", 64, 20), ("ble_paced", "oneway", 200, 50)):
                        cases.append({"name": f"{topology}/{local}/{mode}", "topology": topology,
                                      "local_workload": local, "mode": mode, "transport": "ble",
                                      "pattern": pattern, "payload_bytes": payload, "packets_per_second": pps})
            continue
        modes = [("idle", "espnow" if topology == "direct_espnow" else "udp", "none", 0, 0),
                 ("rtt", "espnow" if topology == "direct_espnow" else "udp", "rtt", 64, 50 if topology == "direct_espnow" else 100),
                 ("udp_paced" if topology != "direct_espnow" else "espnow_paced",
                  "espnow" if topology == "direct_espnow" else "udp", "oneway", 200 if topology == "direct_espnow" else 1024, 200)]
        if topology != "direct_espnow":
            modes.append(("tcp_goodput", "tcp", "oneway", 1024, 0))
        for local in ("menu", "full_gate_drone"):
            for mode, transport, pattern, payload, pps in modes:
                cases.append({"name": f"{topology}/{local}/{mode}", "topology": topology,
                              "local_workload": local, "mode": mode, "transport": transport,
                              "pattern": pattern, "payload_bytes": payload, "packets_per_second": pps})
    return {"schema": "cyd-perf-link-plan/1", "seed": 23063, "repetitions": 3,
            "warmup_ms": 2000, "duration_ms": 10000, "direct_channel": 6,
            "port": 41832, "reply_drain_ms": 700, "workloads": cases}


def validate_plan(plan: dict) -> None:
    if plan.get("schema") != "cyd-perf-link-plan/1" or not plan.get("workloads"):
        raise ValueError("Expected a nonempty cyd-perf-link-plan/1 workload array")
    if not 1 <= plan.get("repetitions", 3) <= 20 or not 1 <= plan.get("direct_channel", 6) <= 13:
        raise ValueError("Invalid repetitions/direct channel")
    if not 1 <= plan.get("duration_ms", 10000) <= 60000 or not 0 <= plan.get("warmup_ms", 2000) <= 10000:
        raise ValueError("Wireless windows must be bounded to 60 seconds plus <=10 seconds warmup")
    if not 1 <= plan.get("port", 41832) <= 65535:
        raise ValueError("Invalid known-peer data port")
    for case in plan["workloads"]:
        if case.get("topology") not in TOPOLOGIES or case.get("local_workload") not in ("menu", "full_gate_drone"):
            raise ValueError("Unknown topology/local workload")
        if case.get("transport") not in ("espnow", "udp", "tcp", "ble") or case.get("pattern") not in ("none", "rtt", "oneway"):
            raise ValueError("Unknown bounded link transport/pattern")
        payload, pps = case.get("payload_bytes"), case.get("packets_per_second")
        if not isinstance(payload, int) or not 0 <= payload <= (200 if case["transport"] in ("espnow", "ble") else 1024):
            raise ValueError("Payload exceeds known target limit")
        if not isinstance(pps, int) or not 0 <= pps <= 1000:
            raise ValueError("Paced link rate must be 0..1000 packets/s")


def read_identities(path: pathlib.Path) -> dict:
    config = json.loads(path.read_text(encoding="utf-8-sig"))
    if config.get("schema") != "cyd-perf-link-identities/1":
        raise ValueError("Expected cyd-perf-link-identities/1")
    for name in ("cyd", "peer"):
        entry = config.get(name, {})
        for field, pattern in (("source_sha", r"[0-9a-f]{40}"), ("source_digest", r"[0-9a-f]{64}"), ("firmware_sha256", r"[0-9a-f]{64}")):
            if not re.fullmatch(pattern, entry.get(field, "")):
                raise ValueError("Exact board source/image identity is required")
        if not entry.get("board") or not entry.get("profile") or not re.fullmatch(r"COM[1-9][0-9]*", entry.get("port", ""), re.I):
            raise ValueError("Board, profile and confirmed Windows port are required")
        firmware = pathlib.Path(entry["firmware_file"])
        if not firmware.is_absolute():
            firmware = path.parent / firmware
        if digest(firmware) != entry["firmware_sha256"]:
            raise ValueError("Supplied firmware file differs from the identified image")
        entry["firmware_file"] = str(firmware.resolve())
    if config["cyd"]["port"].upper() == config["peer"]["port"].upper():
        raise ValueError("Two distinct owner-identified serial ports are required")
    return config


class Endpoint:
    def __init__(self, name: str, identity: dict, output: pathlib.Path, guard: SecretGuard) -> None:
        import serial
        self.name, self.identity, self.guard = name, identity, guard
        self.output = output
        output.mkdir(parents=True, exist_ok=False)
        self.raw = (output / "serial-rx.redacted.bin").open("wb")
        self.events = (output / "serial-events.redacted.jsonl").open("w", encoding="utf-8", buffering=1)
        self.target = (output / "target.redacted.jsonl").open("w", encoding="utf-8", buffering=1)
        self.started = time.perf_counter()
        self.offset = 0
        self.pending = bytearray()
        self.redactor = StreamingRedactor(guard)
        self.records: list[dict] = []
        self.native_usb = False
        self.private_records: list[dict] = []
        self.errors: list[str] = []
        self.serial = serial.Serial(port=None, baudrate=identity.get("baud", 533333), timeout=0.02, write_timeout=3)
        self.serial.dtr = self.serial.rts = False
        self.serial.port = identity["port"]
        self.serial.open()
        self.log("open", port=identity["port"], baud=identity.get("baud", 533333), dtr=False, rts=False)

    def log(self, event: str, **details) -> None:
        self.events.write(json.dumps(self.guard.clean({"event": event, "host_elapsed_s": time.perf_counter() - self.started,
                                                       "utc": utc_now(), **details}), allow_nan=False) + "\n")

    def read(self, private=False) -> None:
        data = self.serial.read(max(1, min(8192, self.serial.in_waiting)))
        if not data:
            return
        arrived = time.perf_counter() - self.started
        if not private:
            self.raw.write(self.redactor.feed(data))
            self.log("rx", offset=self.offset, bytes=len(data), redacted=True)
            self.offset += len(data)
        self.pending.extend(data)
        if len(self.pending) > 1024 * 1024:
            raise ValueError("Unterminated endpoint telemetry exceeds bounded limit")
        while b"\n" in self.pending:
            line, _, remainder = self.pending.partition(b"\n")
            self.pending = bytearray(remainder)
            try:
                record = parse_perf(line)
            except (ValueError, UnicodeDecodeError) as error:
                if private:
                    raise ValueError("Malformed private acknowledgement; RX omitted") from None
                self.errors.append(self.guard.clean(str(error)))
                continue
            if record is not None:
                record = self.guard.clean(record)
                record["host_received_elapsed_s"] = arrived
                if private:
                    self.private_records.append(record)
                else:
                    self.records.append(record)
                    self.target.write(json.dumps(record, allow_nan=False) + "\n")

    def wait(self, seconds: float, private=False) -> None:
        deadline = time.perf_counter() + seconds
        while time.perf_counter() < deadline:
            self.read(private=private)

    def send(self, command: str) -> None:
        if command.startswith(("perf link provision ", "perf link allow ")):
            raise ValueError("Sensitive commands require a private transaction")
        self.log("tx", command=command)
        self._write(command)

    def _write(self, command: str) -> None:
        if "\n" in command or "\r" in command or len(command) > 500:
            raise ValueError("Command must be one bounded ASCII line")
        data = (command + "\n").encode("ascii")
        if self.serial.write(data) != len(data):
            raise OSError("Short endpoint serial write")

    def until(self, kinds: set[str], cursor: int, timeout=15, run_id=None, private=False) -> list[dict]:
        deadline = time.perf_counter() + timeout
        recover = (not private and self.native_usb and self.identity.get("usb_progress_poll", True)
                   and kinds in ({"link_info"}, {"link_summary"}))
        expected = next(iter(kinds)) if recover else None
        last_offset, last_progress = self.offset, time.perf_counter()
        last_poke, pokes = float("-inf"), 0
        while time.perf_counter() < deadline:
            if self.errors:
                raise RuntimeError("Malformed public telemetry; redacted bytes retained")
            records = self.private_records if private else self.records
            matched = False
            for record in records[cursor:]:
                if record["type"] in ("error", "link_error", "kernel_error"):
                    raise RuntimeError("Endpoint rejected command: " + json.dumps(self.guard.clean(record)))
                if record["type"] in ("ack", "link_ack") and record.get("ok") is False:
                    raise RuntimeError("Endpoint setup failed: " + json.dumps(self.guard.clean(record)))
                if record["type"] in kinds and (run_id is None or record.get("run_id") == run_id):
                    matched = True
            # A recovery query produces another metadata reply. Consume those
            # replies in this same control/export phase before another command,
            # and return the latest info snapshot rather than stale metadata.
            info_replies = sum(record["type"] == "link_info" for record in records[cursor:])
            if matched and (not recover or info_replies >= pokes + (1 if expected == "link_info" else 0)):
                return records[cursor:]
            self.read(private=private)
            at = time.perf_counter()
            if self.offset != last_offset:
                last_offset, last_progress = self.offset, at
            if recover and pokes < 3 and at - last_progress >= .75 and at - last_poke >= 1:
                pending = bytes(self.pending).lstrip()
                allowed = {expected}
                if matched and expected == "link_summary":
                    allowed.add("link_info")
                partial_type = next((kind for kind in allowed if pending.startswith(b"PERF {") and
                    re.search(rb'"type"\s*:\s*"' + kind.encode("ascii") + rb'"', pending)), None)
                if partial_type:
                    pokes += 1
                    self.log("usb_progress_poll", awaited_record=expected, pending_record=partial_type,
                             attempt=pokes, stalled_for_s=at - last_progress, rx_offset=self.offset,
                             pending_bytes=len(self.pending), command="perf link info",
                             scope="native USB metadata/export only; outside timed traffic; private transactions excluded")
                    self.send("perf link info")
                    last_poke = at
        raise TimeoutError("Endpoint acknowledgement/summary timeout (no private contents recorded)")

    def request(self, command: str, kinds=None, timeout=15, run_id=None) -> list[dict]:
        cursor = len(self.records)
        self.send(command)
        return self.until(kinds or {"ack", "link_ack"}, cursor, timeout, run_id)

    def private_request(self, command: str, purpose: str) -> None:
        # Omit all RX in this transaction. Delayed public SDK output is also
        # protected by the streaming credential redactor after this boundary.
        self.raw.write(self.redactor.flush())
        self.pending.clear()
        self.private_records.clear()
        self.log("private_transaction", purpose=purpose, state="begin", tx_payload_omitted=True, rx_omitted=True)
        try:
            self._write(command)
            self.until({"ack", "link_ack", "link_provisioned"}, 0, private=True)
            self.wait(0.3, private=True)
        finally:
            self.private_records.clear()
            self.pending.clear()
            self.log("private_transaction", purpose=purpose, state="end", tx_payload_omitted=True, rx_omitted=True)

    def info(self) -> dict:
        records = self.request("perf link info", {"link_info"})
        return [record for record in records if record["type"] == "link_info"][-1]

    def close(self) -> None:
        self.raw.write(self.redactor.flush())
        self.serial.close()
        for stream in (self.raw, self.events, self.target):
            stream.close()


def verify_identity(endpoint: Endpoint) -> dict:
    records = endpoint.request("perf info", {"info"})
    info = next(record for record in records if record["type"] == "info")
    for key, field in (("source_sha", "source_sha"), ("source_digest", "source_digest"), ("profile", "profile")):
        if info.get(key) != endpoint.identity[field]:
            raise RuntimeError("Target source/profile differs from identified image")
    endpoint.native_usb = info.get("native_usb") is True
    return {"identity": endpoint.guard.clean(endpoint.identity), "target_info": records, "link_info": endpoint.info(),
            "usb_progress_poll_policy": {"enabled": endpoint.native_usb and endpoint.identity.get("usb_progress_poll", True),
                "native_usb_reported": endpoint.native_usb, "record_types": ["link_info", "link_summary"],
                "maximum_per_wait": 3, "minimum_stall_s": .75, "minimum_query_spacing_s": 1,
                "scope": "metadata/export waits only; never quiet traffic or private provisioning"},
            "binary_digest_attested_by_target": False}


def provision(endpoint: Endpoint, section: dict) -> None:
    ssid = section["ssid"].encode("utf-8").hex()
    password = section["password"].encode("utf-8").hex() or "-"
    endpoint.private_request(f"perf link provision {ssid} {password}", "credential_provision")


def ready(endpoint: Endpoint, timeout=35) -> dict:
    deadline = time.perf_counter() + timeout
    while time.perf_counter() < deadline:
        info = endpoint.info()
        if info.get("mode_ready") is True:
            return info
        endpoint.wait(0.8)
    raise TimeoutError("Owned endpoint did not enter requested radio topology; no unrelated devices probed")


def configure(topology: str, endpoints: dict[str, Endpoint], private: dict, plan: dict) -> dict:
    for endpoint in endpoints.values():
        endpoint.request("perf link stop")
        endpoint.request("perf link mode off 0")
    cyd, peer = endpoints["cyd"], endpoints["peer"]
    channel = plan.get("direct_channel", 6)
    if topology in ("direct_espnow", "direct_ble"):
        for endpoint in endpoints.values():
            endpoint.request(f"perf link mode {'ble 0' if topology == 'direct_ble' else 'espnow ' + str(channel)}")
    else:
        for endpoint in endpoints.values():
            provision(endpoint, private[topology])
        if topology == "direct_softap":
            peer.request(f"perf link mode ap {channel}")
            ready(peer)
            cyd.request("perf link mode sta 0")
        else:
            for endpoint in endpoints.values():
                endpoint.request("perf link mode sta 0")
    infos = {name: endpoint.info() if topology == "direct_ble" else ready(endpoint) for name, endpoint in endpoints.items()}
    if topology == "direct_ble" and not all(info.get("ble_compiled") is True for info in infos.values()):
        raise RuntimeError("Directed BLE transport was not compiled on both identified boards")
    if topology == "direct_espnow" and len({info.get("actual_channel") for info in infos.values()}) != 1:
        raise RuntimeError("Direct ESP-NOW endpoints reported different channels")
    return infos


def authorize_pair(endpoints: dict[str, Endpoint], infos: dict, port: int, guard: SecretGuard, topology: str) -> None:
    token = secrets.token_hex(16)
    guard.add(token)
    for name, endpoint in endpoints.items():
        other = infos["peer" if name == "cyd" else "cyd"]
        mac = other.get("bt_mac" if topology == "direct_ble" else "sta_mac")
        ip = "0.0.0.0" if topology in ("direct_espnow", "direct_ble") else other.get("ip", "0.0.0.0")
        if not isinstance(mac, str) or not re.fullmatch(r"[0-9a-fA-F]{12}", mac) or int(mac[:2], 16) & 1 or int(mac, 16) == 0:
            raise ValueError("Known peer MAC is absent/invalid")
        address = ipaddress.IPv4Address(ip)
        if address.is_multicast or address.is_loopback or (topology not in ("direct_espnow", "direct_ble") and address.is_unspecified):
            raise ValueError("Known peer IPv4 is absent/invalid")
        endpoint.private_request(f"perf link allow {mac} {ip} {port} {token}", "known_peer_session")


def scene(endpoint: Endpoint, local: str, seed: int) -> list[dict]:
    records = []
    commands = [f"perf seed {seed}", "perf orient 0", "perf backdrop 0", "perf scene menu"]
    if local == "full_gate_drone":
        commands += ["perf scene visions", "perf vision 1 1 1 0", "perf combined drone"]
    for command in commands:
        records += endpoint.request(command)
    return records


def connected(endpoints: dict[str, Endpoint], field: str, timeout=15, minimum_ble_mtu=0) -> dict:
    deadline = time.perf_counter() + timeout
    while time.perf_counter() < deadline:
        infos = {name: endpoint.info() for name, endpoint in endpoints.items()}
        if all(info.get(field) is True and (not minimum_ble_mtu or
               (info.get("ble_ready") is True and info.get("ble_mtu", 0) >= minimum_ble_mtu
                and info.get("ble_actual_payload_limit", 0) >= minimum_ble_mtu - 43)) for info in infos.values()):
            return infos
        for endpoint in endpoints.values():
            endpoint.wait(0.2)
    raise TimeoutError("Known-peer connection was not established; no unrelated endpoints attempted")


def derive_link(client: dict, peer: dict, duration_ms: int, case: dict) -> dict:
    for record in (client, peer):
        if record.get("complete") is not True:
            raise RuntimeError("Link transport did not complete its marked packet window")
        bins = record.get("rtt_histogram_counts", [])
        if len(bins) != 32 or any(not isinstance(value, int) or value < 0 for value in bins) or sum(bins) != record.get("rtt_count"):
            raise RuntimeError("RTT histogram count/retention is inconsistent")
    if client.get("duration_us") != duration_ms * 1000:
        raise RuntimeError("Link initiator window differs from the profiler window")
    duration = duration_ms / 1000
    accepted, received = client.get("tx_api_accepted_packets", 0), peer.get("rx_unique_packets", 0)
    rtt_count = client.get("rtt_count", 0)
    derived = {"initiator_duration_s": duration, "sender_api_accepted_packets": accepted,
               "receiver_unique_packets": received,
               "receiver_payload_goodput_bytes_per_s": peer.get("rx_payload_bytes", 0) / duration,
               "sender_accepted_payload_bytes_per_s": client.get("tx_payload_bytes", 0) / duration,
               "accepted_not_observed_at_receiver": accepted - received if received <= accepted else None,
               "receiver_vs_sender_counts_consistent": received <= accepted,
               "accepted_packet_loss_fraction": (accepted - received) / accepted if accepted and received <= accepted else None,
               "rtt_samples": rtt_count,
               "rtt_mean_us": client.get("rtt_sum_us", 0) / rtt_count if rtt_count else None,
               "rtt_max_us": client.get("rtt_max_us") if rtt_count else None,
               "adjacent_rtt_absolute_difference_mean_us": client.get("rtt_adjacent_abs_difference_sum_us", 0) / client["rtt_adjacent_abs_difference_count"]
                   if client.get("rtt_adjacent_abs_difference_count") else None,
               "adjacent_rtt_absolute_difference_max_us": client.get("rtt_adjacent_abs_difference_max_us") if client.get("rtt_adjacent_abs_difference_count") else None,
               "method": "Receiver unique payload bytes / initiator fixed marked steady window; receive tail included; excludes 40-byte application header and PHY overhead. API acceptance is not peer receipt.",
               "jitter_method": "Absolute difference between adjacent received RTT values; not one-way delay jitter.",
               "rtt_percentile_method": "ceil-log2 histogram upper bound capped at exact observed maximum; p99 omitted below100 samples"}
    for percent in (50, 95, 99):
        value = None
        if rtt_count and (percent != 99 or rtt_count >= 100):
            target, cumulative = (rtt_count * percent + 99) // 100, 0
            for index, count in enumerate(client["rtt_histogram_counts"]):
                cumulative += count
                if cumulative >= target:
                    value = min((1 << index) if index < 31 else 0xFFFFFFFF, client["rtt_max_us"])
                    break
        derived[f"rtt_p{percent}_us_upper"] = value
    if not accepted or not received or (case["pattern"] == "rtt" and not rtt_count):
        derived["traffic_established"] = False
    else:
        derived["traffic_established"] = True
    return derived


def execute_run(run: dict, endpoints: dict[str, Endpoint], infos: dict, plan: dict,
                guard: SecretGuard, endpoint_metadata: dict) -> None:
    case, run_id = run["settings"], run["run_id"]
    cyd, peer = endpoints["cyd"], endpoints["peer"]
    for endpoint in endpoints.values():
        endpoint.request("perf link stop")
    authorize_pair(endpoints, infos, plan.get("port", 41832), guard, case["topology"])
    run["setup_target"] = scene(cyd, case["local_workload"], plan.get("seed", 23063))
    cyd.wait(0.25)
    cursors = {name: len(endpoint.records) for name, endpoint in endpoints.items()}
    run["target_record_start_indices"] = cursors
    if case["pattern"] != "none" or case["transport"] == "ble":
        peer.request(f"perf link serve {run_id} {case['transport']}")
        pattern = case["pattern"] if case["pattern"] != "none" else "oneway"
        cyd.request(f"perf link arm {case['transport']} {pattern} {case['payload_bytes']} {case['packets_per_second']}")
        if case["transport"] in ("tcp", "ble"):
            run["connection_info"] = connected(endpoints, "tcp_connected" if case["transport"] == "tcp" else "ble_connected",
                                                 minimum_ble_mtu=case["payload_bytes"] + 43 if case["transport"] == "ble" else 0)
        if case["pattern"] == "none":
            cyd.request("perf link idle")
    warmup, duration = plan.get("warmup_ms", 2000), plan.get("duration_ms", 10000)
    cyd.request(f"perf run {run_id} {warmup} {duration}", {"run"}, run_id=run_id)
    deadline = time.perf_counter() + (warmup + duration + plan.get("reply_drain_ms", 700)) / 1000
    while time.perf_counter() < deadline:
        for endpoint in endpoints.values():
            endpoint.read()
    cyd.send("perf export")
    profile = cyd.until({"end"}, cursors["cyd"], timeout=30, run_id=run_id)
    end = validate_end(profile, run_id, duration)
    info = next(record for record in endpoint_metadata["cyd"]["target_info"] if record["type"] == "info")
    run["cyd_profile"] = interpreted_records(profile, info)
    run["profile_status"] = "MEASURED"
    run["frames_per_second"] = end["frames"] * 1e6 / end["elapsed_us"]
    if case["pattern"] != "none":
        for endpoint in endpoints.values():
            endpoint.request("perf link stop")
        for name, endpoint in endpoints.items():
            endpoint.send("perf link export")
            exported = endpoint.until({"link_summary"}, cursors[name], timeout=20, run_id=run_id)
            run[name + "_link"] = [record for record in exported if record["type"] == "link_summary" and record.get("run_id") == run_id]
        run["derived"] = derive_link(run["cyd_link"][-1], run["peer_link"][-1], duration, case)
        if not run["derived"]["traffic_established"]:
            raise RuntimeError("No valid end-to-end traffic/RTT established; target profile retained separately")
    else:
        run["cyd_link"] = [cyd.info()]
        run["peer_link"] = [peer.info()]
    run["post_info"] = {name: endpoint.info() for name, endpoint in endpoints.items()}
    run["status"] = "MEASURED"


def save(result: dict, output: pathlib.Path, guard: SecretGuard) -> None:
    public = guard.clean(result)
    temporary = output / "results.tmp"
    temporary.write_text(json.dumps(public, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temporary.replace(output / "results.json")
    rows, profile_metrics, profile_samples, derived = [], [], [], []
    for run in public["runs"]:
        context = {"run_id": run["run_id"], "workload": run["workload"], "status": run["status"],
                   "topology": run["settings"]["topology"], "local_workload": run["settings"]["local_workload"]}
        for name in ("cyd", "peer"):
            identity = public["endpoints"].get(name, {}).get("identity", {})
            base = {**context, "endpoint": name, "source_sha": identity.get("source_sha"),
                    "firmware_sha256": identity.get("firmware_sha256"), "profile": identity.get("profile")}
            for record in run.get(name + "_link", []):
                rows.append({**base, **record})
            if name == "cyd":
                for record in run.get("cyd_profile", []):
                    if record["type"] in ("metric", "i2s_events"):
                        profile_metrics.append({**base, "profile_status": run.get("profile_status"), **record})
                    elif record["type"] == "sample":
                        profile_samples.append({**base, "profile_status": run.get("profile_status"), **record})
        derived.append({**context, "profile_status": run.get("profile_status"), "frames_per_second": run.get("frames_per_second"),
                        **run.get("derived", {}), "failure": run.get("error")})
    for filename, records in (("link-metrics.csv", rows), ("profile-metrics.csv", profile_metrics),
                              ("profile-samples.csv", profile_samples), ("link-derived.csv", derived)):
        fields = list(dict.fromkeys(key for row in records for key in row)) or ["run_id", "workload", "status"]
        with (output / filename).open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            for row in records:
                writer.writerow({key: json.dumps(value, separators=(",", ":")) if isinstance(value, (dict, list)) else value for key, value in row.items()})


def capture(args) -> int:
    guard = SecretGuard()
    private = read_private(args.provision_private, guard)
    identities = read_identities(args.identities)
    plan = json.loads(args.plan.read_text(encoding="utf-8-sig"))
    validate_plan(plan)
    args.output.mkdir(parents=True, exist_ok=False)
    result = {"schema": "cyd-perf-link-capture/1", "evidence_class": "physical_measured", "status": "IN PROGRESS",
              "harness_sha256": digest(pathlib.Path(__file__)),
              "capture_module_sha256": digest(pathlib.Path(__file__).with_name("capture.py")),
              "started_utc": utc_now(), "plan": plan, "plan_sha256": digest(args.plan), "endpoints": {}, "runs": [],
              "privacy": "Provision/allow TX and transaction RX omitted; public RX length-preserving redaction of registered literal, hex and JSON-escaped secrets. No private configuration path/hash recorded.",
              "limitations": ["Target source/profile/digest are checked; local binary SHA256 needs the owner's separate flash receipt, not target attestation.",
                              "Host receive timestamps are serial transport observations, not synchronized target clocks.",
                              "RTT uses one initiator clock; peer counters use marked measurement packets plus a bounded drain, not equal board wall clocks.",
                              "Only the two UART-identified owned boards are configured as data peers. AP credentials stay in RAM and outside logs.",
                              "Classic ESP32 temperature is an undocumented uncalibrated RTC-library indication; numeric conversion is not calibrated temperature."]}
    endpoints = {}
    try:
        for name in ("cyd", "peer"):
            endpoints[name] = Endpoint(name, identities[name], args.output / name, guard)
            endpoints[name].wait(0.2)
            result["endpoints"][name] = verify_identity(endpoints[name])
        save(result, args.output, guard)
        current, topology_error = None, None
        infos = {}
        number = 0
        for case in plan["workloads"]:
            if current != case["topology"]:
                current = case["topology"]
                try:
                    infos = configure(current, endpoints, private, plan)
                    topology_error = None
                except (RuntimeError, TimeoutError, ValueError) as error:
                    infos = {}
                    topology_error = guard.clean(str(error))
            for repetition in range(1, case.get("repetitions", plan.get("repetitions", 3)) + 1):
                number += 1
                run_id = f"l{number:03}_{repetition}"
                run = {"run_id": run_id, "workload": case["name"], "repetition": repetition,
                       "status": "IN PROGRESS", "settings": case, "topology_info": infos}
                result["runs"].append(run)
                print(f"{run_id} {case['name']}", flush=True)
                cursors = {name: len(endpoint.records) for name, endpoint in endpoints.items()}
                if topology_error:
                    run.update(status="BLOCKED", error=topology_error, stage="topology_setup")
                else:
                    try:
                        execute_run(run, endpoints, infos, plan, guard, result["endpoints"])
                    except (RuntimeError, TimeoutError, ValueError) as error:
                        run.update(status="FAILED", error=guard.clean(str(error)))
                        run["partial_target"] = {name: endpoint.records[cursors[name]:] for name, endpoint in endpoints.items()}
                        for endpoint in endpoints.values():
                            try:
                                endpoint.request("perf link stop", timeout=2)
                                if endpoint.name == "cyd":
                                    endpoint.request("perf stop", timeout=2)
                            except Exception:
                                pass
                        run["failure_info"] = {}
                        for name, endpoint in endpoints.items():
                            try:
                                run["failure_info"][name] = endpoint.info()
                            except Exception:
                                run["failure_info"][name] = {"status": "UNAVAILABLE"}
                        if any(endpoint.errors for endpoint in endpoints.values()):
                            raise RuntimeError("Serial telemetry corruption prevents safe case correlation") from None
                save(result, args.output, guard)
        result["execution_complete"] = True
        result["status"] = "CAPTURE COMPLETE" if all(run["status"] == "MEASURED" for run in result["runs"]) else "FAILED"
        result["run_status_counts"] = dict(collections.Counter(run["status"] for run in result["runs"]))
        result["ended_utc"] = utc_now()
    except BaseException as error:
        result["status"] = "INTERRUPTED" if isinstance(error, KeyboardInterrupt) else "FAILED"
        result["error"] = guard.clean(str(error))
        for name, endpoint in endpoints.items():
            result.setdefault("partial_target", {})[name] = endpoint.records
        raise
    finally:
        for endpoint in endpoints.values():
            try:
                endpoint.request("perf link stop", timeout=2)
                endpoint.request("perf link mode off 0", timeout=2)
                endpoint.wait(0.2)
            except Exception:
                pass
            endpoint.close()
        save(result, args.output, guard)
        hashes = {path.relative_to(args.output).as_posix(): digest(path) for path in sorted(args.output.rglob("*")) if path.is_file()}
        (args.output / "artifact-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n", encoding="utf-8")
    return 0 if result["status"] == "CAPTURE COMPLETE" else 2


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    child = commands.add_parser("plan")
    child.add_argument("--output", type=pathlib.Path, required=True)
    child.add_argument("--include-ble", action="store_true", help="Add directed GATT cases only for a separately identified BLE-capable image pair")
    child = commands.add_parser("capture")
    child.add_argument("--identities", type=pathlib.Path, required=True)
    child.add_argument("--provision-private", type=pathlib.Path, required=True)
    child.add_argument("--plan", type=pathlib.Path, required=True)
    child.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    if args.command == "plan":
        plan = default_plan(include_ble=args.include_ble)
        args.output.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
        print(args.output)
        return 0
    try:
        return capture(args)
    except KeyboardInterrupt:
        print("Wireless capture interrupted; redacted partial evidence retained.", file=sys.stderr)
        return 130
    except Exception:
        # Never format arbitrary exception arguments here: private-file errors
        # can carry credential contents. The redacted result has useful detail.
        print("Wireless capture failed; inspect redacted evidence or validate configuration.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
