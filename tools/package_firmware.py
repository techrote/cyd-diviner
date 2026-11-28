#!/usr/bin/env python3
"""Transactionally package a verified current-source build and actual partition map."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile
from source_provenance import capture


def digest(data):
    return hashlib.sha256(data).hexdigest()


def partitions(data):
    result = []
    for start in range(0, len(data) - 31, 32):
        magic, kind, subtype, offset, size, label, flags = struct.unpack_from("<HBBII16sI", data, start)
        if magic != 0x50AA:
            break
        result.append({"type": kind, "subtype": subtype, "offset": offset, "size": size,
                       "label": label.rstrip(b"\0").decode("ascii"), "flags": flags})
    if not result:
        raise ValueError("No valid ESP partition entries")
    return result


def flash_bytes(value):
    match = re.fullmatch(r"(\d+)(KB|MB)", str(value).upper())
    if not match:
        raise ValueError("Declared flash capacity must be explicit KB or MB")
    return int(match[1]) * (1024 if match[2] == "KB" else 1024 * 1024)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("environment", choices=["cardputer", "cyd", "cyd-diagnostics", "cyd-pcm"])
    parser.add_argument("--output", type=Path, default=Path("delivery"))
    parser.add_argument("--allow-dirty", action="store_true", help="Development packages only; never CI qualification")
    args = parser.parse_args()
    build = Path(".pio/build") / args.environment
    info = json.loads((build / "flash-layout.json").read_text())
    current = capture(Path.cwd())
    if info.get("environment") != args.environment:
        raise ValueError("Build environment mismatch")
    if any(info.get(k) != v for k, v in current.items()):
        raise ValueError("Stale source identity, tree, or contents: rebuild before delivery")
    if current["dirty"] and not args.allow_dirty:
        raise ValueError("Uncommitted or untracked source: only clean builds can be delivered")
    expected_chip = "esp32s3" if args.environment == "cardputer" else "esp32"
    if info.get("chip") != expected_chip:
        raise ValueError("Wrong chip for requested environment")
    capacity = flash_bytes(info["flash_size"])
    out = args.output / args.environment
    if out.exists() and any(out.iterdir()):
        raise ValueError("Destination is not empty; use a new delivery directory")
    # Read and validate everything before touching any prior output. Copy the
    # verified bytes themselves, not a second unchecked read of a source path.
    files = {}
    occupied = []
    end = 0
    table = None
    app = None
    full_name = args.environment + "-full.bin"
    for group in ("images", "debug_files"):
        clean = []
        for original in info.get(group, []):
            image = dict(original)
            name = image["name"]
            if Path(name).name != name or name in files or name in ("manifest.json", "README.md", "SHA256SUMS.txt", full_name):
                raise ValueError("Unsafe or duplicate artifact filename")
            data = Path(image.pop("source_file")).read_bytes()
            if len(data) != image["bytes"] or digest(data) != image["sha256"]:
                raise ValueError("A build input changed after metadata was recorded")
            files[name] = data
            if group == "images":
                lo, hi = image["offset"], image["offset"] + len(data)
                if lo < 0 or hi > capacity or any(lo < b and hi > a for a, b, _ in occupied):
                    raise ValueError("Invalid, overlapping or out-of-flash image layout")
                occupied.append((lo, hi, data)); end = max(end, hi)
                if name == "partitions.bin": table = partitions(data)
                if lo == info["app_offset"]: app = data
            clean.append(image)
        info[group] = clean
    ranges = []
    for p in table or []:
        lo, hi = p["offset"], p["offset"] + p["size"]
        if p["size"] <= 0 or lo < 0 or hi > capacity or any(lo < b and hi > a for a, b in ranges):
            raise ValueError("Partition table exceeds flash or contains overlapping slots")
        ranges.append((lo, hi))
    slot = next((p for p in (table or []) if p["type"] == 0 and p["offset"] == info["app_offset"]), None)
    if not slot or app is None or len(app) > slot["size"]:
        raise ValueError("Firmware does not fit the app slot in the delivered table")
    if not any(p[0] < 0x8000 for p in occupied) or not table:
        raise ValueError("A full image requires bootloader and partition table")
    full = bytearray(b"\xff" * end)
    for lo, hi, data in occupied: full[lo:hi] = data
    files[full_name] = full
    info.update(partitions=table, firmware_bytes=len(app), app_capacity=slot["size"],
                app_headroom_bytes=slot["size"] - len(app),
                merged_image={"name": full_name, "offset": 0, "bytes": len(full), "sha256": digest(full)},
                qualification="BUILD ONLY; physical operation NOT RUN", flash_performed=False)
    files["manifest.json"] = (json.dumps(info, indent=2) + "\n").encode()
    files["README.md"] = f"""# {args.environment} — Draft/HOLD

Source: `{current['source_sha']}`. App: {len(app):,} bytes. App slot:
`{slot['label']}` at `0x{slot['offset']:x}`, {slot['size']:,} bytes;
headroom {slot['size']-len(app):,} bytes. Flash capacity: {capacity:,} bytes.

No physical device was flashed or qualified by this build. Review the repository's
`docs/convergence/OWNER-EVALUATION.md` before installation. Preserve a full backup,
identify the board and existing partition table, and verify SHA256SUMS.txt.

`firmware.bin` is app-only. Install it only at an offset verified against the
target's actual table, and only if the slot fits. The old Cardputer Launcher
1600 KiB slot must not be assumed sufficient.

`{full_name}` starts at zero and includes this build's bootloader, partition
table, OTA bootstrap where configured, and app. It replaces the layout within
that written range. It is not a backup of NVS, Launcher, other apps or filesystem.
It does not preserve unspecified contents. No automatic flashing script is supplied.

All offsets, packages, partition entries and image hashes are in manifest.json.
Physical sound, radio interoperability and real-time headroom remain owner tests.
""".encode()
    files["SHA256SUMS.txt"] = "".join(f"{digest(data)}  {name}\n" for name, data in sorted(files.items())).encode()
    # Do not expose a partial bundle if any validation or write fails.
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".diviner-package-", dir=args.output) as temporary:
        stage = Path(temporary) / args.environment
        stage.mkdir()
        for name, data in files.items(): (stage / name).write_bytes(data)
        if out.exists(): out.rmdir()  # only a previously verified empty directory
        stage.replace(out)
    print(json.dumps({"environment": args.environment, "source_sha": current["source_sha"],
                      "firmware_bytes": len(app), "slot_bytes": slot["size"], "output": str(out)}))


if __name__ == "__main__":
    main()
