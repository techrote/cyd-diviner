"""Observed color-window proof and pooling fixtures; no physical measurements."""
import copy
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from capture import validate_color_run, validate_plan
from summarize import settings_scope
import test_capture_mission as mission


def color_record(format="rgb111", layers=2):
    enabled = format != "off"
    bits, alpha = {"off": (0, 0), "rgb111": (3, 0), "rgb444": (12, 0), "rgba5551": (15, 1)}[format]
    base = ((320 * (bits + alpha) + 7) // 8) * 8 if enabled else 0
    overlay = base if layers == 2 else 0
    coverage = 320 if enabled and layers == 2 and not alpha else 0
    packed = base + overlay + coverage
    return {"schema": 1, "type": "color_run", "run_id": "r001_1", "format": format,
            "enabled": enabled, "effect_layers": layers, "source_raster_bits_per_pixel": 16,
            "output_spi_bits_per_pixel": 16, "packed_colour_bits_per_pixel": bits,
            "packed_alpha_bits_per_pixel": alpha, "packed_storage_bits_per_pixel": bits + alpha,
            "external_coverage_bits_per_pixel": int(coverage > 0), "layout_fits": True,
            "packed_base_payload_bytes": base, "packed_overlay_payload_bytes": overlay,
            "coverage_payload_bytes": coverage, "packed_total_payload_bytes": packed,
            "rgb565_dma_reserved_bytes": 10240, "scratch_required_bytes": 10240 + packed,
            "canvas_backing_reserved_bytes": 153600, "backing_reservation_changed": False,
            "successful_applied_frames": 5 if enabled else 0, "native_frames": 0 if enabled else 5,
            "window_assignment": "renderer_DMA_completion_time", "receipt_scope": "postwindow_actual_renderer_layout",
            "steady_begin_us": 123000000, "end_us": 133000000,
            "bands_applied": 150 if enabled else 0, "rejected_frames": 0,
            "first_applied_offset_us": 20000 if enabled else None,
            "last_applied_offset_us": 9000000 if enabled else None}


class ColorSerial(mission.FirmwareSerial):
    def write(self, data):
        if data.strip() == b"perf export":
            record = color_record()
            record.pop("schema")
            self.emit(**record)
        return super().write(data)


class CaptureColorTests(unittest.TestCase):
    def test_optional_old_plans_and_each_observed_packed_format(self):
        self.assertIsNone(validate_color_run([], "r001_1", None, 10000))
        for format in ("off", "rgb111", "rgb444", "rgba5551"):
            record = color_record(format)
            expected = {"format": format, "effect_layers": 2}
            self.assertIs(validate_color_run([record], "r001_1", expected, 10000), record)

    def test_ack_or_wrong_window_or_no_applied_frame_is_insufficient(self):
        expected = {"format": "rgb111", "effect_layers": 2}
        for change in ({"run_id": "another"}, {"effect_layers": 1}, {"format": "rgb444"},
                       {"successful_applied_frames": 0}, {"rejected_frames": 1},
                       {"last_applied_offset_us": 10000001}, {"canvas_backing_reserved_bytes": 4},
                       {"packed_total_payload_bytes": 0}, {"output_spi_bits_per_pixel": 3}):
            with self.subTest(change=change):
                record = {**color_record(), **change}
                with self.assertRaises(RuntimeError):
                    validate_color_run([record], "r001_1", expected, 10000)
        for records in ([], [{"type": "ack", "ok": True}], [color_record(), color_record()]):
            with self.assertRaises(RuntimeError):
                validate_color_run(records, "r001_1", expected, 10000)
        with self.assertRaises(ValueError):
            validate_plan({"schema": "cyd-perf-plan/1", "workloads": [{"name": "bad",
                "expected_color": {**expected, "successful_applied_frames": 5}}]})

    def test_observed_settings_split_scope_but_counts_and_pre_frame_layout_do_not(self):
        run = {"settings": {}, "target": [color_record()], "setup_target": [
            {"type": "scene_settings", "scene": "color", "effect_layers": 1, "format": "rgb111"}]}
        other = copy.deepcopy(run)
        other["target"][0].update(successful_applied_frames=90, bands_applied=2700,
            first_applied_offset_us=30000, last_applied_offset_us=8000000, host_received_elapsed_s=50)
        other["setup_target"][0]["effect_layers"] = 2
        self.assertEqual(settings_scope(run), settings_scope(other))
        for change in ({"effect_layers": 1}, {"format": "rgb444"}, {"canvas_backing_reserved_bytes": 200000}):
            other = copy.deepcopy(run)
            other["target"][0].update(change)
            self.assertNotEqual(settings_scope(run), settings_scope(other))

    def test_capture_verifies_exported_record_and_preserves_failed_evidence(self):
        for expected_format in ("rgb111", "rgb444"):
            with tempfile.TemporaryDirectory() as directory, mock.patch.object(mission, "FirmwareSerial", ColorSerial):
                workloads = [{"name": "color fixture", "commands": ["perf scene visions"],
                    "expected_color": {"format": expected_format, "effect_layers": 2}}]
                if expected_format == "rgb111":
                    result = mission.MissionProtocolTests.run_plan(self, directory, workloads)
                    self.assertEqual(result["status"], "CAPTURE COMPLETE")
                else:
                    with self.assertRaisesRegex(RuntimeError, "Observed color"):
                        mission.MissionProtocolTests.run_plan(self, directory, workloads)
                    output = pathlib.Path(directory) / "capture"
                    result = json.loads((output / "results.json").read_text())
                    self.assertEqual(result["status"], "FAILED")
                    self.assertTrue(any(record["type"] == "color_run" for record in result["runs"][0]["target_partial"]))
                    self.assertIn(b'"type": "color_run"', (output / "serial-rx.bin").read_bytes())


if __name__ == "__main__":
    unittest.main()
