"""Publication provenance and completion guards; fixtures are not measurements."""
import csv
import hashlib
import json
import pathlib
import tempfile
import unittest
import zipfile

from publish_evidence import publish, stress_rows
from summarize import summarize, write_csv
from test_report import capture, measured


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fixture(root):
    source = root / "physical" / "preflight-fixture"
    source.mkdir(parents=True)
    result = capture([measured()])
    result["plan"]["schema"] = "cyd-perf-plan/1"
    (source / "results.json").write_text(json.dumps(result) + "\n", encoding="utf-8")
    for name in ("samples.csv", "metrics.csv", "target.jsonl", "serial-rx.bin", "serial-events.jsonl", "serial-text.log"):
        (source / name).write_bytes(b"parser fixture, not measurement\n")
    hashes = {path.name: digest(path) for path in source.iterdir()}
    (source / "artifact-hashes.json").write_text(json.dumps(hashes) + "\n", encoding="utf-8")
    summary = summarize(result)
    (source / "summary.json").write_text(json.dumps(summary) + "\n", encoding="utf-8")
    for category in ("scenes", "metrics", "kernels", "spi", "missing"):
        write_csv(source / f"summary-{category}.csv", summary[category])
    (source / "figures").mkdir()
    files = []
    for extension in ("png", "svg"):
        name = "fixture." + extension
        (source / "figures" / name).write_bytes(b"hash/copy fixture, not image evidence")
        files.append({"file": name, "sha256": digest(source / "figures" / name)})
    plot_manifest = {"schema": "cyd-perf-figures/1", "capture_status": "CAPTURE COMPLETE",
                     "source_sha": result["source_sha"], "firmware_sha256": result["firmware_sha256"],
                     "profile": result["profile"], "input_sha256": digest(source / "results.json"),
                     "figures": [{"name": "fixture", "files": files}]}
    (source / "figures" / "manifest.json").write_text(json.dumps(plot_manifest) + "\n", encoding="utf-8")
    archive = root / "raw.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        for path in sorted(source.rglob("*")):
            if path.is_file():
                bundle.write(path, "physical/preflight-fixture/" + path.relative_to(source).as_posix())
    return source, archive


class PublicationTests(unittest.TestCase):
    def test_stress_csv_rows_preserve_identity_and_warmup_relative_timestamps(self):
        run = measured("sustained/cpu-both-cores")
        run["settings"]["stress_seconds"] = 365
        run["target"][0].update(warmup_ms=2000, temperature_method="recorded_sensor_method")
        run["target"][2].update(elapsed_ms=3000, cpu1_busy_ticks=99, cpu1_total_ticks=100,
                                 core1_busy_pct=99, heap_free_bytes=64000)
        result = capture([run])
        rows = stress_rows(result)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["source_sha"], result["source_sha"])
        self.assertEqual(rows[0]["firmware_sha256"], result["firmware_sha256"])
        self.assertEqual(rows[0]["target_elapsed_ms"], 3000)
        self.assertEqual(rows[0]["steady_elapsed_ms"], 1000)
        self.assertEqual(rows[0]["temperature_method"], "recorded_sensor_method")
        self.assertIsNone(rows[0]["core0_busy_pct"])
        self.assertEqual(rows[0]["cpu1_busy_ticks"], 99)
        self.assertEqual(rows[0]["heap_free_bytes"], 64000)

    def test_completed_snapshot_publishes_compact_data_and_archive_member_links(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source, archive = fixture(root)
            before = {path.relative_to(source).as_posix(): digest(path) for path in source.rglob("*") if path.is_file()}
            output = root / "published-preflight"
            publish(source, output, archive, "physical/preflight-fixture", "preflight-fixture")
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual(manifest["capture_status"], "CAPTURE COMPLETE")
            self.assertEqual(manifest["archive"]["sha256"], digest(archive))
            self.assertEqual(manifest["stress_rows"], 0)
            self.assertFalse((output / "results.json").exists())
            self.assertFalse((output / "serial-rx.bin").exists())
            self.assertEqual(json.loads((output / "summary.json").read_text()), summarize(capture_with_schema()))
            report = (output / "REPORT.md").read_text(encoding="utf-8")
            self.assertIn("[results.json](../raw.zip) (archive member `physical/preflight-fixture/results.json`)", report)
            self.assertIn("short preflight", report)
            self.assertIn("figures/fixture.png", report)
            with (output / "stress-samples.csv").open(newline="", encoding="utf-8") as stream:
                reader = csv.DictReader(stream)
                self.assertIn("firmware_sha256", reader.fieldnames)
                self.assertEqual(list(reader), [])
            for name, receipt in manifest["outputs"].items():
                self.assertEqual(receipt["sha256"], digest(output / name))
            after = {path.relative_to(source).as_posix(): digest(path) for path in source.rglob("*") if path.is_file()}
            self.assertEqual(before, after)

    def test_incomplete_status_and_missing_planned_repetitions_are_rejected(self):
        for mutation in (lambda result: result.update(status="IN PROGRESS"),
                         lambda result: result["plan"].update(repetitions=2)):
            with tempfile.TemporaryDirectory() as temporary:
                root = pathlib.Path(temporary)
                source, archive = fixture(root)
                result = json.loads((source / "results.json").read_text())
                mutation(result)
                (source / "results.json").write_text(json.dumps(result))
                with self.assertRaises(ValueError):
                    publish(source, root / "out", archive, "physical/preflight-fixture", "preflight-fixture")
                self.assertFalse((root / "out").exists())

    def test_changed_raw_artifact_and_stale_figures_are_rejected(self):
        for name in ("serial-rx.bin", "figures/fixture.png"):
            with tempfile.TemporaryDirectory() as temporary:
                root = pathlib.Path(temporary)
                source, archive = fixture(root)
                (source / name).write_bytes(b"changed")
                with self.assertRaises(ValueError):
                    publish(source, root / "out", archive, "physical/preflight-fixture", "preflight-fixture")
                self.assertFalse((root / "out").exists())

    def test_wrong_archive_prefix_or_member_bytes_cannot_pass_provenance(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source, archive = fixture(root)
            with self.assertRaisesRegex(ValueError, "Missing archive member"):
                publish(source, root / "out", archive, "physical/another", "preflight-fixture")
            bad = root / "wrong.zip"
            with zipfile.ZipFile(archive) as original, zipfile.ZipFile(bad, "w") as changed:
                for name in original.namelist():
                    changed.writestr(name, b"wrong bytes" if name.endswith("/results.json") else original.read(name))
            with self.assertRaisesRegex(ValueError, "differs from completed capture"):
                publish(source, root / "out", bad, "physical/preflight-fixture", "preflight-fixture")

    def test_publication_cannot_write_into_raw_folder_or_overwrite_existing_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source, archive = fixture(root)
            with self.assertRaisesRegex(ValueError, "inside the raw capture"):
                publish(source, source / "published", archive, "physical/preflight-fixture", "preflight-fixture")
            output = root / "existing"
            output.mkdir()
            with self.assertRaisesRegex(ValueError, "already exists"):
                publish(source, output, archive, "physical/preflight-fixture", "preflight-fixture")


def capture_with_schema():
    result = capture([measured()])
    result["plan"]["schema"] = "cyd-perf-plan/1"
    return result


if __name__ == "__main__":
    unittest.main()
