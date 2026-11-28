#!/usr/bin/env python3
"""Exercise the real qualification runner's evidence/status guards.

Each case copies the production runner and provenance module into a temporary
Git checkout. Deterministic fake children isolate runner classification from
DSP, compilers and sanitizer availability. No firmware is built or flashed.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
QUALIFY = Path(os.environ.get("DIVINER_QUALIFY_SCRIPT", ROOT / "tools/qualify.py"))
PROVENANCE = QUALIFY.with_name("source_provenance.py")

NESTED_CHILD = '''import argparse
import json
import os
from pathlib import Path
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--output-dir", type=Path, required=True)
args, _ = parser.parse_known_args()
args.output_dir.mkdir(parents=True, exist_ok=True)
scenario = os.environ["DIVINER_GUARD_SCENARIO"] if Path(__file__).name == "dsp_run.py" else "pass"
report = args.output_dir / "report.json"
if scenario == "missing":
    print("Fixture child exits zero without required evidence")
    raise SystemExit(0)
if scenario == "malformed":
    report.write_text("{not valid JSON")
    raise SystemExit(0)
status = {"failure-with-blocker": "FAIL", "blocked": "BLOCKED"}.get(scenario, "PASS")
report.write_text(json.dumps({"status": status, "cases": [{"name": "fixture", "status": status}]}))
if scenario in ("failure-with-blocker", "blocked"):
    print("ThreadSanitizer: unexpected memory mapping (fixture marker)")
if scenario == "nonzero-pass":
    raise SystemExit(3)
raise SystemExit(2 if status == "BLOCKED" else 1 if status == "FAIL" else 0)
'''

FAKE_COMPILER = '''#!/usr/bin/env python3
from pathlib import Path
import sys
output = Path(sys.argv[sys.argv.index("-o") + 1])
output.write_text("#!/usr/bin/env python3\\nprint('Fixture lifetime executable completed')\\n")
output.chmod(0o755)
'''


class QualificationGuardTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="diviner-qualification-guard-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "tools").mkdir()
        (self.root / "tests").mkdir()
        (self.root / ".gitignore").write_text("delivery/\n__pycache__/\n*.pyc\n")
        shutil.copy2(QUALIFY, self.root / "tools/qualify.py")
        shutil.copy2(PROVENANCE, self.root / "tools/source_provenance.py")
        for name in ("dsp_run.py", "run_radio.py", "pcm_protocol_test.py"):
            (self.root / "tests" / name).write_text(NESTED_CHILD)
        # These checks have no nested report contract. Do not recursively run
        # this test if it is itself wired into the master's ordinary mode.
        for name in ("package_firmware_test.py", "dependency_metadata_test.py",
                     "qualification_guard_test.py"):
            (self.root / "tests" / name).write_text("print('Fixture check completed')\n")
        self.compiler = self.root / "fixture-cxx"
        self.compiler.write_text(FAKE_COMPILER)
        self.compiler.chmod(0o755)
        self.git("init", "-q")
        self.git("add", ".")
        self.git("-c", "user.name=Diviner fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "-qm", "Isolated qualification fixture")
        self.output = self.root / "delivery/guard"

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.root, text=True,
                                       stderr=subprocess.DEVNULL).strip()

    def run_scenario(self, scenario, expected, exit_code):
        env = dict(os.environ, DIVINER_GUARD_SCENARIO=scenario, CXX=str(self.compiler))
        result = subprocess.run([sys.executable, str(self.root / "tools/qualify.py"),
                                 "--output", str(self.output)], cwd=self.root, env=env,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, exit_code, result.stdout + result.stderr)
        report = json.loads((self.output / "report.json").read_text())
        self.assertEqual(report["status"], expected, report)
        self.assertFalse(report["dirty"], "Fixture source changed while running the harness")
        cases = {case["name"]: case for case in report["cases"]}
        self.assertEqual(cases["dsp"]["status"], expected, report)
        self.assertGreater(len(cases), 3, "The master did not execute its independent checks")
        for name, case in cases.items():
            if name != "dsp":
                self.assertEqual(case["status"], "PASS", (name, case))
        return cases, (self.output / "dsp.log").read_text()

    def test_complete_successful_evidence_is_accepted(self):
        self.run_scenario("pass", "PASS", 0)

    def test_zero_exit_without_required_report_fails(self):
        cases, log = self.run_scenario("missing", "FAIL", 1)
        self.assertEqual(cases["dsp"]["exit"], 0)
        self.assertIn("Fixture child exits zero", log)

    def test_stale_report_cannot_satisfy_missing_child_evidence(self):
        stale = self.output / "dsp/report.json"
        stale.parent.mkdir(parents=True)
        stale.write_text('{"status":"PASS"}')
        self.run_scenario("missing", "FAIL", 1)
        self.assertFalse(stale.exists())

    def test_malformed_required_report_fails(self):
        self.run_scenario("malformed", "FAIL", 1)

    def test_nonzero_exit_cannot_be_reported_as_pass(self):
        cases, _ = self.run_scenario("nonzero-pass", "FAIL", 1)
        self.assertEqual(cases["dsp"]["exit"], 3)

    def test_application_failure_takes_precedence_over_runtime_blocker(self):
        self.run_scenario("failure-with-blocker", "FAIL", 1)

    def test_explicit_runtime_blocker_remains_blocked(self):
        self.run_scenario("blocked", "BLOCKED", 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
