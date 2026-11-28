#!/usr/bin/env python3
"""Installed-dependency provenance fixtures; no network or hardware access."""
import json
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Environment:
    def subst(self, value):
        assert value == "$PROJECT_DIR", value
        return str(ROOT)
    def AddPostAction(self, *args):
        pass


identity = runpy.run_path(str(ROOT / "tools/firmware_metadata.py"),
                         init_globals={"env": Environment(), "Import": lambda *args: None})["library_identity"]


class DependencyIdentity(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="diviner-dependency-")
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)

    def metadata(self, relative, version, uri=None):
        path = self.folder / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        value = {"type":"library", "name":"Touch fixture", "version":version,
                 "spec":{"owner":None, "id":None, "name":"Touch fixture",
                         "requirements":None, "uri":uri}}
        path.write_text(json.dumps(value))

    def test_versionless_git_library_records_installed_version_and_full_revision(self):
        def git(*args):
            return subprocess.check_output(["git", "-C", str(self.folder), *args],
                                           text=True, stderr=subprocess.DEVNULL, timeout=10).strip()
        git("init", "-q")
        git("config", "user.name", "Dependency fixture")
        git("config", "user.email", "fixture@example.invalid")
        (self.folder / "library.json").write_text('{"name":"Touch fixture"}')
        git("add", "library.json")
        git("commit", "-qm", "Independent dependency fixture")
        revision = git("rev-parse", "HEAD")
        version = "0.0.0+sha." + revision[:7]
        uri = "git+https://github.com/PaulStoffregen/XPT2046_Touchscreen.git#v1.4"
        self.metadata(".git/.piopm", version, uri)
        result = identity(self.folder, {"name":"Touch fixture"})
        self.assertEqual(result["version"], version)
        self.assertEqual(result["resolved_version"], version)
        self.assertEqual(result["resolved_revision"], revision)
        self.assertEqual(result["source_spec"]["uri"], uri)

    def test_declared_and_resolved_versions_remain_distinct(self):
        self.metadata(".piopm", "1.2.3+installed")
        result = identity(self.folder, {"name":"Declared fixture", "version":"1.2.3"})
        self.assertEqual(result["name"], "Declared fixture")
        self.assertEqual(result["version"], "1.2.3")
        self.assertEqual(result["resolved_version"], "1.2.3+installed")
        self.assertNotIn("resolved_revision", result)

    def test_metadata_root_fallback_and_missing_manifest_name(self):
        self.metadata(".piopm", "2.3.4")
        result = identity(self.folder, {})
        self.assertEqual((result["name"], result["version"]), ("Touch fixture", "2.3.4"))

    def test_no_installed_metadata_preserves_explicit_unknown(self):
        result = identity(self.folder, {"name":"Unknown fixture"})
        self.assertIsNone(result["version"])
        self.assertNotIn("resolved_revision", result)

    def test_vcs_metadata_location_precedes_package_root(self):
        self.metadata(".hg/.piopm", "3.4.5")
        self.metadata(".piopm", "9.9.9")
        self.assertEqual(identity(self.folder, {})["version"], "3.4.5")


if __name__ == "__main__":
    unittest.main()
