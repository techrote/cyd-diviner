"""Build and validate the portable suite with a host C++17 compiler.

All emitted timing data are host replay, never physical ESP32 measurements.
Run on Windows with a compiler on PATH, or from WSL with python3.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import platform
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--git", default="git", help="use git.exe for a Windows worktree invoked from WSL")
    parser.add_argument("--out", type=Path, default=Path("evidence/host-kernels"))
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--sanitize", action="store_true", help="also build and run ASan/UBSan")
    args = parser.parse_args()
    if not 1 <= args.repetitions <= 10:
        parser.error("repetitions must be in 1..10")
    root = Path(__file__).resolve().parents[2]
    out = (root / args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    build = root / ".pio/perf-host"
    build.mkdir(parents=True, exist_ok=True)
    source_paths = [root / "platformio.ini", *sorted((root / "src").rglob("*"))]
    digest = hashlib.sha256()
    for path in source_paths:
        if path.is_file():
            digest.update(path.relative_to(root).as_posix().encode() + b"\0" + path.read_bytes())
    receipt = {
        "schema": 1,
        "evidence_class": "host_replay",
        "utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source_sha": subprocess.check_output([args.git, "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "source_digest": digest.hexdigest(),
        "source_dirty": bool(subprocess.check_output([args.git, "status", "--porcelain"], cwd=root, text=True).strip()),
        "host": platform.platform(),
        "compiler": subprocess.check_output([args.cxx, "--version"], text=True),
        "commands": [],
        "limitations": [
            "Host timings are not target timings and imply no ESP32/S3/C3 throughput.",
            "Float checksum equality across architectures is not required; numeric error is checked separately.",
            "DSP equivalence is checked over one fresh 128/512 sample block; long-term model divergence remains measurable.",
            "Working bytes describe selected context; allocated common workspace is larger.",
        ],
    }

    def run(command: list[str], prefix: str) -> subprocess.CompletedProcess[str]:
        completed = subprocess.run(command, cwd=root, capture_output=True, text=True)
        (out / f"{prefix}.stdout.log").write_text(completed.stdout, encoding="utf-8")
        (out / f"{prefix}.stderr.log").write_text(completed.stderr, encoding="utf-8")
        receipt["commands"].append({"argv": command, "returncode": completed.returncode, "log_prefix": prefix})
        return completed

    common = [args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion", "-Werror", "-I", "src", "src/perf_kernels.cpp", "tools/perf/kernels_host.cpp"]
    valid = True
    executable = build / ("kernels_host.exe" if platform.system() == "Windows" else "kernels_host")
    compiled = run([*common, "-O2", "-o", str(executable)], "compile")
    valid &= compiled.returncode == 0
    if valid:
        for repetition in range(1, args.repetitions + 1):
            completed = run([str(executable), "--selftest"], f"repetition-{repetition}")
            valid &= completed.returncode == 0
            if completed.returncode == 0:
                result = json.loads(completed.stdout)
                (out / f"repetition-{repetition}.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
                valid &= result["valid"]
    if args.sanitize:
        sanitized = build / ("kernels_sanitized.exe" if platform.system() == "Windows" else "kernels_sanitized")
        compiled = run([*common, "-O1", "-g", "-fsanitize=undefined,address", "-fno-sanitize-recover=all", "-o", str(sanitized)], "compile-sanitized")
        valid &= compiled.returncode == 0
        if compiled.returncode == 0:
            completed = run([str(sanitized), "--selftest"], "sanitized")
            valid &= completed.returncode == 0
    receipt["valid"] = bool(valid)
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"valid": bool(valid), "receipt": str(out / "receipt.json"), "evidence_class": "host_replay"}))
    return 0 if valid else 1


if __name__ == "__main__":
    raise SystemExit(main())
