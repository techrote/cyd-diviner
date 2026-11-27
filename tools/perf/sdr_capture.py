#!/usr/bin/env python3
"""IQS1 integrity/index decoder and finite SDR experiment capture backend."""
from __future__ import annotations

import argparse
import binascii
import contextlib
import csv
import hashlib
import json
import pathlib
import re
import secrets
import struct
import time

from capture import utc_now
from link_capture import SecretGuard, StreamingRedactor

IQ_HEADER = struct.Struct("<4sIQHBBHBB")
END_FIELDS = ("status", "detail", "units", "pairs", "elapsed_us", "late_max", "work_max_cycles",
              "frames", "drops", "abandoned", "abandoned_input_pairs", "stopped_by_host")
HOST_STREAM_POLICY = {"rx": "read on arrival; drain available bytes up to65536",
    "tx": "poll currently queued bytes only; skip empty UART without blocking timeout",
    "ordinary_rx_queries_inside_iqs": False, "policy_version": 2}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class IQSDecoder:
    """Incremental bounded resynchronization; raw bytes remain the authority."""
    def __init__(self, sink=None):
        self.buffer = bytearray()
        self.frames, self.text = [], []
        self.start, self.end = None, None
        self.crc_errors = self.invalid_headers = self.discarded_bytes = 0
        self.missing_sequences = self.duplicate_sequences = self.reordered_sequences = 0
        self.sample_gap_count = self.missing_output_samples = self.overlapping_output_samples = 0
        self.gap_flag_frames = self.drop_flag_frames = 0
        self.last_sequence, self.next_sample = None, None
        self.valid_payload_bytes = self.valid_samples = 0
        self.sink = sink
        self.consumed = 0

    def discard(self, count):
        del self.buffer[:count]
        self.consumed += count

    def feed(self, data):
        self.buffer.extend(data)
        while self.buffer:
            if self.buffer.startswith(b"IQS1"):
                if len(self.buffer) < IQ_HEADER.size:
                    break
                _, sequence, sample_index, samples, bits, flags, decimation, gain, shift = IQ_HEADER.unpack_from(self.buffer)
                payload_bytes = samples * bits * 2 // 8
                if (bits not in (4, 8, 16) or not samples or payload_bytes > 1024 or
                    decimation not in (64, 128, 256, 512, 1024) or shift > 24):
                    self.invalid_headers += 1
                    self.discarded_bytes += 1
                    self.discard(1)
                    continue
                total = IQ_HEADER.size + payload_bytes + 4
                if len(self.buffer) < total:
                    break
                wire = bytes(self.buffer[:total])
                checksum = struct.unpack_from("<I", wire, total - 4)[0]
                if binascii.crc32(wire[:-4]) & 0xFFFFFFFF != checksum:
                    self.crc_errors += 1
                    self.discarded_bytes += 1
                    self.discard(1)
                    continue
                row = {"sequence": sequence, "output_sample_index": sample_index, "samples": samples,
                       "bits_per_component": bits, "flags": flags, "decimation": decimation,
                       "gain_metadata": gain, "shift": shift, "crc32": f"{checksum:08x}",
                       "wire_offset": self.consumed, "payload_bytes": payload_bytes,
                       "sample_gap_before": 0, "overlap_samples": 0}
                if self.last_sequence is not None:
                    advance = (sequence - self.last_sequence) & 0xFFFFFFFF
                    if advance == 0:
                        self.duplicate_sequences += 1
                    elif advance < 0x80000000:
                        self.missing_sequences += advance - 1
                        self.last_sequence = sequence
                    else:
                        self.reordered_sequences += 1
                else:
                    self.last_sequence = sequence
                if self.next_sample is not None:
                    if sample_index > self.next_sample:
                        row["sample_gap_before"] = sample_index - self.next_sample
                        self.missing_output_samples += row["sample_gap_before"]
                        self.sample_gap_count += 1
                    elif sample_index < self.next_sample:
                        row["overlap_samples"] = min(samples, self.next_sample - sample_index)
                        self.overlapping_output_samples += row["overlap_samples"]
                self.next_sample = max(self.next_sample or 0, sample_index + samples)
                self.gap_flag_frames += bool(flags & 1)
                self.drop_flag_frames += bool(flags & 2)
                self.valid_payload_bytes += payload_bytes
                self.valid_samples += samples
                self.frames.append(row)
                if self.sink:
                    self.sink(row, wire[IQ_HEADER.size:-4])
                self.discard(total)
                continue
            newline = self.buffer.find(b"\n")
            magic = self.buffer.find(b"IQS1")
            if magic >= 0 and (newline < 0 or magic < newline):
                if magic:
                    self.discarded_bytes += magic
                    self.discard(magic)
                continue
            if newline < 0:
                if len(self.buffer) > 4096:
                    # Retain a possible split magic/end token. This does not
                    # discard raw evidence: the caller stores all incoming RX.
                    count = len(self.buffer) - 64
                    self.discarded_bytes += count
                    self.discard(count)
                break
            line = bytes(self.buffer[:newline + 1]).strip()
            offset = self.consumed
            self.discard(newline + 1)
            if line:
                self.text.append({"wire_offset": offset, "text": line.decode("ascii", errors="replace")})
            match = re.fullmatch(rb"IQS (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)", line)
            if match:
                values = list(map(int, match.groups()))
                if self.start is not None:
                    raise ValueError("Multiple IQS start records in one finite stream")
                self.start = dict(zip(("input_sample_rate_hz", "decimation", "bits_per_component", "shift", "mode", "frequency_mhz"), values))
                self.start["output_sample_rate_hz"] = values[0] / values[1] if values[1] else None
            if line.startswith(b"IQSEND "):
                values = line.split()[1:]
                if len(values) != len(END_FIELDS) or any(not re.fullmatch(rb"-?\d+", value) for value in values):
                    raise ValueError("Malformed IQSEND end boundary")
                if self.end is not None:
                    raise ValueError("Multiple IQSEND end records in one finite stream")
                self.end = dict(zip(END_FIELDS, map(int, values)))

    def summary(self):
        return {"schema": "cyd-sdr-iqs-integrity/1", "frames": len(self.frames), "valid_samples_in_frames": self.valid_samples,
                "valid_payload_bytes": self.valid_payload_bytes, "crc_errors": self.crc_errors,
                "invalid_headers": self.invalid_headers, "resync_discarded_bytes": self.discarded_bytes,
                "missing_frame_sequences": self.missing_sequences, "duplicate_frame_sequences": self.duplicate_sequences,
                "reordered_frame_sequences": self.reordered_sequences, "sample_gap_events": self.sample_gap_count,
                "missing_output_samples_between_valid_frames": self.missing_output_samples,
                "overlapping_output_samples": self.overlapping_output_samples,
                "gap_flag_frames": self.gap_flag_frames, "drop_flag_frames": self.drop_flag_frames,
                "first_output_sample_index": self.frames[0]["output_sample_index"] if self.frames else None,
                "trailing_unparsed_bytes": len(self.buffer), "start": self.start, "end": self.end,
                "finite_boundary_observed": self.end is not None,
                "wire_components": "interleaved signed I,-Q; IQS4 packs I low nibble,Q high nibble",
                "limitations": ["Sample index gaps describe delivered IQ continuity, not isolated RF loss.",
                    "Data before the first valid sample index and after the last valid frame are not synthesized as measured samples.",
                    "Missing IQSEND leaves the receive stream uncertain; do not send ordinary receiver commands into that boundary.",
                    "Host timestamps and independent board clocks do not establish end-to-end latency."]}


class IndexedI8Writer:
    """Native IQS8 payload plus explicit zero-valued host erasure placeholders."""
    def __init__(self, stream, maximum_gap_samples=10000000):
        self.stream, self.maximum_gap_samples = stream, maximum_gap_samples
        self.next_index, self.origin, self.erasures = None, None, []

    def __call__(self, row, payload):
        if row["bits_per_component"] != 8:
            raise ValueError("Native i8 reconstruction requires IQS8; no implicit quantization")
        start, count = row["output_sample_index"], row["samples"]
        if self.next_index is None:
            self.origin = self.next_index = start
        if start > self.next_index:
            gap = start - self.next_index
            if gap > self.maximum_gap_samples:
                raise ValueError("IQ sample gap exceeds bounded host erasure allocation")
            self.erasures.append({"output_sample_index": self.next_index, "samples": gap,
                                  "file_sample_offset": self.next_index - self.origin,
                                  "representation": "host_zero_IQ_placeholder; not measured RF silence"})
            remaining = gap * 2
            while remaining:
                chunk = min(remaining, 65536)
                self.stream.write(b"\0" * chunk)
                remaining -= chunk
            self.next_index = start
        overlap = min(count, max(0, self.next_index - start))
        self.stream.write(payload[overlap * 2:])
        self.next_index = max(self.next_index, start + count)


def write_tables(output, decoder):
    (output / "iq-integrity.json").write_text(json.dumps(decoder.summary(), indent=2) + "\n", encoding="utf-8")
    with (output / "iq-frames.csv").open("w", newline="", encoding="utf-8") as stream:
        fields = list(decoder.frames[0]) if decoder.frames else ["sequence", "output_sample_index", "samples"]
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(decoder.frames)


def decode(args):
    args.output.mkdir(parents=True, exist_ok=False)
    with (args.output / "iq-indexed.i8").open("wb") if args.i8 else contextlib.nullcontext(None) as stream:
        sink = IndexedI8Writer(stream) if args.i8 else None
        decoder = IQSDecoder(sink)
        with args.raw.open("rb") as source:
            while data := source.read(8192):
                decoder.feed(data)
    write_tables(args.output, decoder)
    if sink:
        (args.output / "iq-erasures.json").write_text(json.dumps({"origin_output_sample_index": sink.origin,
            "erasures": sink.erasures}, indent=2) + "\n", encoding="utf-8")
    manifest = {"schema": "cyd-sdr-iqs-decode-manifest/1", "input_sha256": digest(args.raw),
                "tool_sha256": digest(pathlib.Path(__file__)), "finite_boundary_observed": decoder.end is not None,
                "outputs": {path.name: digest(path) for path in sorted(args.output.iterdir()) if path.is_file()}}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return 0 if decoder.end is not None and decoder.frames and decoder.end.get("status") == 0 else 2


class SDRPort:
    def __init__(self, name, identity, folder, guard):
        import serial
        folder.mkdir(parents=True, exist_ok=False)
        self.name, self.guard, self.identity = name, guard, identity
        self.raw = (folder / "serial-rx.redacted.bin").open("wb")
        self.events = (folder / "serial-events.redacted.jsonl").open("w", encoding="utf-8", buffering=1)
        self.text = (folder / "serial-text.redacted.jsonl").open("w", encoding="utf-8", buffering=1)
        self.redactor, self.pending, self.lines, self.records = StreamingRedactor(guard), bytearray(), [], []
        self.started, self.offset = time.perf_counter(), 0
        self.serial = serial.Serial(port=None, baudrate=identity["baud"], timeout=.01, write_timeout=5)
        self.serial.dtr = self.serial.rts = False
        self.serial.port = identity["port"]
        self.serial.open()
        self.log("open", port=identity["port"], baud=identity["baud"], dtr=False, rts=False)

    def log(self, event, **values):
        self.events.write(json.dumps(self.guard.clean({"event": event, "host_elapsed_s": time.perf_counter() - self.started,
            "utc": utc_now(), **values}), allow_nan=False) + "\n")

    def send(self, command, private=False):
        if "\n" in command or "\r" in command or len(command) > 200:
            raise ValueError("One bounded control line required")
        if command.startswith(("permit ", "tx ")) and not private:
            raise ValueError("RF authorization and TX token require private control writes")
        payload = (command + "\n").encode("ascii")
        self.log("tx_private" if private else "tx", **({"operation": command.split()[0], "tx_payload_omitted": True}
                 if private else {"command": command}))
        self.write(payload)

    def write(self, payload):
        if self.serial.write(payload) != len(payload):
            raise OSError("Short SDR serial write")

    def read(self, binary=False, nonblocking=False):
        available = self.serial.in_waiting
        if nonblocking and not available:
            return b""
        payload = self.serial.read(max(1, min(65536, available)))
        if not payload:
            return b""
        self.raw.write(self.redactor.feed(payload))
        self.log("rx", offset=self.offset, bytes=len(payload))
        self.offset += len(payload)
        if not binary:
            self.pending.extend(payload)
            if len(self.pending) > 65536:
                raise ValueError("Unterminated SDR control response exceeds bound")
            while b"\n" in self.pending:
                line, _, remaining = self.pending.partition(b"\n")
                self.pending = bytearray(remaining)
                value = self.guard.clean(line.decode("utf-8", errors="replace").strip())
                self.lines.append(value)
                self.text.write(json.dumps({"line": value, "host_elapsed_s": time.perf_counter() - self.started}) + "\n")
                if value.startswith("SDR "):
                    record = json.loads(value[4:])
                    if not isinstance(record, dict) or not isinstance(record.get("type"), str):
                        raise ValueError("Malformed SDR JSON response")
                    self.records.append(record)
        return payload

    def until(self, predicate, cursor, timeout=10, records=False):
        deadline = time.perf_counter() + timeout
        while time.perf_counter() < deadline:
            values = self.records if records else self.lines
            for value in values[cursor:]:
                if records and value.get("type") in ("error", "radio_error"):
                    raise RuntimeError("SDR transmitter rejected operation: " + json.dumps(value))
                if not records and value.startswith("ERR"):
                    raise RuntimeError("SDR receiver rejected operation: " + value)
                if predicate(value):
                    return value
            self.read()
        raise TimeoutError("SDR control response timeout; raw bytes retained")

    def request(self, command, predicate, timeout=10, records=False, private=False):
        cursor = len(self.records if records else self.lines)
        self.send(command, private=private)
        return self.until(predicate, cursor, timeout, records)

    def close(self):
        self.raw.write(self.redactor.flush())
        self.serial.close()
        for stream in (self.raw, self.events, self.text):
            stream.close()


def read_identities(path):
    config = json.loads(path.read_text(encoding="utf-8-sig"))
    if config.get("schema") != "cyd-sdr-identities/1":
        raise ValueError("Expected cyd-sdr-identities/1 with explicit tx/rx image identities")
    for name in ("tx", "rx"):
        entry = config.get(name, {})
        if (not re.fullmatch(r"COM[1-9][0-9]*", entry.get("port", ""), re.I) or
            not re.fullmatch(r"[0-9a-fA-F]{12}", entry.get("mac", "")) or
            not re.fullmatch(r"[0-9a-f]{64}", entry.get("firmware_sha256", "")) or
            type(entry.get("baud")) is not int or not 115200 <= entry["baud"] <= 3000000):
            raise ValueError("Confirmed ports/MACs, baud and exact image SHA256 are required")
        image = pathlib.Path(entry["firmware_file"])
        if not image.is_absolute():
            image = path.parent / image
        if not image.is_file() or digest(image) != entry["firmware_sha256"]:
            raise ValueError("SDR image digest differs from owner-identified binary")
        entry["firmware_file"] = str(image.resolve())
    expected = config["tx"].get("expected_info", {})
    if (not re.fullmatch(r"[0-9a-f]{40}", expected.get("source_sha", "")) or
        not re.fullmatch(r"[0-9a-f]{64}", expected.get("tx_source_digest", "")) or
        expected.get("profile") != "classic-sdr-tx-probe" or expected.get("mac") != config["tx"]["mac"]):
        raise ValueError("Exact TX source/profile/digest/MAC runtime identity required")
    if not all(isinstance(config["rx"].get("expected_version", {}).get(key), str) for key in ("revision", "profile")):
        raise ValueError("Exact upstream RX VERSION revision/profile are required")
    if config["tx"]["port"].upper() == config["rx"]["port"].upper():
        raise ValueError("Distinct owner-identified TX/RX ports are required")
    return config


def default_plan():
    return {"schema": "cyd-sdr-plan/1", "repetitions": 1, "receiver": {"duration_ms": 2000, "decimation": 64,
        "bits": 8, "rate_code": 6, "shift": 7, "mode": 2, "baseline_ms": 100,
        "commands": ["FREQ 2433", "FOFS 0", "GAIN MANUAL 40"]}, "workloads": [
        {"name": f"calibration/alternating/{mode}/{baud}", "mode": mode, "baud": baud,
         "backoff_quarter_db": 96, "pattern": "alternating", "bit_count": baud // 2}
        for mode in ("api", "ram") for baud in (1000, 5000, 10000, 20000)]}


def validate_plan(plan):
    if plan.get("schema") != "cyd-sdr-plan/1" or not plan.get("workloads"):
        raise ValueError("Expected nonempty cyd-sdr-plan/1")
    receiver = plan.get("receiver", {})
    if (receiver.get("bits") != 8 or receiver.get("decimation") not in (64, 128, 256, 512, 1024)
        or receiver.get("rate_code") not in (0, 1, 6) or receiver.get("mode") not in (0, 2)
        or not 0 <= receiver.get("shift", -1) <= 24 or not 1000 <= receiver.get("duration_ms", 0) <= 30000
        or not 0 <= receiver.get("baseline_ms", 100) <= 1000 or not 1 <= plan.get("repetitions", 1) <= 10):
        raise ValueError("Invalid bounded IQS8 configuration/repetitions")
    for command in receiver.get("commands", []):
        if not re.fullmatch(r"FREQ \d{3,4}|FOFS -?\d{1,4}|GAIN (?:HARDWARE|MANUAL \d{1,3})|BANDWIDTH \d{1,2}", command):
            raise ValueError("Only explicit bounded RX tuning/gain/filter commands are allowed")
    for case in plan["workloads"]:
        if (case.get("mode") not in ("api", "ram") or not 100 <= case.get("baud", 0) <= 100000
            or not 32 <= case.get("backoff_quarter_db", 0) <= 120 or not 1 <= case.get("bit_count", 0) <= 32768 * 8
            or case["bit_count"] * 1000 > 2000 * case["baud"]
            or receiver["duration_ms"] < 500 + receiver.get("baseline_ms", 100) + case["bit_count"] * 1000 / case["baud"]):
            raise ValueError("Finite TX configuration exceeds device/capture window bounds")
        if case.get("pattern") != "alternating" and not case.get("bits_file") and not case.get("symbols_file"):
            raise ValueError("TX requires explicit alternating calibration or packed bit file")


def pack_symbols(symbols):
    if any(value not in (0, 1) for value in symbols):
        raise ValueError("NRZ symbol file must contain only byte-valued0/1 symbols")
    packed = bytearray((len(symbols) + 7) // 8)
    for index, value in enumerate(symbols):
        packed[index // 8] |= value << (7 - index % 8)
    return bytes(packed)


def capture_run(run, tx, rx, plan, plan_path, folder, guard):
    folder.mkdir()
    case, receiver = run["settings"], plan["receiver"]
    run["host_stream_policy"] = HOST_STREAM_POLICY
    if case.get("pattern") == "alternating":
        packed = b"\x55" * ((case["bit_count"] + 7) // 8)
    else:
        source = pathlib.Path(case.get("symbols_file", case.get("bits_file")))
        if not source.is_absolute():
            source = plan_path.parent / source
        source_bytes = source.read_bytes()
        if case.get("symbols_file"):
            if len(source_bytes) != case["bit_count"]:
                raise ValueError("NRZ byte-symbol file length differs from explicit bit count")
            packed = pack_symbols(source_bytes)
        else:
            packed = source_bytes
        run["source_file_sha256"] = hashlib.sha256(source_bytes).hexdigest()
    if len(packed) != (case["bit_count"] + 7) // 8:
        raise ValueError("Packed bit file length differs from explicit bit count")
    (folder / "uploaded-bits.bin").write_bytes(packed)
    run["uploaded_bits_sha256"] = hashlib.sha256(packed).hexdigest()
    crc = binascii.crc32(packed) & 0xFFFFFFFF
    tx.request(f"load {case['bit_count']} {crc:08x}", lambda record: record.get("type") == "ready", records=True)
    before = time.perf_counter()
    cursor = len(tx.records)
    tx.log("upload", bytes=len(packed), bit_count=case["bit_count"], crc32=f"{crc:08x}", tx_payload_in_separate_file=True)
    tx.write(packed)
    loaded = tx.until(lambda record: record.get("type") == "loaded", cursor, records=True)
    if loaded.get("bits") != case["bit_count"]:
        raise ValueError("Target loaded-bit acknowledgment differs")
    run["upload_host_elapsed_s"] = time.perf_counter() - before
    for command in receiver.get("commands", []):
        rx.request(command, lambda line: line == "OK")
    gain = rx.request("GAIN?", lambda line: line.startswith("GAIN "))
    run["receiver_gain_readback"] = {"raw": gain, "fields": gain.split()[1:],
        "method": "upstream mode/index/min/max/register flag; index is not calibrated dB"}
    requested_gain = next((command for command in receiver.get("commands", []) if command.startswith("GAIN ")), None)
    if requested_gain and " ".join(gain.split()[:len(requested_gain.split())]) != requested_gain:
        raise RuntimeError("Receiver queried gain mode/index differs from requested fixed setting")
    token = secrets.token_hex(4)
    guard.add(token)
    guard.add(token.upper())
    tx.request(f"permit {token}", lambda record: record.get("type") == "permit" and record.get("ok") is True,
               records=True, private=True)
    rx.send(f"IQS {receiver['duration_ms']} {receiver['decimation']} {receiver['bits']} {receiver['rate_code']} {receiver['shift']} {receiver['mode']}")
    started_tx, tx_cursor = False, len(tx.records)
    baseline_until = None
    deadline = time.perf_counter() + receiver["duration_ms"] / 1000 + 10
    results, errors, traces = [], [], []
    with (folder / "iq-stream.bin").open("wb") as raw, (folder / "iq-indexed.i8").open("wb") as iq:
        sink, decoder = IndexedI8Writer(iq), None
        decoder = IQSDecoder(sink)
        run["rx_raw_start_offset"] = rx.offset
        if rx.pending:
            pending = bytes(rx.pending)
            raw.write(pending)
            decoder.feed(pending)
            rx.pending.clear()
        while time.perf_counter() < deadline:
            data = rx.read(binary=True)
            if data:
                raw.write(data)
                decoder.feed(data)
            # Empty TX UART reads otherwise wait10ms (often15.6ms on Windows),
            # throttling the independent high-rate IQ receiver to one USB queue
            # drain per timeout. Only RX may wait for the next arriving byte.
            tx.read(nonblocking=True)
            if decoder.start and decoder.frames and baseline_until is None:
                baseline_until = time.perf_counter() + receiver.get("baseline_ms", 100) / 1000
            if baseline_until is not None and not started_tx and time.perf_counter() >= baseline_until:
                tx.send(f"tx {case['mode']} {case['baud']} {case['backoff_quarter_db']} {token}", private=True)
                run["tx_command_host_elapsed_s"] = time.perf_counter() - tx.started
                started_tx = True
            results = [record for record in tx.records[tx_cursor:] if record.get("type") == "tx_result"]
            errors = [record for record in tx.records[tx_cursor:] if record.get("type") in ("error", "radio_error")]
            if errors:
                run["tx_errors"] = errors
            traces = [record for record in tx.records[tx_cursor:] if record.get("type") == "tx_edge"]
            if decoder.end and (errors or (results and len(traces) >= results[-1].get("trace_count", 0))):
                break
        run["rx_raw_end_offset"] = rx.offset
    write_tables(folder, decoder)
    (folder / "iq-erasures.json").write_text(json.dumps({"origin_output_sample_index": sink.origin,
        "erasures": sink.erasures}, indent=2) + "\n", encoding="utf-8")
    run["iq_integrity"] = decoder.summary()
    run["tx_target"] = tx.records[tx_cursor:]
    run["tx_result"] = results[-1] if results else None
    run["rf_signal_verified"] = None
    run["demodulation_status"] = "NOT RUN"
    if decoder.end is None:
        raise RuntimeError("Missing IQSEND: RX boundary uncertain; close port without ordinary commands")
    rx.request("RELEASE", lambda line: line == "OK")
    result = run["tx_result"]
    if (not result or errors or not started_tx or not decoder.frames or decoder.end.get("status") != 0 or
        result.get("requested_bits") != case["bit_count"] or result.get("processed_bits") != case["bit_count"] or
        result.get("requested_baud") != case["baud"] or result.get("mode") != case["mode"] or
        result.get("aborted_late") is not False or result.get("aborted_deadline") is not False
        or result.get("wifi_stop_status", 0) != 0 or result.get("wifi_deinit_status", 0) != 0
        or result.get("backoff_quarter_db") != case["backoff_quarter_db"]):
        raise RuntimeError("Finite TX/IQ acquisition incomplete; RF delivery is not established")
    if any(decoder.start.get(key) != receiver[key] for key in ("decimation", "shift", "mode")) or decoder.start.get("bits_per_component") != 8:
        raise RuntimeError("IQ start metadata differs from requested acquisition")
    run["status"] = "ACQUIRED"


def capture(args):
    if not args.rf_authorized:
        raise ValueError("RF transmission requires the explicit operator --rf-authorized flag after owner approval")
    identities = read_identities(args.identities)
    plan = json.loads(args.plan.read_text(encoding="utf-8-sig"))
    validate_plan(plan)
    args.output.mkdir(parents=True, exist_ok=False)
    guard, ports = SecretGuard(), {}
    result = {"schema": "cyd-sdr-capture/1", "evidence_class": "physical_iq_acquisition", "status": "IN PROGRESS",
        "started_utc": utc_now(), "operator_rf_authorization_flag": True, "identities": identities, "plan": plan,
        "host_stream_policy": HOST_STREAM_POLICY,
        "harness_sha256": digest(pathlib.Path(__file__)), "plan_sha256": digest(args.plan), "runs": [],
        "limitations": ["TX switch timing is SDK call completion, not a measured RF edge.",
            "Acquired IQ and completed target commands do not alone prove successful RF modulation or audio reconstruction.",
            "No on-board real-time receiver/DAC or synchronized end-to-end latency measurement.",
            "Host-indexed IQ gaps use explicit zero-valued erasure placeholders; those zeros are not measured RF silence.",
            "RX MAC/source binary identity depends on owner preflash/flash receipts; upstream runtime VERSION has no MAC/image-digest attestation."]}
    try:
        for name in ("tx", "rx"):
            ports[name] = SDRPort(name, identities[name], args.output / name, guard)
        tx, rx = ports["tx"], ports["rx"]
        info = tx.request("info", lambda record: record.get("type") == "info", records=True)
        if any(info.get(key) != value for key, value in identities["tx"]["expected_info"].items()):
            raise RuntimeError("SDR TX runtime source/profile/digest/MAC identity mismatch")
        # Upstream SYNC parses decimal uint64 and echoes its canonical decimal
        # form. A hexadecimal nonce or leading zeroes would break correlation.
        nonce = str(secrets.randbits(63) or 1)
        rx.request("SYNC " + nonce, lambda line: line == "SYNC " + nonce)
        version_line = rx.request("VERSION?", lambda line: line.startswith("VERSION "))
        version = json.loads(version_line[len("VERSION "):])
        if any(version.get(key) != value for key, value in identities["rx"]["expected_version"].items()):
            raise RuntimeError("SDR RX runtime VERSION differs from owner-identified image")
        transport = rx.request("TRANSPORT?", lambda line: line.startswith("TRANSPORT "))
        if not transport.startswith("TRANSPORT USB "):
            raise RuntimeError("Continuous IQS requires the identified native USB transport")
        result["target_info"] = {"tx": info, "rx_version": version, "rx_transport": transport,
                                 "rx_caps": rx.request("CAPS", lambda line: line.startswith("CAPS "))}
        for case in plan["workloads"]:
            for repetition in range(1, plan.get("repetitions", 1) + 1):
                run_id = f"s{len(result['runs']) + 1:03}_{repetition}"
                run = {"run_id": run_id, "workload": case["name"], "repetition": repetition,
                       "settings": case, "status": "IN PROGRESS"}
                result["runs"].append(run)
                try:
                    capture_run(run, tx, rx, plan, args.plan, args.output / run_id, guard)
                except BaseException as error:
                    run.update(status="FAILED", error=guard.clean(str(error)))
                    raise
                finally:
                    (args.output / "results.json").write_text(json.dumps(guard.clean(result), indent=2) + "\n", encoding="utf-8")
        result["status"] = "CAPTURE COMPLETE"
    except BaseException as error:
        result["status"] = "INTERRUPTED" if isinstance(error, KeyboardInterrupt) else "FAILED"
        result["error"] = guard.clean(str(error))
        raise
    finally:
        # Finite TX has no further permission after its one-use token. Never
        # inject ordinary commands into an IQ stream with an unknown end boundary.
        for port in ports.values():
            port.close()
        result["ended_utc"] = utc_now()
        (args.output / "results.json").write_text(json.dumps(guard.clean(result), indent=2) + "\n", encoding="utf-8")
        hashes = {path.relative_to(args.output).as_posix(): digest(path) for path in sorted(args.output.rglob("*")) if path.is_file()}
        (args.output / "artifact-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n", encoding="utf-8")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    child = commands.add_parser("decode")
    child.add_argument("--raw", type=pathlib.Path, required=True)
    child.add_argument("--output", type=pathlib.Path, required=True)
    child.add_argument("--i8", action="store_true", help="Create indexed native IQ8 with separately recorded host erasure placeholders")
    child = commands.add_parser("plan")
    child.add_argument("--output", type=pathlib.Path, required=True)
    child = commands.add_parser("capture")
    child.add_argument("--identities", type=pathlib.Path, required=True)
    child.add_argument("--plan", type=pathlib.Path, required=True)
    child.add_argument("--output", type=pathlib.Path, required=True)
    child.add_argument("--rf-authorized", action="store_true")
    args = parser.parse_args()
    if args.command == "plan":
        plan = default_plan()
        validate_plan(plan)
        args.output.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
        return 0
    if args.command == "decode":
        return decode(args)
    return capture(args)


if __name__ == "__main__":
    raise SystemExit(main())
