"""Read-only inventory and verified full-flash backup using one esptool session.

Enter download mode manually. Only a RAM stub is uploaded; flash and eFuses are
never written or erased. Leave the RAM stub at 115200 without resetting the app.
Backups include NVS/credentials: --out must be NEW and outside the repository.
"""
from __future__ import annotations
import argparse
import contextlib
import datetime
import hashlib
import io
import json
import os
import pathlib
import sys
import time

ESPTOOL_VERSION = "4.14.0"
REPOSITORY = pathlib.Path(__file__).resolve().parents[2]


class BackupError(RuntimeError):
    pass


def flash_capacity(jedec):
    code = (jedec >> 16) & 255
    if not 0x10 <= code <= 0x1A:
        raise BackupError(f"Unsupported JEDEC capacity byte 0x{code:02x}; no flash read attempted")
    return 1 << code


def backup_board(*, port, chip, out, backup_bytes=None, baud=460800, esptool_api=None, serial_api=None):
    """Inject fake APIs for offline tests. This function has no flashing path."""
    out = pathlib.Path(out).resolve()
    if out == REPOSITORY or REPOSITORY in out.parents:
        raise BackupError("Full flash/NVS backups must stay outside the repository")
    if (backup_bytes is not None and backup_bytes <= 0) or baud <= 0:
        raise BackupError("Backup size and baud must be positive")
    out.mkdir(parents=True, exist_ok=False)
    receipt = {"schema": "cyd-perf-backup/2", "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "port": port, "chip_requested": chip, "requested_backup_bytes": backup_bytes,
               "requested_transfer_baud": baud, "python": sys.version, "operations": [],
               "flash_written": False, "flash_erased": False, "efuses_written": False,
               "verified": False, "ram_stub_left_at_115200": False,
               "exit_policy": "keep_RAM_stub_running_no_reset_no_app_start"}

    def save():
        temporary = out / "backup-receipt.json.tmp"
        temporary.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        temporary.replace(out / "backup-receipt.json")

    def call(operation, arguments, function, summarize=None):
        stdout, stderr = io.StringIO(), io.StringIO()
        record = {"operation": operation, "arguments": arguments,
                  "utc": datetime.datetime.now(datetime.timezone.utc).isoformat()}
        receipt["operations"].append(record)
        started = time.perf_counter()
        try:
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                result = function()
            record["ok"] = True
            if summarize is not None:
                record["result"] = summarize(result)
            elif result is None or isinstance(result, (str, bool, int, float, list, dict)):
                record["result"] = result
            return result
        except BaseException as error:
            record.update(ok=False, error_type=type(error).__name__, error=str(error))
            raise
        finally:
            record.update(stdout=stdout.getvalue(), stderr=stderr.getvalue(), elapsed_seconds=time.perf_counter() - started)
            save()
            print(record["stdout"], end="", flush=True)
            print(record["stderr"], end="", file=sys.stderr, flush=True)

    transport = esp = None
    connected = False
    failure = None
    save()
    try:
        if esptool_api is None:
            import esptool as esptool_api
        if serial_api is None:
            import serial as serial_api
        receipt.update(esptool_version=esptool_api.__version__, pyserial_version=getattr(serial_api, "VERSION", "unknown"))
        if esptool_api.__version__ != ESPTOOL_VERSION:
            raise BackupError(f"Require esptool=={ESPTOOL_VERSION}; install tools/perf/backup-requirements.txt")
        requested_class = esptool_api.CHIP_DEFS[chip]

        def open_port():
            serial = serial_api.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5)
            serial.dtr = serial.rts = False
            serial.port = port
            serial.open()
            return serial
        transport = call("serial.open", {"port": port, "baud": 115200, "dtr": False, "rts": False}, open_port)
        bauds = list(dict.fromkeys((115200, baud)))
        for index, connect_baud in enumerate(bauds):
            def connect():
                transport.baudrate = connect_baud
                if not transport.is_open:
                    transport.dtr = transport.rts = False
                    transport.open()
                # connect() closes the serial port on failure: each fallback
                # reopens it and constructs a fresh requested ROM loader.
                loader = requested_class(transport, connect_baud)
                loader.connect(mode="no_reset", attempts=1)
                return loader
            try:
                esp = call("esptool.connect", {"chip": chip, "baud": connect_baud, "mode": "no_reset", "attempts": 1}, connect)
                connected = True
                receipt["connected_baud"] = connect_baud
                break
            except Exception:
                if index == len(bauds) - 1:
                    raise
        # Verify silicon independently of the class selected by --chip.
        if requested_class.USES_MAGIC_VALUE:
            address = esptool_api.ESPLoader.CHIP_DETECT_MAGIC_REG_ADDR
            identity = call("esptool.read_reg", {"address": address, "purpose": "chip_identity_magic"}, lambda: esp.read_reg(address))
            expected = requested_class.MAGIC_VALUE
        else:
            identity = call("esptool.get_chip_id", {}, esp.get_chip_id)
            expected = requested_class.IMAGE_CHIP_ID
        if identity != expected:
            raise BackupError(f"Requested {chip} identity mismatch; no stub or flash read attempted")
        receipt["chip_identity_verified"] = True
        for key, method in (("chip_description", "get_chip_description"), ("chip_revision", "get_chip_revision"),
                            ("chip_features", "get_chip_features"), ("crystal_mhz", "get_crystal_freq")):
            receipt[key] = call("esptool." + method, {}, getattr(esp, method))
        # Classic ESP32 has no GET_SECURITY_INFO command. Read only named
        # non-key flags through target methods, never an espefuse dump/summary.
        receipt["security"] = {}
        for name in ("get_flash_encryption_enabled", "get_secure_boot_enabled", "get_encrypted_download_disabled"):
            method = getattr(esp, name, None)
            receipt["security"][name.removeprefix("get_")] = bool(call("esptool." + name, {}, method)) if method else None
        esp = call("esptool.run_stub", {"destination": "RAM_only"}, esp.run_stub)
        receipt["ram_stub_loaded"] = True
        call("esptool.flash_spi_attach", {"hspi_arg": 0}, lambda: esp.flash_spi_attach(0))
        jedec = call("esptool.flash_id", {}, esp.flash_id)
        capacity = flash_capacity(jedec)
        receipt.update(jedec_hex=f"0x{jedec:06x}", flash_bytes=capacity,
                       manufacturer_hex=f"0x{jedec & 255:02x}", memory_type_hex=f"0x{(jedec >> 8) & 255:02x}")
        if backup_bytes is not None and backup_bytes != capacity:
            raise BackupError(f"Requested backup size {backup_bytes} differs from detected JEDEC capacity {capacity}; no flash read attempted")
        call("esptool.flash_set_parameters", {"size": capacity, "effect": "RAM_geometry_only"}, lambda: esp.flash_set_parameters(capacity))
        if backup_bytes is not None:
            if transport.baudrate != baud:
                call("esptool.change_baud", {"baud": baud}, lambda: esp.change_baud(baud))
            data = call("esptool.read_flash", {"offset": 0, "length": capacity}, lambda: esp.read_flash(0, capacity), lambda value: {"bytes_received": len(value)})
            if len(data) != capacity:
                raise BackupError(f"Short flash read: expected {capacity}, received {len(data)} bytes")
            backup = out / "original-full-flash.bin"
            with backup.open("xb") as file:
                file.write(data)
                file.flush()
                os.fsync(file.fileno())
            disk_bytes = backup.read_bytes()
            if len(disk_bytes) != capacity:
                raise BackupError("Stored backup length differs from detected flash capacity")
            receipt["backup"] = {"path": str(backup), "bytes": len(disk_bytes),
                                 "sha256": hashlib.sha256(disk_bytes).hexdigest(), "md5": hashlib.md5(disk_bytes).hexdigest()}
            target_md5 = call("esptool.flash_md5sum", {"offset": 0, "length": capacity}, lambda: esp.flash_md5sum(0, capacity))
            receipt["target_md5"] = target_md5
            if target_md5.lower() != receipt["backup"]["md5"]:
                raise BackupError("Independent target MD5 verification failed; retained backup is UNVERIFIED")
            receipt["verified"] = True
            print("Verified backup SHA256:", receipt["backup"]["sha256"], flush=True)
    except BaseException as error:
        failure = error
        receipt["error"] = {"type": type(error).__name__, "message": str(error)}
    finally:
        if connected and esp is not None and receipt.get("chip_identity_verified"):
            try:
                call("esptool.change_baud", {"baud": 115200, "purpose": "leave_known_stub_baud"}, lambda: esp.change_baud(115200))
                # Deliberately keep the stub. soft_reset(True) can require a
                # fresh physical BOOT intervention on this CYD.
                receipt["ram_stub_left_at_115200"] = bool(getattr(esp, "IS_STUB", False))
            except Exception as error:
                receipt["cleanup_error"] = {"type": type(error).__name__, "message": str(error)}
                failure = failure or error
        if transport is not None:
            try:
                call("serial.close", {"dtr": False, "rts": False, "reset": False}, transport.close)
            except Exception as error:
                receipt["close_error"] = {"type": type(error).__name__, "message": str(error)}
                failure = failure or error
        receipt["completed"] = failure is None
        save()
    if failure is not None:
        raise BackupError(f"{failure}; retained receipt: {out / 'backup-receipt.json'}") from failure
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--chip", choices=["esp32", "esp32s3", "esp32c3"], required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--backup-bytes", type=lambda value: int(value, 0), help="Must match JEDEC capacity; omit for inventory only")
    parser.add_argument("--baud", type=int, default=460800, help="Transfer/fallback stub speed; initial connection is always 115200")
    args = parser.parse_args()
    try:
        backup_board(port=args.port, chip=args.chip, out=args.out, backup_bytes=args.backup_bytes, baud=args.baud)
    except (BackupError, FileExistsError, ModuleNotFoundError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
