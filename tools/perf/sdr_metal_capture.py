#!/usr/bin/env python3
"""Owned-board finite SDR-metal experiments with deferred native snapshot RX."""
from __future__ import annotations

import argparse
import binascii
import csv
import hashlib
import json
import math
import pathlib
import re
import secrets
import struct
import time

from capture import utc_now
from link_capture import SecretGuard
from sdr_capture import SDRPort, digest

RAW_FORMAT = "LE32_I10_Q10_metadata12_native"
STATUS_NOT_CALLED = -2147483648


def validate_sensor_cleanup(record):
    keys = ("install_status", "enable_status", "read_status", "disable_status", "uninstall_status")
    if any(type(record.get(key)) is not int for key in keys):
        raise RuntimeError("Temperature query lacks sensor lifecycle proof; stop/recover before continuing")
    if (record["disable_status"] not in (0, STATUS_NOT_CALLED) or record["uninstall_status"] not in (0, STATUS_NOT_CALLED)
        or (record["install_status"] == 0 and record["uninstall_status"] != 0)
        or (record["enable_status"] == 0 and record["disable_status"] != 0)):
        raise RuntimeError("Temperature sensor disable/uninstall failed; stop/recover before continuing")


class SnapshotDecoder:
    """METAL JSON, exact raw length and LF; no delimiter search in payloads."""
    def __init__(self, sink=None):
        self.pending, self.records, self.snapshots, self.text = bytearray(), [], [], []
        self.binary_header, self.sink = None, sink
        self.offset = 0

    def feed(self, data):
        self.pending.extend(data)
        while self.pending:
            if self.binary_header is not None:
                header = self.binary_header
                count = header["raw_bytes"]
                if len(self.pending) < count + 1:
                    return
                payload, following = bytes(self.pending[:count]), self.pending[count]
                actual = f"{binascii.crc32(payload) & 0xffffffff:08x}"
                receipt = {"header": header, "actual_crc32": actual, "crc_ok": actual == header.get("crc32"),
                           "following_lf_ok": following == 10, "payload_offset": self.offset}
                self.snapshots.append(receipt)
                if self.sink:
                    self.sink(receipt, payload)
                del self.pending[:count + 1]
                self.offset += count + 1
                self.binary_header = None
                if not receipt["following_lf_ok"]:
                    raise ValueError("Snapshot binary boundary has no following LF; raw retained")
                if not receipt["crc_ok"]:
                    raise ValueError("Snapshot CRC32 mismatch; raw retained")
                continue
            newline = self.pending.find(b"\n")
            if newline < 0:
                if len(self.pending) > 16384:
                    raise ValueError("Unterminated METAL JSON exceeds bounded header")
                return
            line = bytes(self.pending[:newline]).strip()
            del self.pending[:newline + 1]
            self.offset += newline + 1
            if not line.startswith(b"METAL "):
                self.text.append(line.decode("utf-8", errors="replace"))
                continue
            def reject_constant(value):
                raise ValueError("Nonfinite METAL JSON constant: " + value)
            record = json.loads(line[6:].decode(), parse_constant=reject_constant)
            if not isinstance(record, dict) or record.get("schema") != 1 or not isinstance(record.get("type"), str):
                raise ValueError("Invalid METAL schema/type")
            self.records.append(record)
            if record["type"] == "snapshot":
                n, size = record.get("sample_count"), record.get("raw_bytes")
                if (type(n) is not int or not 1 <= n <= 16380 or type(size) is not int or size != n * 4
                    or record.get("raw_format") != RAW_FORMAT or not re.fullmatch(r"[0-9a-f]{8}", record.get("crc32", ""))):
                    raise ValueError("Invalid native snapshot length/format/CRC metadata")
                self.binary_header = record


class MetalRX(SDRPort):
    def __init__(self, name, identity, folder, guard):
        super().__init__(name, identity, folder, guard)
        self.decoder = SnapshotDecoder()
        self.records = self.decoder.records
        self.target = (folder / "target.jsonl").open("w", encoding="utf-8", buffering=1)

    def read(self, binary=False, nonblocking=False):
        data = super().read(binary=True, nonblocking=nonblocking)
        cursor = len(self.records)
        try:
            if data:
                self.decoder.feed(data)
        finally:
            for record in self.records[cursor:]:
                self.target.write(json.dumps(record) + "\n")
        return data

    def close(self):
        self.target.close()
        super().close()


def read_identities(path):
    config = json.loads(path.read_text(encoding="utf-8-sig"))
    if config.get("schema") != "cyd-sdr-metal-identities/1":
        raise ValueError("Expected cyd-sdr-metal-identities/1 with explicit tx/rx source/image identities")
    for name, profiles, digest_key in (("tx", ("classic-sdr-metal-tx", "s3-sdr-metal-tx"), "tx_source_digest"), ("rx", ("s3-metal-snapshot",), "source_digest")):
        entry, expected = config.get(name, {}), config.get(name, {}).get("expected_info", {})
        if (expected.get("profile") not in profiles or not re.fullmatch(r"[0-9a-f]{40}", expected.get("source_sha", ""))
            or not re.fullmatch(r"[0-9a-f]{64}", expected.get(digest_key, ""))
            or not re.fullmatch(r"[0-9a-fA-F]{12}", expected.get("mac", ""))
            or not re.fullmatch(r"[0-9a-f]{64}", entry.get("firmware_sha256", ""))
            or not re.fullmatch(r"COM[1-9][0-9]*", entry.get("port", ""), re.I)
            or type(entry.get("baud")) is not int or not 115200 <= entry["baud"] <= 3000000):
            raise ValueError("Exact metal profile/source/digest/MAC, image hash and confirmed Windows port required")
        image = pathlib.Path(entry["firmware_file"])
        if not image.is_absolute(): image = path.parent / image
        if not image.is_file() or digest(image) != entry["firmware_sha256"]:
            raise ValueError("Metal firmware file differs from owner-identified image")
        entry["firmware_file"] = str(image.resolve())
    if config["tx"]["port"].upper() == config["rx"]["port"].upper():
        raise ValueError("Distinct owner-identified TX/RX ports required")
    return config


def default_plan(with_tx=False):
    cases = [{"name": f"rx-{mode}/{rate}", "rx_mode": mode, "rate_hz": rate, "samples": 16380,
              "rx_repeats": 20 if mode == "bench" else 1, "arm_delay_us": 100000,
              "gap_us": 0, "timeout_us": 20000, "tx_enabled": False}
             for mode in ("bench", "capture") for rate in (16000000, 40000000, 80000000)]
    if with_tx:
        for mode, baud, mask in (("api", 1000, 0), ("ram", 100000, 0), ("gate", 1000000, 0),
                                 ("gate", 0, 0), ("gate", 0, 1000)):
            cases.append({"name": f"tx-{mode}/{baud}/mask{mask}", "rx_mode": "capture", "rate_hz": 16000000,
                "samples": 16380, "rx_repeats": 1, "arm_delay_us": 100000, "gap_us": 0, "timeout_us": 20000,
                "tx_enabled": True, "tx_mode": mode, "baud": baud, "backoff0_quarter_db": 96,
                "backoff1_quarter_db": 112, "duration_us": 200000, "mask_segment_us": mask,
                "bit_count": 64, "pattern": "alternating"})
    return {"schema": "cyd-sdr-metal-plan/1", "repetitions": 1,
        "receiver": {"init_path": "phy", "frequency_mhz": 2433, "offset_khz": -80, "manual_gain_index": 40},
        "transmitter": {"init_path": "phy", "symbol_capacity": 64, "trace_capacity": 512, "trace_stride": 0},
        "temperature_queries": True,
        "rf_overlap_scope": "RX arm/TX setup use independent clocks; requested overlap is not synchronized proof; inspect native IQ",
        "workloads": cases}


def validate_plan(plan):
    if plan.get("schema") != "cyd-sdr-metal-plan/1" or not plan.get("workloads") or not 1 <= plan.get("repetitions", 1) <= 20:
        raise ValueError("Expected finite cyd-sdr-metal-plan/1")
    rx, tx = plan.get("receiver", {}), plan.get("transmitter", {})
    if (rx.get("init_path", "phy") not in ("phy", "wifi") or not 1 <= rx.get("frequency_mhz", 0) <= 65535 or not -1000 <= rx.get("offset_khz", 1001) <= 1000
        or not 0 <= rx.get("manual_gain_index", -1) <= 127 or tx.get("init_path") not in ("phy", "wifi")
        or not 1 <= tx.get("symbol_capacity", 0) <= 1000000 or not 0 <= tx.get("trace_capacity", -1) <= 100000
        or not 0 <= tx.get("trace_stride", -1) <= 0xffffffff):
        raise ValueError("Explicit tuning/preallocation/observer settings required")
    if "temperature_queries" in plan and type(plan["temperature_queries"]) is not bool:
        raise ValueError("temperature_queries is an explicit boolean")
    for case in plan["workloads"]:
        if (not case.get("name") or case.get("rx_mode") not in ("capture", "bench")
            or case.get("rate_hz") not in (16000000, 40000000, 80000000) or not 1 <= case.get("samples", 0) <= 16380
            or not 1 <= case.get("rx_repeats", 0) <= 10000 or not 0 <= case.get("arm_delay_us", -1) <= 5000000
            or not 0 <= case.get("gap_us", -1) <= 1000000 or not 1 <= case.get("timeout_us", 0) <= 1000000
            or type(case.get("tx_enabled")) is not bool):
            raise ValueError("Invalid finite RX snapshot/bench settings")
        if case["tx_enabled"]:
            if (case.get("tx_mode") not in ("api", "ram", "gate", "ask", "fast") or not 0 <= case.get("baud", -1) <= 240000000
                or not 32 <= case.get("backoff0_quarter_db", 0) <= 120 or not 32 <= case.get("backoff1_quarter_db", 0) <= 120
                or not 1 <= case.get("duration_us", 0) <= 2000000 or not 0 <= case.get("mask_segment_us", -1) <= 20000
                or not 1 <= case.get("bit_count", 0) <= tx["symbol_capacity"]
                or (case["mask_segment_us"] and case["tx_mode"] not in ("gate", "ask"))):
                raise ValueError("Invalid TX finite mode/rate/backoff/IRQ mask/preloaded pattern")
            if case["tx_mode"] == "fast" and (not case["baud"] or case["mask_segment_us"] or tx["trace_stride"]):
                raise ValueError("Fast requires positive baud, IRQ enabled/mask0 and observe0")
            if case["mask_segment_us"] and math.ceil(case["duration_us"] / case["mask_segment_us"]) + 1 > tx["trace_capacity"]:
                raise ValueError("Mask segment records exceed explicit preallocated capacity")
            if case.get("pattern") != "alternating" and not case.get("bits_file"):
                raise ValueError("TX requires explicit finite preloaded pattern")


def validate_snapshot(receipt, run_id, case, bench=False):
    header = receipt["header"]
    if (not receipt["crc_ok"] or not receipt["following_lf_ok"] or header.get("source_id") != run_id
        or header.get("sample_count") != case["samples"] or header.get("nominal_rate_hz") != case["rate_hz"]
        or any(header.get(key) is not True for key in ("complete", "hardware_done", "registers_restored", "tail_canary_ok"))
        or header.get("unchanged_sentinel_words") != 0):
        raise RuntimeError("Native snapshot CRC/canary/restore/source/count proof failed")
    expected_rep = case["rx_repeats"] if bench else header.get("repetition")
    if not 1 <= expected_rep <= case["rx_repeats"] or header.get("source_repetition") != expected_rep:
        raise RuntimeError("Snapshot source repetition correlation failed")
    if header.get("owner_before") != header.get("owner_after") or header.get("lanes_before") != header.get("lanes_after"):
        raise RuntimeError("Snapshot owner/lane readback mismatch")


def validate_tx_cleanup(result):
    if (result.get("teardown_status") != 0 or result.get("wifi_driver_owned_after") is not False
        or result.get("default_event_loop_owned_after") is not False
        or result.get("direct_power_domain_owned_after") is not False):
        raise RuntimeError("Metal TX teardown failed or driver/event-loop/direct-power-domain ownership remains or lacks proof")
    if result.get("radio_stop_api") == "WiFi_stop_deinit":
        # Pinned SDK ESP_ERR_WIFI_NOT_STARTED is0x3002. The firmware explicitly
        # accepts that already-stopped condition before successful deinit.
        if result.get("wifi_stop_status") not in (0, 0x3002) or result.get("wifi_deinit_status") != 0:
            raise RuntimeError("Metal Wi-Fi stop/deinit reported failure")


TX_OUTPUT_TRANSPORT = "explicit_USB_Serial_JTAG_write_all"
TX_OUTPUT_U32 = ("output_lines_attempted", "output_lines_accepted", "output_lines_failed", "output_format_failures",
                 "output_write_calls", "output_short_writes", "output_zero_writes", "output_write_errors", "output_flush_failures")
TX_OUTPUT_U64 = ("output_bytes_requested", "output_bytes_accepted")
TX_OUTPUT_FAILURES = ("output_lines_failed", "output_format_failures", "output_write_errors", "output_flush_failures")


def tx_output_transport_info(records):
    """Latest runtime info; a declared capability cannot disappear silently."""
    infos = [r for r in records if r.get("type") == "info"]
    if not infos:
        return None
    if any(r.get("output_transport") is not None for r in infos) and infos[-1].get("output_transport") is None:
        raise RuntimeError("Metal TX output transport capability disappeared from runtime info")
    return infos[-1]


def require_fast_capability(info):
    if (not info or info.get("profile") != "s3-sdr-metal-tx" or info.get("cpu_mhz") != 240
        or info.get("fast_paced_supported") is not True or info.get("output_transport") != TX_OUTPUT_TRANSPORT
        or info.get("output_line_capacity_bytes") != 4096 or info.get("output_line_timeout_us") != 5000000
        or not re.fullmatch(r"[0-9a-f]{64}", info.get("fast_schedule_helpers_digest", ""))):
        raise RuntimeError("Fast requires the explicitly advertised S3 rational-clock backend and write-all transport")


class SourceIntegrityError(RuntimeError):
    def __init__(self, validation):
        self.validation = validation
        super().__init__("TX source integrity failed: " + validation["failure_scope"])


def require_source_integrity_capability(info):
    if (not info or info.get("profile") != "s3-sdr-metal-tx" or info.get("source_integrity_supported") is not True
        or info.get("gate_mask") != 1 << 18 or info.get("amplitude_mask") != 255 << 10
        or not re.fullmatch(r"[0-9a-f]{64}", info.get("word_helpers_digest", ""))):
        raise RuntimeError("Source integrity requires the reviewed S3 word-helper identity and gate/amplitude masks")


def expected_source_crcs(uploaded, result, info):
    """Recompute intended packed/LE32 words and phase-zero fast deltas on host."""
    require_source_integrity_capability(info)
    count = result.get("pattern_bits")
    if (type(count) is not int or not 1 <= count <= 1000000 or not isinstance(uploaded, bytes)
        or len(uploaded) != (count + 7) // 8):
        raise RuntimeError("Source-integrity proof requires exact uploaded bytes and active bit count")
    initialized, observed = result.get("initialized_word"), result.get("observed_backoff1_word")
    if (any(type(v) is not int or not 0 <= v <= 0xffffffff for v in (initialized, observed))
        or not initialized & info["gate_mask"] or result.get("restored_word_requested") != initialized):
        raise RuntimeError("Source word mapping lacks initialized/restored unsigned-word proof")
    mode = result.get("mode")
    if mode not in ("api", "ram", "gate", "ask", "fast"): raise RuntimeError("Unknown source word mapping mode")
    gate, amplitude = info["gate_mask"], info["amplitude_mask"]
    low = initialized if mode == "ask" else initialized & ~gate
    high = (initialized & ~amplitude) | (observed & amplitude) if mode == "ask" else initialized | gate
    words_crc = 0
    for index in range(count):
        value = high if (uploaded[index // 8] >> (7 - index % 8)) & 1 else low
        words_crc = binascii.crc32(struct.pack("<I", value), words_crc)
    deltas_crc = None
    if mode == "fast":
        rate = result.get("requested_baud")
        if type(rate) is not int or not 1 <= rate <= 240000000: raise RuntimeError("Fast delta source lacks a positive rate")
        deltas_crc = 0
        for index in range(count):
            delta = (index + 1) * 240000000 // rate - index * 240000000 // rate
            deltas_crc = binascii.crc32(struct.pack("<I", delta), deltas_crc)
    return {"packed_crc32": binascii.crc32(uploaded) & 0xffffffff, "words_crc32": words_crc & 0xffffffff,
            "deltas_crc32": deltas_crc & 0xffffffff if deltas_crc is not None else None,
            "packed_bytes": len(uploaded), "words_bytes": count * 4,
            "packed_sha256": hashlib.sha256(uploaded).hexdigest()}


def validate_tx_source_integrity(result, record, before_info, after_info, uploaded):
    require_source_integrity_capability(before_info)
    require_source_integrity_capability(after_info)
    expected = expected_source_crcs(uploaded, result, before_info)
    fast = result["mode"] == "fast"
    crc_fields = ("packed_expected", "packed_before_radio", "packed_after_radio", "packed_after_tone",
        "packed_before_loop", "packed_after_loop", "packed_after_teardown", "words_expected", "words_before_loop",
        "words_after_loop", "words_after_teardown", "deltas_after_build", "deltas_before_loop", "deltas_after_loop", "deltas_after_teardown")
    if any(type(record.get(k + "_crc32")) is not int or not 0 <= record[k + "_crc32"] <= 0xffffffff for k in crc_fields):
        raise RuntimeError("Source-integrity receipt has missing/invalid unsigned CRC fields")
    flags = ("after_radio_checked", "after_tone_checked", "preloop_checked", "loop_started", "teardown_checked")
    if (any(record.get(k) is not True for k in flags) or record.get("delta_required") is not fast
        or record.get("schedule_populated_after_startup_calibration") is not fast
        or record.get("buffer_word_byte_order") != "native_little_endian_u32"
        or record.get("packed_bit_order") != "MSB_first_per_byte"
        or record.get("schedule_population_scope") != "after_all_SDK_startup_and_tone_setup_before_timed_loop"):
        raise RuntimeError("Source-integrity stages/loop/population/byte-order proof incomplete")
    if (record.get("packed_bytes") != expected["packed_bytes"] or record.get("words_bytes") != expected["words_bytes"]
        or record.get("deltas_bytes") != (expected["words_bytes"] if fast else 0)
        or type(after_info.get("loaded_crc32")) is not int or after_info["loaded_crc32"] != expected["packed_crc32"]
        or after_info.get("loaded_bits") != result["pattern_bits"]):
        raise RuntimeError("Source lengths/post-terminal upload identity differ from host bytes")
    intervals = []
    for buffer in ("packed", "words", "deltas"):
        address, size = record.get(buffer + "_address"), record.get(buffer + "_bytes")
        if (type(address) is not int or not 0 <= address <= 0xffffffff or type(size) is not int or size < 0
            or (buffer == "deltas" and not fast and address != 0)
            or (size and (not address or address + size > 0x100000000 or (buffer != "packed" and address % 4)))):
            raise RuntimeError("Source buffer address/count/alignment proof invalid")
        if size: intervals.append((address, address + size))
    if any(a < d and c < b for i, (a, b) in enumerate(intervals) for c, d in intervals[i + 1:]):
        raise RuntimeError("Reported source buffers overlap")
    stage_errors = []
    for stage in ("expected", "before_radio", "after_radio", "after_tone", "before_loop", "after_loop", "after_teardown"):
        if record["packed_" + stage + "_crc32"] != expected["packed_crc32"]: stage_errors.append("packed_" + stage)
    for stage in ("expected", "before_loop", "after_loop", "after_teardown"):
        if record["words_" + stage + "_crc32"] != expected["words_crc32"]: stage_errors.append("words_" + stage)
    for stage in ("after_build", "before_loop", "after_loop", "after_teardown"):
        value = record["deltas_" + stage + "_crc32"]
        if value != (expected["deltas_crc32"] if fast else 0): stage_errors.append("deltas_" + stage)
    target_timed = (all(record["packed_" + stage + "_crc32"] == record["packed_expected_crc32"]
                        for stage in ("before_radio", "after_radio", "after_tone", "before_loop", "after_loop"))
        and all(record["words_" + stage + "_crc32"] == record["words_expected_crc32"] for stage in ("before_loop", "after_loop"))
        and (not fast or all(record["deltas_" + stage + "_crc32"] == record["deltas_after_build_crc32"] for stage in ("before_loop", "after_loop"))))
    target_retained = (target_timed and record["packed_after_teardown_crc32"] == record["packed_expected_crc32"]
        and record["words_after_teardown_crc32"] == record["words_expected_crc32"]
        and (not fast or record["deltas_after_teardown_crc32"] == record["deltas_after_build_crc32"]))
    timed = not any(not value.endswith("after_teardown") for value in stage_errors)
    retained = not stage_errors
    validation = {"status": "VALIDATED" if retained else "FAILED", "host_expected": expected,
        "timed_checkpoint_CRCs_match": timed, "retained_checkpoint_CRCs_match": retained,
        "target_reported_timed_integrity": target_timed, "target_reported_retained_integrity": target_retained,
        "mismatching_stages": stage_errors, "failure_scope": "none" if retained else
            "TEARDOWN_ONLY_CHANGE; timed checkpoints match; no RF BER attribution" if timed else
            "SOURCE_CHECKPOINT_OR_MAPPING_MISMATCH; stages retained; RF cause not isolated",
        "deltas": "host_recomputed_phase_zero_LE32" if fast else "NOT APPLICABLE",
        "scope": "CRC32_at_marked_buffer_stages; transient changes/collisions and physical RF payload not independently attested"}
    if any(record.get(k) is not v for k, v in (("ok", target_retained), ("timed_source_integrity_ok", target_timed), ("retained_source_integrity_ok", target_retained))):
        raise RuntimeError("Source-integrity flags contradict host-computed CRC stages")
    if not retained: raise SourceIntegrityError(validation)
    return validation


def validate_tx_fast_clock(result, clock, before_info, after_info):
    """Integer schedule/carry proof, not measured RF symbols or transient jitter."""
    require_fast_capability(before_info)
    require_fast_capability(after_info)
    if after_info["fast_schedule_helpers_digest"] != before_info["fast_schedule_helpers_digest"]:
        raise RuntimeError("Fast schedule helper identity changed during the exposure")
    unsigned = ("requested_baud", "target_duration_cycles", "source_words_completed", "ideal_words_by_target_end",
        "target_tail_words_not_transmitted", "target_words_ahead_due_rounding", "schedule_next_start_cycles",
        "ideal_next_start_cycles_floor", "end_ideal_backlog_cycles", "ring_length_symbols", "ring_cycles_floor",
        "ring_remainder", "ring_phase_at_end", "precomputed_delta_min_cycles", "precomputed_delta_max_cycles",
        "schedule_bytes_reserved", "ring_boundary_extra_cycles_max", "floor_reference_early_cycles_max",
        "real_reference_early_cycles_strict_upper_bound")
    if any(type(clock.get(k)) is not int or not 0 <= clock[k] <= 0xffffffffffffffff for k in unsigned):
        raise RuntimeError("Fast clock receipt lacks bounded unsigned counts")
    if type(clock.get("end_clock_error_cycles")) is not int:
        raise RuntimeError("Fast clock error must be a signed integer")
    rate, words, duration, length, elapsed = (result.get(k) for k in
        ("requested_baud", "processed_bits", "requested_duration_us", "pattern_bits", "elapsed_cycles"))
    zero_fields = ("requested_mask_segment_us", "trace_stride", "trace_count", "segment_records", "skipped_paced_slots")
    if (any(type(v) is not int or v <= 0 for v in (rate, words, duration, length, elapsed))
        or rate > 240000000 or duration > 2000000 or max(words, length, elapsed) > 0xffffffff or result.get("mode") != "fast"
        or result.get("loop_variant") != "fast_rational_DRAM_paced" or result.get("unpaced") is not False
        or result.get("backend_available") is not True
        or any(type(result.get(k)) is not int or result[k] != 0 for k in zero_fields)
        or "late_writes" not in result or result["late_writes"] is not None
        or "late_cycles_max" not in result or result["late_cycles_max"] is not None):
        raise RuntimeError("Fast result violates positive-rate/no-skip/unobserved-lateness/IRQ-observer contract")
    cpu = 240000000
    target_cycles = duration * 240
    target_words = (duration * rate + 999999) // 1000000
    ideal_next = words * cpu // rate
    rings, partial = divmod(words, length)
    scheduled = rings * length * cpu // rate + partial * cpu // rate
    ring_remainder = length * cpu % rate
    expected = {"requested_baud": rate, "target_duration_cycles": target_cycles, "source_words_completed": words,
        "ideal_words_by_target_end": target_words, "target_tail_words_not_transmitted": max(0, target_words - words),
        "target_words_ahead_due_rounding": max(0, words - target_words), "schedule_next_start_cycles": scheduled,
        "ideal_next_start_cycles_floor": ideal_next, "end_clock_error_cycles": elapsed - ideal_next,
        "end_ideal_backlog_cycles": max(0, elapsed - ideal_next), "ring_length_symbols": length,
        "ring_cycles_floor": length * cpu // rate, "ring_remainder": ring_remainder,
        "ring_phase_at_end": rings * ring_remainder % rate,
        "precomputed_delta_min_cycles": cpu // rate,
        "precomputed_delta_max_cycles": cpu // rate + int(length * (cpu % rate) >= rate),
        "ring_boundary_extra_cycles_max": 1, "floor_reference_early_cycles_max": 1,
        "real_reference_early_cycles_strict_upper_bound": 2}
    prior_rings, prior_partial = divmod(words - 1, length)
    prior_start = prior_rings * length * cpu // rate + prior_partial * cpu // rate
    if (any(clock.get(k) != v for k, v in expected.items()) or elapsed < target_cycles
        or prior_start >= target_cycles or ideal_next - scheduled not in (0, 1)
        or clock["schedule_bytes_reserved"] < length * 4 or clock["schedule_bytes_reserved"] % 4
        or after_info.get("fast_schedule_bytes_reserved") != clock["schedule_bytes_reserved"]
        or clock.get("schedule_memory") != "internal_8bit_DRAM"
        or clock.get("per_word_lateness_observed") is not False or clock.get("max_transient_backlog_cycles") is not None):
        raise RuntimeError("Fast counts/rate/rounding/carry/tail/schedule-allocation correlation failed")
    for key, expected_value in (("achieved_writes_per_s", words * cpu / elapsed),
                                ("mean_loop_cycles_per_write", elapsed / words)):
        value = result.get(key)
        if type(value) not in (int, float) or not math.isfinite(value) or abs(value - expected_value) > .001:
            raise RuntimeError("Fast software rate does not match reported count/cycles")
    return {"status": "VALIDATED", "source_words_completed": words,
        "target_tail_words_not_transmitted": max(0, target_words - words),
        "floor_reference_early_cycles_at_end": ideal_next - scheduled,
        "scope": "rational_schedule_and_source_prefix_counter_consistency; no_RF_symbol_or_max_transient_lateness_proof"}


def validate_tx_transport(records, before_info=None, after_info=None, uploaded_packed=None):
    """New write-all transport proof; legacy captures retain their old scope.

    Driver counters are cumulative snapshots before the reporting line itself.
    A fresh post-terminal info is required to observe the final flush result.
    Retry observations are permitted; incomplete/failed accepted output is not.
    This does not turn SDK acceptance or register state into RF cessation proof.
    """
    ends = [r for r in records if r.get("type") == "tx_end"]
    fast_clocks = [r for r in records if r.get("type") == "tx_fast_clock"]
    integrity_records = [r for r in records if r.get("type") == "tx_source_integrity"]
    advertised = (before_info or {}).get("output_transport")
    new_fields = bool(fast_clocks or integrity_records) or (before_info or {}).get("source_integrity_supported") is True or any(r.get("type") == "tx_result" and r.get("mode") == "fast" for r in records) or any("tx_result_driver_accepted" in r or "output_lines_attempted" in r for r in ends)
    if advertised is None and not new_fields:
        return {"status": "NOT_APPLICABLE", "scope": "legacy_TX_without_write_all_receipts"}
    if advertised != TX_OUTPUT_TRANSPORT:
        raise RuntimeError("Metal TX new output receipts lack the supported runtime transport capability")
    if before_info.get("output_line_capacity_bytes") != 4096 or before_info.get("output_line_timeout_us") != 5000000:
        raise RuntimeError("Metal TX output capacity/timeout differs from reviewed transport")
    if after_info is None:
        raise RuntimeError("Metal TX post-terminal info required to observe final output flush")
    identity = ("profile", "source_sha", "tx_source_digest", "word_helpers_digest", "mac", "output_transport",
                "output_line_capacity_bytes", "output_line_timeout_us", "source_integrity_supported")
    if after_info.get("type") != "info" or any(after_info.get(k) != before_info.get(k) for k in identity):
        raise RuntimeError("Metal TX post-terminal runtime identity/transport changed")
    results = [r for r in records if r.get("type") == "tx_result"]
    traces = [r for r in records if r.get("type") == "tx_trace"]
    segments = [r for r in records if r.get("type") == "tx_segment"]
    if len(results) != 1 or len(ends) != 1:
        raise RuntimeError("Metal TX output result/terminal record missing or ambiguous")
    result, end = results[0], ends[0]
    if (records.index(result) >= records.index(end)
        or any(records.index(r) <= records.index(result) or records.index(r) >= records.index(end) for r in traces + segments)):
        raise RuntimeError("Metal TX output records are outside the result/terminal batch")
    source_validation = None
    if before_info.get("source_integrity_supported") is True:
        if (len(integrity_records) != 1 or end.get("source_integrity_required") is not True
            or end.get("source_integrity_driver_accepted") is not True
            or not records.index(result) < records.index(integrity_records[0]) < records.index(end)
            or any(records.index(r) < records.index(integrity_records[0]) for r in fast_clocks + traces + segments)):
            raise RuntimeError("Source-integrity record/acceptance/order is missing or duplicated")
        source_validation = validate_tx_source_integrity(result, integrity_records[0], before_info, after_info, uploaded_packed)
        if end.get("source_integrity_ok") is not True: raise RuntimeError("Source-integrity terminal flag rejects retained source")
    elif integrity_records or any(k in end for k in ("source_integrity_required", "source_integrity_driver_accepted", "source_integrity_ok")):
        raise RuntimeError("Source-integrity receipt lacks its runtime capability")
    fast_validation = None
    if result.get("mode") == "fast":
        if (len(fast_clocks) != 1 or end.get("fast_clock_required") is not True
            or end.get("fast_clock_driver_accepted") is not True
            or not records.index(result) < records.index(fast_clocks[0]) < records.index(end)):
            raise RuntimeError("Fast clock record/terminal acceptance missing, duplicated or outside its batch")
        fast_validation = validate_tx_fast_clock(result, fast_clocks[0], before_info, after_info)
    elif fast_clocks or ("fast_clock_required" in end and end["fast_clock_required"] is not False) or end.get("fast_clock_driver_accepted") is not None:
        raise RuntimeError("Non-fast result has unexpected fast-clock receipt or acceptance flags")
    expected_counts = {"trace_records": len(traces), "segment_records": len(segments),
                       "trace_records_driver_accepted": len(traces), "segment_records_driver_accepted": len(segments)}
    if any(type(end.get(k)) is not int or end[k] != value for k, value in expected_counts.items()):
        raise RuntimeError("Metal TX driver-accepted trace/segment counts differ from parsed output")
    if (type(result.get("trace_count")) is not int or result["trace_count"] != len(traces)
        or type(result.get("segment_records")) is not int or result["segment_records"] != len(segments)
        or any(end.get(k) is not True for k in ("tx_result_driver_accepted", "all_prior_records_driver_accepted",
                                               "prior_output_drained", "backend_available", "restored_word_matches",
                                               "tone_disable_requested", "continuous_tone_disable_requested"))
        or end.get("radio_ready_after_teardown") is not False or end.get("tone_enable_bit_after_stop_request") is not False):
        raise RuntimeError("Metal TX output batch acceptance/drain or stop receipt failed")
    validate_tx_cleanup(result)
    validate_tx_cleanup(end)
    for key in ("restored_word_requested", "restored_word_readback", "tone_disable_word_readback"):
        if type(end.get(key)) is not int or not 0 <= end[key] <= 0xffffffff:
            raise RuntimeError("Metal TX stop/register readback lacks unsigned word proof")
    if (end["restored_word_requested"] != end["restored_word_readback"]
        or end["restored_word_requested"] != result.get("restored_word_requested")
        or end["restored_word_readback"] != result.get("restored_word_readback")):
        raise RuntimeError("Metal TX terminal restoration differs from result")
    gate_mask = before_info.get("gate_mask")
    if (type(gate_mask) is not int or not 0 < gate_mask <= 0xffffffff
        or end["tone_disable_word_readback"] & gate_mask):
        raise RuntimeError("Metal TX tone-disable register contradicts the stop receipt")
    for label, record in (("before", before_info), ("terminal", end), ("after", after_info)):
        for key in TX_OUTPUT_U32 + TX_OUTPUT_U64:
            maximum = 0xffffffff if key in TX_OUTPUT_U32 else 0xffffffffffffffff
            if type(record.get(key)) is not int or not 0 <= record[key] <= maximum:
                raise RuntimeError("Metal TX missing/invalid output counter " + label + ":" + key)
        if (any(record[k] != 0 for k in TX_OUTPUT_FAILURES) or record.get("output_resync_pending") is not False
            or record["output_lines_attempted"] != record["output_lines_accepted"]
            or record["output_bytes_requested"] != record["output_bytes_accepted"]
            or not record["output_zero_writes"] <= record["output_short_writes"] <= record["output_write_calls"]):
            raise RuntimeError("Metal TX output failure/drop/pending or inconsistent acceptance counters: " + label)
    for first, last in ((before_info, end), (end, after_info)):
        if any(last[k] < first[k] for k in TX_OUTPUT_U32 + TX_OUTPUT_U64):
            raise RuntimeError("Metal TX output counters regressed/reset during the batch")
    if (end["output_lines_accepted"] - before_info["output_lines_accepted"] < 1 + len(traces) + len(segments) + len(fast_clocks) + len(integrity_records)
        or after_info["output_lines_accepted"] <= end["output_lines_accepted"]
        or after_info["output_bytes_accepted"] <= end["output_bytes_accepted"]):
        raise RuntimeError("Metal TX output counters lack complete batch/terminal progress")
    return {"status": "VALIDATED", "transport": TX_OUTPUT_TRANSPORT,
            "trace_records": len(traces), "segment_records": len(segments),
            "fast_clock": fast_validation,
            "source_integrity": source_validation,
            "scope": "host_parsed_batch_plus_driver_acceptance_and_post_terminal_flush_counters; not_RF_cessation_proof",
            "retry_scope": "short_and_zero_writes_allowed_when_full_output_succeeds; cumulative_failure_counters_must_be_zero"}


def execute_run(run, tx, rx, plan, plan_path, folder, guard):
    folder.mkdir()
    case, run_id = run["settings"], run["run_id"]
    rx_cursor, tx_cursor, snapshot_cursor = len(rx.records), len(tx.records), len(rx.decoder.snapshots)
    output_before = tx_output_transport_info(tx.records)
    if case.get("tx_enabled") and case.get("tx_mode") == "fast": require_fast_capability(output_before)
    if case.get("tx_enabled") and (output_before or {}).get("source_integrity_supported") is True: require_source_integrity_capability(output_before)
    def save_snapshot(receipt, payload):
        filename = f"snapshot-{len(rx.decoder.snapshots) - snapshot_cursor:04}.native32le"
        path = folder / filename
        path.write_bytes(payload)
        receipt["file"] = filename
        receipt["sha256"] = digest(path)
    rx.decoder.sink = save_snapshot
    def temperature(endpoint, rx_sensor=False):
        if not plan.get("temperature_queries", True): return {"status": "NOT MEASURED", "reason": "disabled by plan"}
        is_s3 = rx_sensor or endpoint.identity["expected_info"]["profile"] == "s3-sdr-metal-tx"
        command = "metal temperature" if rx_sensor else "temperature" if is_s3 else "info"
        record = endpoint.request(command, lambda r: r.get("type") == ("temperature" if is_s3 else "info"), records=True)
        if is_s3: validate_sensor_cleanup(record)
        value = record.get("temperature_c_driver" if is_s3 else "temperature_raw")
        observed = type(value) in (int, float) and math.isfinite(value)
        return {"status": "RECORDED" if observed else "NOT MEASURED", "scope": "outside timed RF/snapshot loop", "target": record}
    run["temperature_before"] = {"tx": temperature(tx), "rx": temperature(rx, True)}
    if case["tx_enabled"]:
        if case.get("pattern") == "alternating":
            packed = b"\x55" * ((case["bit_count"] + 7) // 8)
        else:
            path = pathlib.Path(case["bits_file"])
            if not path.is_absolute(): path = plan_path.parent / path
            packed = path.read_bytes()
        if len(packed) != (case["bit_count"] + 7) // 8:
            raise ValueError("Preloaded packed pattern length differs from requested bit count")
        (folder / "uploaded-bits.bin").write_bytes(packed)
        run["uploaded_bits_sha256"] = hashlib.sha256(packed).hexdigest()
        tx.request(f"load {case['bit_count']} {binascii.crc32(packed) & 0xffffffff:08x}", lambda r: r.get("type") == "ready", records=True)
        loaded_cursor = len(tx.records)
        tx.log("upload", bytes=len(packed), bit_count=case["bit_count"], packed_MSB_first=True)
        tx.write(packed)
        loaded = tx.until(lambda r: r.get("type") == "loaded", loaded_cursor, records=True)
        if loaded.get("bits") != case["bit_count"]: raise RuntimeError("Metal loaded-bit receipt differs")
        permit = secrets.token_hex(4)
        guard.add(permit); guard.add(permit.upper())
        tx.request("permit " + permit, lambda r: r.get("type") == "permit" and r.get("ok") is True, records=True, private=True)
    command = (f"metal {case['rx_mode']} {run_id} {case['rate_hz']} {case['samples']} {case['rx_repeats']} "
               f"{case['arm_delay_us']} {case['gap_us']} {case['timeout_us']}")
    armed = rx.request(command, lambda r: r.get("type") == "armed" and r.get("id") == run_id, records=True)
    if any(armed.get(field) != value for field, value in (("mode", case["rx_mode"]), ("sample_count", case["samples"]),
        ("nominal_rate_hz", case["rate_hz"]), ("repeats_requested", case["rx_repeats"]), ("arm_delay_us", case["arm_delay_us"]),
        ("inter_snapshot_gap_us", case["gap_us"]), ("timeout_us", case["timeout_us"]))):
        raise RuntimeError("RX armed receipt differs from explicit plan")
    run["armed"] = armed
    if case["tx_enabled"]:
        tx.send(f"tx {case['tx_mode']} {case['baud']} {case['backoff0_quarter_db']} {case['backoff1_quarter_db']} "
                f"{case['duration_us']} {case['mask_segment_us']} {permit}", private=True)
    expected_duration = (case["arm_delay_us"] + case["rx_repeats"] * (case["timeout_us"] + case["gap_us"])) / 1e6
    if case["rx_mode"] == "capture": expected_duration += case["rx_repeats"] * 5
    deadline = time.perf_counter() + expected_duration + 15
    while time.perf_counter() < deadline:
        rx.read()
        tx.read(nonblocking=True)
        recent_rx, recent_tx = rx.records[rx_cursor:], tx.records[tx_cursor:]
        for record in recent_rx + recent_tx:
            if record.get("type") in ("error", "radio_error"):
                raise RuntimeError("Metal operation failed: " + json.dumps(record))
        ended_rx = [r for r in recent_rx if r.get("type") == "run_end" and r.get("id") == run_id]
        ended_tx = [r for r in recent_tx if r.get("type") == "tx_end"]
        if ended_rx and (not case["tx_enabled"] or ended_tx): break
    else:
        raise TimeoutError("Missing finite metal terminal boundary; raw data retained; no recovery commands injected")
    run["receiver_run_end"] = ended_rx[-1]
    end = ended_rx[-1]
    if (end.get("ok") is not True or end.get("captures_completed") != case["rx_repeats"]
        or end.get("sample_count_each") != case["samples"] or end.get("nominal_rate_hz") != case["rate_hz"]
        or end.get("last_owner_restored") is not True or end.get("last_canary_ok") is not True
        or end.get("transfer_failures_delta") != 0):
        raise RuntimeError("Metal RX run completion/canary/restore/transfer proof failed")
    if case["rx_mode"] == "bench":
        read_id = run_id + "_last"
        cursor = len(rx.decoder.snapshots)
        rx.send("metal read " + read_id)
        deadline = time.perf_counter() + 15
        while time.perf_counter() < deadline:
            rx.read()
            tx.read(nonblocking=True)
            if any(record.get("type") == "error" for record in rx.records[rx_cursor:]):
                raise RuntimeError("Deferred snapshot read failed; error retained")
            if len(rx.decoder.snapshots) > cursor: break
        else: raise TimeoutError("Deferred last-snapshot read did not complete")
        last = rx.decoder.snapshots[-1]["header"]
        if last.get("id") != read_id or last.get("repetition") != 0:
            raise RuntimeError("Deferred read request/source identity mismatch")
    snapshots = rx.decoder.snapshots[snapshot_cursor:]
    if len(snapshots) != (1 if case["rx_mode"] == "bench" else case["rx_repeats"]):
        raise RuntimeError("Finalized snapshot count differs from selected capture/read mode")
    for receipt in snapshots: validate_snapshot(receipt, run_id, case, case["rx_mode"] == "bench")
    run["snapshots"] = snapshots
    run["rx_target"] = rx.records[rx_cursor:]
    run["tx_target"] = tx.records[tx_cursor:]
    if case["tx_enabled"]:
        results = [r for r in run["tx_target"] if r.get("type") == "tx_result"]
        traces = [r for r in run["tx_target"] if r.get("type") == "tx_trace"]
        segments = [r for r in run["tx_target"] if r.get("type") == "tx_segment"]
        if len(results) != 1: raise RuntimeError("Metal TX result missing or ambiguous")
        result = results[0]
        if (result.get("backend_available") is not True or result.get("mode") != case["tx_mode"]
            or result.get("requested_baud") != case["baud"] or result.get("requested_duration_us") != case["duration_us"]
            or result.get("requested_mask_segment_us") != case["mask_segment_us"]
            or result.get("backoff0_quarter_db") != case["backoff0_quarter_db"]
            or result.get("backoff1_quarter_db") != case["backoff1_quarter_db"]
            or result.get("unpaced") is not (case["baud"] == 0)
            or result.get("trace_stride") != plan["transmitter"]["trace_stride"]
            or result.get("init_path") != ("experimental_direct_PHY" if plan["transmitter"]["init_path"] == "phy" else "WiFi_NULL")
            or result.get("pattern_bits") != case["bit_count"] or not result.get("processed_bits")
            or len(traces) != result.get("trace_count") or len(segments) != result.get("segment_records")
            or result.get("restored_word_readback") != result.get("restored_word_requested")):
            raise RuntimeError("Metal TX mode/timing/trace/restore correlation failed")
        validate_tx_cleanup(result)
        if ended_tx[-1].get("trace_records") != len(traces) or ended_tx[-1].get("segment_records") != len(segments):
            raise RuntimeError("Metal TX terminal record differs from observed trace/segment counts")
        run["transmitter_result"] = result
        output_after = None
        if (output_before or {}).get("output_transport") is not None:
            # Teardown was proven above. Query outside RF and after the terminal
            # line; its final flush status is absent from tx_end's own snapshot.
            output_after = tx.request("info", lambda r: r.get("type") == "info", records=True)
            run["transmitter_output_before"] = output_before
            run["transmitter_output_after"] = output_after
            run["tx_target"] = tx.records[tx_cursor:]
        try:
            run["transmitter_output_validation"] = validate_tx_transport(run["tx_target"], output_before, output_after, packed)
        except SourceIntegrityError as error:
            run["transmitter_source_validation"] = error.validation
            raise
    run["physical_rf_verified"] = None
    run["temperature_after"] = {"tx": temperature(tx), "rx": temperature(rx, True)}
    run["status"] = "ACQUIRED"
    run["snapshot_scope"] = "Separate native SRAM snapshots; sample index resets0 per burst, deferred USB outside capture"
    rx.decoder.sink = None


def capture(args):
    if not args.rf_authorized:
        raise ValueError("Explicit --rf-authorized required after owner approval of new images/physical experiment")
    identities = read_identities(args.identities)
    plan = json.loads(args.plan.read_text(encoding="utf-8-sig")); validate_plan(plan)
    args.output.mkdir(parents=True, exist_ok=False)
    ports, guard = {}, SecretGuard()
    result = {"schema": "cyd-sdr-metal-capture/1", "evidence_class": "physical_snapshot_acquisition", "status": "IN PROGRESS",
        "started_utc": utc_now(), "identities": identities, "plan": plan, "plan_sha256": digest(args.plan),
        "harness_sha256": digest(pathlib.Path(__file__)), "serial_module_sha256": digest(pathlib.Path(__file__).with_name("sdr_capture.py")),
        "operator_rf_authorization_flag": True, "runs": [], "limitations": [
            "Configured native sample clock/horizon is not an independently calibrated rate or continuous stream.",
            "MMIO/call loop completion and achieved writes/s are software observations, not measured RF edge limits.",
            "Local CPU IRQ masking excludes only specified interrupt levels; actual mask/gap durations remain measured.",
            "Separate board clocks and serial command ordering do not establish RF overlap or one-way latency.",
            "No-export bench occupancy and capture-with-USB deadtime are separate cohorts. Inspect raw IQ for signal claims."]}
    try:
        ports["tx"] = SDRPort("tx", identities["tx"], args.output / "tx", guard)
        ports["rx"] = MetalRX("rx", identities["rx"], args.output / "rx", guard)
        for name, command in (("tx", "info"), ("rx", "metal info")):
            info = ports[name].request(command, lambda r: r.get("type") == "info", records=True)
            if any(info.get(key) != value for key, value in identities[name]["expected_info"].items()):
                raise RuntimeError("Metal runtime source/profile/digest/MAC mismatch on " + name)
            result.setdefault("target_info", {})[name] = info
        tx, rx = ports["tx"], ports["rx"]
        config = plan["receiver"]
        init_path = config.get("init_path", "phy")
        initialized = rx.request("metal init " + init_path, lambda r: r.get("type") == "init", records=True)
        if (initialized.get("ok") is not True or initialized.get("requested") != init_path
            or initialized.get("actual_mode") != ("direct_PHY" if init_path == "phy" else "WiFi_NULL")
            or initialized.get("error_code") != 0):
            raise RuntimeError("Metal RX explicit PHY/driver initialization unavailable")
        result["receiver_init"] = initialized
        tune = rx.request(f"metal tune {config['frequency_mhz']} {config['offset_khz']} {config['manual_gain_index']}", lambda r: r.get("type") == "tune", records=True)
        if tune.get("ok") is not True or any(tune.get(key) != config[key] for key in ("frequency_mhz", "offset_khz", "manual_gain_index")):
            raise RuntimeError("Metal RX requested tune/gain unavailable")
        result["receiver_tune"] = tune
        if any(case["tx_enabled"] for case in plan["workloads"]):
            config = plan["transmitter"]
            for command, kind in ((f"reserve {config['symbol_capacity']} {config['trace_capacity']}", "reserved"),
                                  (f"observe {config['trace_stride']}", "observe"), ("init " + config["init_path"], "init_path")):
                tx.request(command, lambda r, kind=kind: r.get("type") == kind and r.get("ok") is True, records=True)
            configured = tx.request("info", lambda r: r.get("type") == "info", records=True)
            if any(configured.get(key) != value for key, value in (("capacity_symbols", config["symbol_capacity"]),
                ("trace_capacity", config["trace_capacity"]), ("trace_stride", config["trace_stride"]))):
                raise RuntimeError("Metal TX actual preallocation/observer configuration differs")
            result["transmitter_setup_info"] = configured
        for case in plan["workloads"]:
            for repetition in range(1, plan.get("repetitions", 1) + 1):
                run = {"run_id": f"m{len(result['runs']) + 1:03}_{repetition}", "workload": case["name"],
                       "repetition": repetition, "settings": case, "status": "IN PROGRESS"}
                result["runs"].append(run)
                cursors = {name: len(port.records) for name, port in ports.items()}
                try:
                    execute_run(run, tx, rx, plan, args.plan, args.output / run["run_id"], guard)
                except BaseException as error:
                    run.update(status="FAILED", error=guard.clean(str(error)), partial_target={
                        name: port.records[cursors[name]:] for name, port in ports.items()})
                    raise
                finally:
                    (args.output / "results.json").write_text(json.dumps(guard.clean(result), indent=2) + "\n", encoding="utf-8")
        result["status"] = "CAPTURE COMPLETE"
    except BaseException as error:
        result.update(status="INTERRUPTED" if isinstance(error, KeyboardInterrupt) else "FAILED", error=guard.clean(str(error)))
        raise
    finally:
        for port in ports.values(): port.close()
        result["ended_utc"] = utc_now()
        (args.output / "results.json").write_text(json.dumps(guard.clean(result), indent=2) + "\n", encoding="utf-8")
        hashes = {path.relative_to(args.output).as_posix(): digest(path) for path in sorted(args.output.rglob("*")) if path.is_file()}
        (args.output / "artifact-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n", encoding="utf-8")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    child = sub.add_parser("plan"); child.add_argument("--output", type=pathlib.Path, required=True)
    child.add_argument("--with-tx", action="store_true")
    child = sub.add_parser("capture")
    child.add_argument("--identities", type=pathlib.Path, required=True); child.add_argument("--plan", type=pathlib.Path, required=True)
    child.add_argument("--output", type=pathlib.Path, required=True); child.add_argument("--rf-authorized", action="store_true")
    args = parser.parse_args()
    if args.command == "plan":
        plan = default_plan(args.with_tx); validate_plan(plan)
        args.output.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8"); return 0
    return capture(args)


if __name__ == "__main__":
    raise SystemExit(main())
