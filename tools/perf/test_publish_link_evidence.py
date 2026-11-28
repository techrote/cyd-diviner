"""Published report links and receipts from a synthetic terminal capture."""
import hashlib
import json
from pathlib import Path
import re
import tempfile
import unittest
import zipfile

from publish_link_evidence import publish
from test_link_analyze import capture, run


class LinkPublicationTests(unittest.TestCase):
    def test_publication_links_actual_manifest_and_preserves_failed_status_and_raw_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            raw = root / "fixture"
            raw.mkdir()
            source = json.dumps(capture([run("one")]), indent=2).encode() + b"\n"
            (raw / "results.json").write_bytes(source)
            hashes = {"results.json": hashlib.sha256(source).hexdigest()}
            (raw / "artifact-hashes.json").write_text(json.dumps(hashes) + "\n", encoding="utf-8")
            archive = root / "raw.zip"
            with zipfile.ZipFile(archive, "w") as bundle:
                for file in raw.iterdir():
                    bundle.write(file, "physical/fixture/" + file.name)
            archive_before = archive.read_bytes()
            output = root / "published"
            publish(raw, output, archive)
            report = (output / "LINK-REPORT.md").read_text(encoding="utf-8")
            self.assertIn("[publication manifest](manifest.json)", report)
            self.assertNotIn("link-analysis-manifest.json", report)
            self.assertFalse((output / "link-analysis-manifest.json").exists())
            for target in re.findall(r"\[[^\]]*\]\(([^)]+)\)", report):
                if "://" not in target:
                    self.assertTrue((output / target).is_file(), target)
            manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["capture_status"], "FAILED")
            self.assertEqual(manifest["input_results_sha256"], hashes["results.json"])
            for name, expected in manifest["outputs"].items():
                payload = (output / name).read_bytes()
                self.assertEqual(len(payload), expected["bytes"], name)
                self.assertEqual(hashlib.sha256(payload).hexdigest(), expected["sha256"], name)
            self.assertEqual(archive_before, archive.read_bytes())
            self.assertEqual(source, (raw / "results.json").read_bytes())


if __name__ == "__main__":
    unittest.main()
