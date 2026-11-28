#!/usr/bin/env python3
"""Compile shared radio protocol and actual radio-instrument qualification."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--sanitizer", choices=("none", "address", "undefined", "address,undefined", "thread"), default="none")
parser.add_argument("--output", type=Path, help="write deterministic CV1 simulation JSON here")
parser.add_argument("--output-dir", type=Path, help="keep compiler/runtime logs, summary and optional WAVs")
parser.add_argument("--audio", action="store_true", help="write six actual-instrument WAV specimens")
parser.add_argument("--compiler", default=os.environ.get("CXX", "g++"))
parser.add_argument("--timeout", type=float, default=90, help="maximum seconds per compiler or test process")
args = parser.parse_args()
if args.timeout <= 0:
    parser.error("--timeout must be positive")
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="diviner-radio-") as temporary:
    build = args.output_dir.resolve() if args.output_dir else Path(temporary)
    build.mkdir(parents=True, exist_ok=True)
    report = {"kind": "native model and actual instrument kernels with mocked graphics/radio HAL",
              "platform": platform.platform(), "sanitizer": args.sanitizer,
              "process_timeout_seconds": args.timeout, "cases": []}
    report["source_sha"] = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True, timeout=15).strip()
    report["dirty_paths"] = subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True, timeout=15).splitlines()
    report["compiler"] = subprocess.check_output([args.compiler, "--version"], text=True, timeout=15).splitlines()[0]
    flags = ["-std=c++17", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-pthread", "-I", str(root / "src")]
    if args.sanitizer != "none":
        flags += ["-fsanitize=" + args.sanitizer, "-fno-omit-frame-pointer"]
    if args.sanitizer == "thread":
        flags += ["-fno-pie", "-no-pie"]
    env = dict(os.environ)
    env.setdefault("ASAN_OPTIONS", "halt_on_error=1")
    env.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    env.setdefault("TSAN_OPTIONS", "halt_on_error=1")
    def bounded(command):
        try:
            return subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  env=env, timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            output = error.stdout or ""
            if isinstance(output, bytes): output = output.decode(errors="replace")
            return subprocess.CompletedProcess(command, 124,
                output + f"\nFAIL: process exceeded {args.timeout:g} second timeout\n")
    failed = blocked = False
    for name, source, extra in [
        ("coven-simulation", "coven_sim.cpp", []),
        ("radio-instrument-kernels", "radio_instrument_test.cpp", ["-I", str(root / "tests/dsp_stubs")]),
    ]:
        exe = build / name
        command = [args.compiler, *flags, *extra, str(root / "tests" / source), "-o", str(exe)]
        compiled = bounded(command)
        (build / (name + "-build.log")).write_text(compiled.stdout)
        case = {"name": name, "command": command, "build_exit": compiled.returncode}
        if compiled.returncode:
            case["status"] = "FAIL"; failed = True; print(compiled.stdout, end="")
        else:
            run = [str(exe)]
            if name == "coven-simulation":
                out = args.output.resolve() if args.output else build / "coven-results.json"
                out.parent.mkdir(parents=True, exist_ok=True); run.append(str(out))
            elif args.audio:
                run.append(str(build / "audio"))
            result = bounded(run)
            (build / (name + ".log")).write_text(result.stdout)
            case.update({"run": run, "exit": result.returncode})
            if result.returncode not in (0, 124) and ("ThreadSanitizer: unexpected memory mapping" in result.stdout or
                                      "LeakSanitizer does not work under ptrace" in result.stdout):
                case["status"] = "BLOCKED"; blocked = True
            else:
                case["status"] = "PASS" if result.returncode == 0 else "FAIL"
                failed |= result.returncode != 0
            print(result.stdout, end="")
        print(name + ": " + case["status"], flush=True)
        report["cases"].append(case)
    report["status"] = "FAIL" if failed else "BLOCKED" if blocked else "PASS"
    (build / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    raise SystemExit(1 if failed else 2 if blocked else 0)
