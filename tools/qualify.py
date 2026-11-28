#!/usr/bin/env python3
"""Run bounded independent portable qualification and retain every outcome."""
import argparse
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from source_provenance import capture


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--sanitizer", choices=["none", "address", "thread"], default="none")
    p.add_argument("--output", type=Path, default=ROOT / "delivery/portable")
    a = p.parse_args()
    out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
    env.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    env.setdefault("TSAN_OPTIONS", "halt_on_error=1")
    report = {**capture(ROOT), "platform": platform.platform(), "sanitizer": a.sanitizer,
              "sanitizer_environment": {k: env[k] for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS", "TSAN_OPTIONS")},
              "scope": "Portable production kernels, actual instrument renderers with test HAL, callback lifetime; no physical timing qualification",
              "cases": []}

    def run(name, cmd, timeout=240, nested_report=None):
        started = time.monotonic()
        if nested_report is not None:
            nested_report.unlink(missing_ok=True)
        process = subprocess.Popen(cmd, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            log, _ = process.communicate(timeout=timeout)
            code = process.returncode
            blocked = ("ThreadSanitizer: unexpected memory mapping" in log or
                       "LeakSanitizer has encountered a fatal error" in log or
                       "LeakSanitizer does not work under ptrace" in log)
            # Nested suites can contain both an application failure and a blocked
            # runtime. Preserve their explicit FAIL precedence; text alone cannot
            # downgrade an application defect to an environmental blocker.
            if nested_report is not None and nested_report.is_file():
                try:
                    status = json.loads(nested_report.read_text()).get("status", "FAIL")
                except (ValueError, OSError) as e:
                    status = "FAIL"; log += "\nUnreadable nested report: " + str(e)
                if status not in ("PASS", "FAIL", "BLOCKED") or (code != 0 and status == "PASS"):
                    status = "FAIL"
            else:
                status = "PASS" if code == 0 else "BLOCKED" if blocked and nested_report is None else "FAIL"
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            log, _ = process.communicate()
            log += f"\nQualification timeout after {timeout}s\n"; code = None; status = "FAIL"
        (out / (name + ".log")).write_text(log)
        report["cases"].append({"name": name, "command": cmd, "exit": code, "status": status,
                                "elapsed_seconds": round(time.monotonic()-started, 3)})
        print(name + ": " + status, flush=True)
        if status != "PASS": print(log[-6000:], flush=True)
        return status == "PASS"

    audio = ["--audio"] if a.sanitizer == "none" else []
    run("dsp", [sys.executable, "tests/dsp_run.py", "--sanitizer", a.sanitizer,
                "--output-dir", str(out / "dsp"), *audio], nested_report=out / "dsp/report.json")
    radio_san = "address,undefined" if a.sanitizer == "address" else a.sanitizer
    run("radio", [sys.executable, "tests/run_radio.py", "--sanitizer", radio_san,
                  "--output-dir", str(out / "radio"), *audio], nested_report=out / "radio/report.json")
    if a.sanitizer != "thread":
        run("pcm-protocol", [sys.executable, "tests/pcm_protocol_test.py", "--sanitizer", radio_san,
                             "--output-dir", str(out / "pcm")], nested_report=out / "pcm/report.json")
    flags = ["-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-pthread"]
    if a.sanitizer != "none":
        flags += ["-fsanitize=" + radio_san, "-fno-omit-frame-pointer"]
    if a.sanitizer == "thread": flags += ["-fno-pie", "-no-pie"]
    exe = out / "lifecycle"
    if run("lifecycle-build", [env.get("CXX", "g++"), *flags, "tests/lifecycle.cpp", "-o", str(exe)]):
        run("lifecycle", [str(exe)], timeout=60)
    if a.sanitizer == "none":
        run("firmware-packaging", [sys.executable, "tests/package_firmware_test.py"])
    report["status"] = ("FAIL" if any(x["status"] == "FAIL" for x in report["cases"]) else
                        "BLOCKED" if any(x["status"] == "BLOCKED" for x in report["cases"]) else "PASS")
    if capture(ROOT) != {k: report[k] for k in ("source_sha", "source_tree", "dirty", "source_snapshot")}:
        report["status"] = "FAIL"; report["error"] = "Source changed during qualification"
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if report["status"] == "PASS" else 2 if report["status"] == "BLOCKED" else 1


if __name__ == "__main__":
    raise SystemExit(main())
