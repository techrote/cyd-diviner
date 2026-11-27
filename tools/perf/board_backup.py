"""Explicit read-only esptool inventory and verified full-flash backup.

Backups contain NVS/credentials. Keep --out outside the repository. This tool
never writes, erases, burns eFuses or chooses firmware. Enter download mode
manually first if automatic reset does not work on the board.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import subprocess
import sys

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--chip", choices=["esp32", "esp32s3", "esp32c3"], required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--backup-bytes", type=lambda s: int(s, 0), help="Must match detected physical flash size; omit for inventory only")
    parser.add_argument("--baud", type=int, default=460800)
    args = parser.parse_args()
    out = args.out.resolve()
    root = pathlib.Path(__file__).resolve().parents[2]
    if out == root or root in out.parents:
        parser.error("Full flash/NVS backups must stay outside the repository")
    out.mkdir(parents=True, exist_ok=True)
    prefix = [sys.executable, "-m", "esptool", "--chip", args.chip, "--port", args.port,
              "--baud", str(args.baud), "--before", "no_reset", "--after", "no_reset"]
    receipt = {"schema": "cyd-perf-backup/1", "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "port": args.port, "chip_requested": args.chip, "commands": [], "flash_written": False}
    def run(tail):
        cmd = prefix + tail
        result = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
        receipt["commands"].append({"argv": cmd, "returncode": result.returncode,
                                    "stdout": result.stdout, "stderr": result.stderr})
        (out / "backup-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        print(result.stdout, end="", flush=True)
        if result.returncode:
            print(result.stderr, file=sys.stderr)
            raise SystemExit(result.returncode)
        return result.stdout
    inventory = run(["flash_id"])
    run(["get_security_info"])
    if args.backup_bytes:
        import re
        match = re.search(r"Detected flash size:\s*(\d+)\s*(KB|MB)", inventory)
        detected = int(match[1]) * (1024 if match[2] == "KB" else 1024 * 1024) if match else None
        if detected != args.backup_bytes:
            raise SystemExit("Requested backup size does not match detected flash size; no backup attempted")
        backup = out / "original-full-flash.bin"
        if backup.exists(): raise SystemExit("Backup file already exists; choose a new --out directory")
        run(["read_flash", "0", str(args.backup_bytes), str(backup)])
        receipt["backup"] = {"bytes": backup.stat().st_size, "sha256": hashlib.sha256(backup.read_bytes()).hexdigest()}
        run(["verify_flash", "0", str(backup)])
        receipt["verified"] = True
        (out / "backup-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        print("Verified backup SHA256:", receipt["backup"]["sha256"])

if __name__ == "__main__": main()
