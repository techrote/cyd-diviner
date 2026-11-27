"""Record exact firmware artifacts without publishing flash backups or credentials."""
import argparse
import datetime
import hashlib
import json
import pathlib
import platform
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def command(args):
    result = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, errors="replace")
    return {"command": args, "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pio", default="pio")
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--build-dir", type=pathlib.Path, default=ROOT / ".pio" / "build")
    parser.add_argument("profiles", nargs="+")
    args = parser.parse_args()
    status = command(["git", "status", "--porcelain"])
    paths = [ROOT / "platformio.ini"] + sorted((ROOT / "src").rglob("*"))
    source = hashlib.sha256()
    for path in paths:
        if path.is_file():
            source.update(path.relative_to(ROOT).as_posix().encode() + b"\0" + path.read_bytes())
    receipt = {"schema": "cyd-perf-build/1", "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "python": platform.python_version(),
               "source": command(["git", "rev-parse", "HEAD"])["stdout"].strip(),
               "dirty": bool(status["stdout"].strip()),
               "source_dirty": bool(command(["git", "status", "--porcelain", "--", "platformio.ini", "src", "tools/perf/identity.py"])["stdout"].strip()),
               "source_tree_sha256": source.hexdigest(),
               "git_status": status, "pio": command([args.pio, "--version"]), "profiles": {}}
    for profile in args.profiles:
        build = args.build_dir / profile
        artifacts = {}
        for name in ("firmware.bin", "firmware.elf", "firmware.map", "bootloader.bin", "partitions.bin"):
            path = build / name
            if path.is_file(): artifacts[name] = {"bytes": path.stat().st_size, "sha256": sha256(path)}
        receipt["profiles"][profile] = {"artifacts": artifacts,
            "dependencies": command([args.pio, "pkg", "list", "-e", profile])}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(args.out)

if __name__ == "__main__": main()
