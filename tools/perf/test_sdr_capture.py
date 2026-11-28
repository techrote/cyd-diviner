"""IQS framing/integrity fixtures only; no RF or device access."""
import binascii
import io
import pathlib
import struct
import tempfile
import unittest
from unittest import mock

from sdr_capture import IQ_HEADER, IQSDecoder, IndexedI8Writer, SDRPort, capture, capture_run, default_plan, pack_symbols
from link_capture import SecretGuard
from test_link_mission import Clock


def frame(sequence, index, payload=b"\x01\x02\x03\x04", bits=8, flags=0):
    count = len(payload) * 8 // (bits * 2)
    header = IQ_HEADER.pack(b"IQS1", sequence, index, count, bits, flags, 64, 7, 7)
    wire = header + payload
    return wire + struct.pack("<I", binascii.crc32(wire) & 0xFFFFFFFF)


START = b"IQS 16000000 64 8 7 0 2437\n"
END = b"IQSEND 0 0 1 16384 100000 5 6 2 0 0 0 0\n"


class IQSParserTests(unittest.TestCase):
    def test_empty_tx_poll_never_enters_the_serial_timeout_wait(self):
        port = SDRPort.__new__(SDRPort)
        port.serial = mock.Mock(in_waiting=0)
        self.assertEqual(port.read(nonblocking=True), b"")
        port.serial.read.assert_not_called()

    def test_packing_is_msb_first_and_authorization_precedes_any_port_open(self):
        self.assertEqual(pack_symbols(bytes([0, 1, 0, 1, 1, 0, 1, 0, 1])), b"\x5a\x80")
        with self.assertRaises(ValueError): pack_symbols(b"\x02")
        with mock.patch("sdr_capture.SDRPort") as port:
            with self.assertRaisesRegex(ValueError, "rf-authorized"):
                capture(mock.Mock(rf_authorized=False))
            port.assert_not_called()

    def test_every_chunk_boundary_preserves_crc_index_gaps_and_tail(self):
        first, last = frame(0, 0, b"IQS1\n\x00"), frame(2, 6, flags=3)
        wire = START + first + last + END
        for boundary in range(1, len(wire)):
            decoder = IQSDecoder()
            decoder.feed(wire[:boundary])
            decoder.feed(wire[boundary:])
            result = decoder.summary()
            self.assertEqual(result["frames"], 2)
            self.assertEqual(result["crc_errors"], 0)
            self.assertEqual(result["missing_frame_sequences"], 1)
            self.assertEqual(result["missing_output_samples_between_valid_frames"], 3)
            self.assertEqual(result["gap_flag_frames"], 1)
            self.assertEqual(result["drop_flag_frames"], 1)
            self.assertEqual(result["start"]["output_sample_rate_hz"], 250000)
            self.assertTrue(result["finite_boundary_observed"])

    def test_corrupt_frame_resynchronizes_without_inventing_missing_iq(self):
        bad = bytearray(frame(1, 2))
        bad[-1] ^= 1
        decoder = IQSDecoder()
        decoder.feed(START + frame(0, 0) + bad + frame(2, 4) + END)
        self.assertEqual(len(decoder.frames), 2)
        self.assertEqual(decoder.crc_errors, 1)
        self.assertEqual(decoder.missing_output_samples, 2)
        self.assertEqual(decoder.missing_sequences, 1)
        missing = IQSDecoder()
        missing.feed(START + frame(0, 0))
        self.assertFalse(missing.summary()["finite_boundary_observed"])
        with self.assertRaisesRegex(ValueError, "IQSEND"):
            missing.feed(b"IQSEND malformed\n")

    def test_native_i8_indexing_marks_erasures_and_omits_overlap(self):
        output = io.BytesIO()
        writer = IndexedI8Writer(output)
        decoder = IQSDecoder(writer)
        decoder.feed(START + frame(0, 10, b"\x01\x02\x03\x04") + frame(1, 14, b"\x05\x06\x07\x08")
                     + frame(1, 14, b"\x05\x06\x07\x08") + END)
        self.assertEqual(output.getvalue(), b"\x01\x02\x03\x04" + b"\0" * 4 + b"\x05\x06\x07\x08")
        self.assertEqual(writer.origin, 10)
        self.assertEqual(writer.erasures[0]["samples"], 2)
        self.assertEqual(decoder.duplicate_sequences, 1)
        self.assertEqual(decoder.overlapping_output_samples, 2)
        self.assertIn("not measured RF", writer.erasures[0]["representation"])

    def test_four_and_sixteen_bit_framing_do_not_silently_quantize(self):
        decoder = IQSDecoder()
        decoder.feed(frame(0, 0, b"\x8f\x70", bits=4) + frame(1, 2, b"\x01\x00\xfe\xff", bits=16))
        self.assertEqual([row["samples"] for row in decoder.frames], [2, 1])
        with self.assertRaisesRegex(ValueError, "no implicit quantization"):
            IQSDecoder(IndexedI8Writer(io.BytesIO())).feed(frame(0, 0, bits=16))


class FakeTX:
    def __init__(self):
        self.records, self.started, self.command = [], 0, None
    def request(self, command, *_args, **kwargs):
        if command.startswith("permit "):
            assert kwargs["private"] is True
            return {"type": "permit", "ok": True}
        return {"type": "ready"}
    def write(self, payload): self.payload = payload
    def until(self, *_args, **_kwargs): return {"type": "loaded", "bits": len(self.payload) * 8}
    def log(self, *_args, **_kwargs): pass
    def send(self, command, private=False):
        assert private
        self.command = command
    def read(self, nonblocking=False):
        assert nonblocking
        if self.command and not self.records:
            _, mode, baud, backoff, _ = self.command.split()
            self.records.extend([{"type": "tx_result", "mode": mode, "requested_baud": int(baud),
                "backoff_quarter_db": int(backoff), "requested_bits": len(self.payload) * 8,
                "processed_bits": len(self.payload) * 8, "aborted_late": False, "aborted_deadline": False,
                "start_us": 1000, "end_us": 2000, "trace_count": 1},
                {"type": "tx_edge", "index": 0, "completion_cycle": 20, "requested_level": 0}])


class FakeRX:
    def __init__(self, tx, missing_end=False):
        self.tx, self.missing_end = tx, missing_end
        self.pending, self.offset, self.stage, self.requests = bytearray(), 0, 0, []
    def request(self, command, *_args):
        self.requests.append(command)
        return "GAIN MANUAL 40 0 83 1" if command == "GAIN?" else "OK"
    def send(self, command): self.iqs = command
    def read(self, binary=False):
        assert binary
        if self.stage == 0:
            self.stage = 1
            result = b"IQS 16000000 64 8 7 2 2433\n" + frame(0, 0)
        elif self.stage == 1 and self.tx.records:
            self.stage = 2
            result = frame(1, 2) + (b"" if self.missing_end else END)
        else:
            result = b""
        self.offset += len(result)
        return result


class SDRBackendTests(unittest.TestCase):
    def test_finite_backend_uploads_before_rx_and_never_queries_uncertain_rx_boundary(self):
        for missing in (False, True):
            with tempfile.TemporaryDirectory() as temporary:
                root = pathlib.Path(temporary)
                tx, plan = FakeTX(), default_plan()
                case = plan["workloads"][0].copy()
                case["bit_count"] = 496  # whole bytes for this short transport fixture
                run = {"settings": case}
                rx = FakeRX(tx, missing)
                with mock.patch("sdr_capture.time.perf_counter", Clock()):
                    if missing:
                        with self.assertRaisesRegex(RuntimeError, "Missing IQSEND"):
                            capture_run(run, tx, rx, plan, root / "plan.json", root / "run", SecretGuard())
                    else:
                        capture_run(run, tx, rx, plan, root / "plan.json", root / "run", SecretGuard())
                        self.assertEqual(run["status"], "ACQUIRED")
                        self.assertIsNone(run["rf_signal_verified"])
                self.assertEqual("RELEASE" in rx.requests, not missing)
                self.assertTrue((root / "run/iq-stream.bin").exists())
                self.assertTrue((root / "run/iq-integrity.json").exists())
                self.assertEqual(len(tx.payload), 62)


if __name__ == "__main__":
    unittest.main()
