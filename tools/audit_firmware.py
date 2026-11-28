#!/usr/bin/env python3
"""Independent byte-level audit of a Diviner firmware delivery (never flashes).

No imports from the packaging/provenance implementation. --repository optionally
anchors the recorded source tree/content snapshot to an existing Git checkout.
The audit covers unsigned ESP32/ESP32-S3 image structure and checksums, not
secure-boot signature verification, device execution or installed-board layout.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import sys


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def git_identity(repository, revision):
    def git(*args, input=None):
        return subprocess.check_output(["git", "-C", str(repository), *args],
                                       input=input, timeout=30)
    tree = git("rev-parse", revision + "^{tree}").decode().strip()
    entries = []
    for row in git("ls-tree", "-r", "-z", revision).split(b"\0"):
        if not row:
            continue
        prefix, path = row.split(b"\t", 1)
        mode, kind, oid = prefix.split()
        require(kind == b"blob", "Source snapshot contains unsupported non-blob entry")
        entries.append((path, mode, oid))
    entries.sort()
    objects = io.BytesIO(git("cat-file", "--batch",
                            input=b"\n".join(item[2] for item in entries) + b"\n"))
    digest = hashlib.sha256()
    for path, mode, oid in entries:
        header = objects.readline().split()
        require(len(header) == 3 and header[0] == oid and header[1] == b"blob",
                "Git source object lookup failed")
        size = int(header[2])
        data = objects.read(size)
        require(len(data) == size and objects.read(1) == b"\n", "Truncated Git object")
        digest.update(path + b"\0")
        if mode == b"120000":
            digest.update(b"symlink\0" + data)
        else:
            require(mode in (b"100644", b"100755"), "Unsupported Git file mode")
            digest.update(b"executable\0" if mode == b"100755" else b"file\0")
            digest.update(hashlib.sha256(data).digest())
    return tree, digest.hexdigest()


def esp_image(data, expected_chip):
    require(len(data) >= 24 and data[0] == 0xe9, "Invalid ESP image header")
    count = data[1]
    require(1 <= count <= 16, "Invalid ESP segment count")
    require(struct.unpack_from("<H", data, 12)[0] == expected_chip, "Wrong image chip ID")
    require(data[23] in (0, 1), "Unsupported ESP image hash flag")
    cursor, checksum, segments = 24, 0xef, []
    for _ in range(count):
        require(cursor + 8 <= len(data), "Truncated ESP segment header")
        address, length = struct.unpack_from("<II", data, cursor)
        cursor += 8
        require(cursor + length <= len(data), "Truncated ESP segment")
        for value in data[cursor:cursor + length]:
            checksum ^= value
        segments.append({"load_address": address, "bytes": length})
        cursor += length
    # ESP pads to the final byte of a 16-byte block, then stores the XOR byte.
    checksum_offset = cursor | 15
    require(checksum_offset < len(data), "Missing ESP image checksum")
    require(data[checksum_offset] == checksum, "ESP segment checksum mismatch")
    end = checksum_offset + 1
    if data[23]:
        require(end + 32 <= len(data), "Missing appended ESP image SHA256")
        require(hashlib.sha256(data[:end]).digest() == data[end:end + 32],
                "Appended ESP image SHA256 mismatch")
        end += 32
    flash_code = data[3] >> 4
    require(flash_code <= 7, "Unsupported ESP image flash-size code")
    return {"segments": segments, "header_flash_bytes": (1 << (20 + flash_code)),
            "xor_checksum": "PASS", "appended_sha256": "PASS" if data[23] else "ABSENT",
            "trailing_bytes": len(data) - end}


def partition_table(data):
    entries, md5_checked = [], False
    for offset in range(0, len(data) - 31, 32):
        row = data[offset:offset + 32]
        marker = struct.unpack_from("<H", row)[0]
        if marker == 0x50aa:
            require(not md5_checked, "Partition after MD5 record")
            kind, subtype, start, size = struct.unpack_from("<BBII", row, 2)
            label = row[12:28].split(b"\0", 1)[0].decode("ascii")
            flags = struct.unpack_from("<I", row, 28)[0]
            entries.append({"type": kind, "subtype": subtype, "offset": start,
                            "size": size, "label": label, "flags": flags})
        elif marker == 0xebeb:
            require(not md5_checked, "Duplicate partition MD5")
            require(hashlib.md5(data[:offset]).digest() == row[16:32],
                    "Partition table MD5 mismatch")
            md5_checked = True
        else:
            require(row == b"\xff" * 32 and data[offset:] == b"\xff" * (len(data) - offset),
                    "Unexpected partition table trailer")
            break
    require(entries and md5_checked, "Missing partition entries or MD5 record")
    return entries


def elf_static_ram(data):
    require(data[:6] == b"\x7fELF\x01\x01", "Expected little-endian ELF32")
    require(struct.unpack_from("<H", data, 18)[0] == 94, "Expected Xtensa ELF")
    start = struct.unpack_from("<I", data, 32)[0]
    stride, count, names_index = struct.unpack_from("<HHH", data, 46)
    require(stride >= 40 and 0 < count < 16384 and names_index < count, "Invalid ELF sections")
    require(start + stride * count <= len(data), "Truncated ELF section table")
    rows = [struct.unpack_from("<10I", data, start + n * stride) for n in range(count)]
    names = rows[names_index]
    require(names[4] + names[5] <= len(data), "Invalid ELF string table")
    strings = data[names[4]:names[4] + names[5]]
    selected = {}
    for row in rows:
        require(row[0] < len(strings), "Invalid ELF section name")
        name = strings[row[0]:].split(b"\0", 1)[0].decode("ascii")
        if name in (".dram0.data", ".dram0.bss", ".noinit"):
            selected[name] = row[5]
    require(".dram0.data" in selected and ".dram0.bss" in selected, "Missing static RAM sections")
    return selected


def audit(args):
    folder = args.directory.resolve()
    require(re.fullmatch("[0-9a-f]{40}", args.source) is not None, "Expected full source SHA")
    require(folder.is_dir(), "Bundle directory missing")
    require(all(p.is_file() and not p.is_symlink() for p in folder.iterdir()),
            "Bundle must contain only regular files")
    output_name = args.output.name if args.output and args.output.resolve().parent == folder else None
    sums, cache = {}, {}
    for line in (folder / "SHA256SUMS.txt").read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([^/\\]+)", line)
        require(match is not None, "Invalid SHA256SUMS record")
        digest, name = match.groups()
        require(name not in (".", "..", "SHA256SUMS.txt") and name not in sums,
                "Unsafe or duplicate checksum entry")
        data = (folder / name).read_bytes()
        require(sha256(data) == digest, "Bundle checksum mismatch: " + name)
        sums[name], cache[name] = digest, data
    actual_names = {p.name for p in folder.iterdir()} - {"SHA256SUMS.txt", output_name}
    require(actual_names == set(sums), "Unlisted or absent bundle file")
    require("manifest.json" in cache and "firmware.bin" in cache, "Required bundle files missing")
    manifest = json.loads(cache["manifest.json"])
    require(manifest["source_sha"] == args.source and manifest["dirty"] is False,
            "Wrong or uncommitted source identity")
    require(manifest["environment"] == args.environment, "Wrong build environment")
    require(manifest.get("flash_performed") is False, "Unexpected flash disposition")
    chip_id = 9 if args.environment == "cardputer" else 0
    require(manifest["chip"] == ("esp32s3" if chip_id else "esp32"), "Wrong profile chip")
    capacity = re.fullmatch(r"(\d+)(MB|KB)", manifest["flash_size"])
    require(capacity is not None, "Unrecognized declared flash size")
    flash_bytes = int(capacity[1]) * (1048576 if capacity[2] == "MB" else 1024)
    source_check = "embedded SHA/tree and manifest"
    if args.repository:
        tree, snapshot = git_identity(args.repository, args.source)
        require(tree == manifest["source_tree"], "Source Git tree mismatch")
        require(snapshot == manifest["source_snapshot"], "Source content snapshot mismatch")
        source_check = "Git commit/tree/content snapshot plus embedded SHA/tree"
    app = cache["firmware.bin"]
    require(args.source.encode() + b"\0" in app, "Source SHA absent from application bytes")
    require(manifest["source_tree"].encode() + b"\0" in app, "Source tree absent from application bytes")
    require(b"diviner_identity source=%s tree=%s profile=%s state=%s" in app,
            "Runtime identity format absent from application")
    records = manifest["images"] + manifest.get("debug_files", [])
    for record in records:
        name = record["name"]
        require(name in cache, "Manifest references missing file")
        require(len(cache[name]) == record["bytes"] and sums[name] == record["sha256"],
                "Manifest size/hash mismatch: " + name)
        require("source_file" not in record, "Build-host path leaked into deliverable")
    parts = partition_table(cache["partitions.bin"])
    require(parts == manifest["partitions"], "Manifest differs from actual partition bytes")
    previous = 0
    for part in sorted(parts, key=lambda p: p["offset"]):
        require(part["size"] > 0 and part["offset"] >= previous, "Partition overlap or empty slot")
        previous = part["offset"] + part["size"]
        require(previous <= flash_bytes, "Partition exceeds declared flash capacity")
    app_parts = [p for p in parts if p["type"] == 0 and p["offset"] == manifest["app_offset"]]
    require(len(app_parts) == 1, "Missing or ambiguous app partition")
    slot = app_parts[0]
    require(len(app) <= slot["size"], "Application exceeds app partition")
    require(manifest["firmware_bytes"] == len(app) and manifest["app_capacity"] == slot["size"] and
            manifest["app_headroom_bytes"] == slot["size"] - len(app), "Incorrect capacity summary")
    images = sorted(manifest["images"], key=lambda p: p["offset"])
    full_info = manifest["merged_image"]
    require(full_info["offset"] == 0 and full_info["name"] in cache, "Invalid full-image origin")
    full = cache[full_info["name"]]
    require(len(full) == full_info["bytes"] and sha256(full) == full_info["sha256"],
            "Full-image metadata mismatch")
    previous, image_results = 0, {}
    for record in images:
        start, data = record["offset"], cache[record["name"]]
        require(start >= previous and start + len(data) <= flash_bytes, "Image overlap or flash overflow")
        require(full[previous:start] == b"\xff" * (start - previous), "Unexpected full-image gap bytes")
        require(full[start:start + len(data)] == data, "Full image differs from component at its offset")
        previous = start + len(data)
        if record["name"] in ("firmware.bin", "bootloader.bin"):
            checked = esp_image(data, chip_id)
            require(checked["header_flash_bytes"] == flash_bytes, "Image header flash capacity mismatch")
            image_results[record["name"]] = checked
    require(previous == len(full), "Full image has unexpected trailing bytes")
    require(next(p["offset"] for p in images if p["name"] == "firmware.bin") == slot["offset"],
            "App image is placed outside its app slot")
    require(next(p["offset"] for p in images if p["name"] == "bootloader.bin") ==
            (0 if chip_id == 9 else 0x1000), "Unexpected bootloader address")
    require(next(p["offset"] for p in images if p["name"] == "partitions.bin") == 0x8000,
            "Unexpected partition-table address")
    static_ram = elf_static_ram(cache["firmware.elf"]) if "firmware.elf" in cache else None
    return {"status": "PASS", "environment": args.environment, "source_sha": args.source,
            "source_tree": manifest["source_tree"], "source_snapshot": manifest["source_snapshot"],
            "source_check": source_check, "checked_files": len(sums),
            "firmware_bytes": len(app), "slot_bytes": slot["size"], "app_offset": slot["offset"],
            "headroom_bytes": slot["size"] - len(app), "flash_bytes": flash_bytes,
            "static_ram_sections": static_ram, "static_ram_bytes": sum(static_ram.values()) if static_ram else None,
            "static_ram_scope": "ELF .dram0.data + .dram0.bss + .noinit; excludes dynamic heap/stack/RTC/PSRAM",
            "app_sha256": sums["firmware.bin"], "full_sha256": sums[full_info["name"]],
            "full_bytes": len(full), "partition_md5": "PASS", "partitions": parts,
            "image_checks": image_results, "platform": manifest["platform"],
            "platform_version": manifest["platform_version"], "packages": manifest["packages"],
            "libraries": manifest["libraries"],
            "physical_execution": "NOT RUN", "secure_boot_signatures": "NOT EVALUATED"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--source", required=True)
    parser.add_argument("--environment", required=True,
                        choices=("cyd", "cyd-diagnostics", "cyd-pcm", "cardputer"))
    parser.add_argument("--repository", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = audit(args)
    except Exception as error:
        report = {"status": "FAIL", "environment": args.environment, "source_sha": args.source,
                  "error": str(error), "physical_execution": "NOT RUN"}
    text = json.dumps(report, sort_keys=True)
    print(text)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n")
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
