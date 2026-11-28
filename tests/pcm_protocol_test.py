#!/usr/bin/env python3
"""Compile the actual VLC transport-independent functions against a host fixture.

Only function extraction is performed; the production implementation is neither
rewritten nor duplicated. Windows I/O and VLC output are mocked by the C fixture.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--output-dir", type=Path)
parser.add_argument("--sanitizer", choices=("none", "address,undefined"), default="none")
parser.add_argument("--compiler", default=os.environ.get("CC", "cc"))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = root / "tools/vlc/cydpcm.c"
text = source.read_text()
# Fail visibly if source boundaries change; never silently test stale copies.
types = text[text.index("enum {"):text.index("static int Open(")]
functions = text[text.index("static bool SendCommand("):text.index("static int Control(")]
for name in ("SendCommand", "Consume", "FindFrame", "SampleTime", "PlayFrame", "Demux"):
    if name + "(" not in functions:
        raise SystemExit("missing actual plugin function: " + name)
with tempfile.TemporaryDirectory(prefix="diviner-pcm-") as temporary:
    build = args.output_dir.resolve() if args.output_dir else Path(temporary)
    build.mkdir(parents=True, exist_ok=True)
    (build/"pcm-types.inc").write_text(types)
    (build/"pcm-functions.inc").write_text(functions)
    exe = build/"pcm-host-test"
    cmd = [args.compiler, "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
           "-I", str(root/"src"), "-I", str(build), str(root/"tests/pcm_host_test.c"), "-o", str(exe)]
    if args.sanitizer != "none":
        cmd += ["-fsanitize=" + args.sanitizer, "-fno-omit-frame-pointer"]
    report = {"kind":"actual VLC parser with mocked Windows serial and VLC output",
              "source_sha":subprocess.check_output(["git","rev-parse","HEAD"],cwd=root,text=True).strip(),
              "dirty_paths":subprocess.check_output(["git","status","--porcelain"],cwd=root,text=True).splitlines(),
              "plugin_sha256":hashlib.sha256(source.read_bytes()).hexdigest(),
              "protocol_sha256":hashlib.sha256((root/"src/cyd_pcm_protocol.h").read_bytes()).hexdigest(),
              "compiler":subprocess.check_output([args.compiler,"--version"],text=True).splitlines()[0],
              "command":cmd, "sanitizer":args.sanitizer}
    compiled = subprocess.run(cmd, capture_output=True, text=True)
    (build/"build.log").write_text(compiled.stdout + compiled.stderr)
    report["build_exit"] = compiled.returncode
    status = compiled.returncode
    if status:
        print(compiled.stdout + compiled.stderr, end="")
    else:
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        output = result.stdout + result.stderr
        print(output, end=""); (build/"run.log").write_text(output)
        status = result.returncode; report["run_exit"] = status
        if status and "LeakSanitizer does not work under ptrace" in output:
            report["status"] = "BLOCKED"
    report.setdefault("status", "PASS" if status == 0 else "FAIL")
    (build/"report.json").write_text(json.dumps(report, indent=2) + "\n")
    raise SystemExit(0 if status == 0 else 2 if report["status"] == "BLOCKED" else 1)
