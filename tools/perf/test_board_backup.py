"""Offline safety/recovery tests: no serial driver or physical board is used."""
import contextlib
import hashlib
import io
import json
import pathlib
import tempfile
import types
import unittest

from board_backup import BackupError, backup_board, flash_capacity


def fake_apis(*, bad_md5=False, fail_first_connect=False, wrong_chip=False, short_read=False,
              interrupt_read=False, cleanup_failure=False):
    state = {"instances": [], "serials": [], "reads": 0, "stub_calls": 0, "bauds": [], "connects": []}
    data = bytes(range(256)) * 256

    class Serial:
        def __init__(self, **kwargs):
            self.port = kwargs["port"]
            self.baudrate = kwargs["baudrate"]
            self.is_open = False
            self.dtr = self.rts = True
            self.opens = 0
            state["serials"].append(self)

        def open(self):
            assert self.dtr is False and self.rts is False
            self.is_open = True
            self.opens += 1

        def close(self):
            self.is_open = False

    class ESP:
        USES_MAGIC_VALUE = True
        MAGIC_VALUE = 0xF01D83
        IS_STUB = False

        def __init__(self, serial, baud):
            self.serial = serial
            state["instances"].append(self)

        def connect(self, **kwargs):
            assert kwargs == {"mode": "no_reset", "attempts": 1}
            state["connects"].append(self.serial.baudrate)
            print("fake serial sync")
            if fail_first_connect and len(state["instances"]) == 1:
                self.serial.close()  # Matches esptool's failed-connect behavior.
                raise RuntimeError("existing stub may be at fallback baud")

        def read_reg(self, address):
            assert address == 0x40001000
            return 0 if wrong_chip else self.MAGIC_VALUE

        def get_chip_description(self): return "ESP32-D0WD-V3 (revision v3.1)"
        def get_chip_revision(self): return 301
        def get_chip_features(self): return ["WiFi", "BT", "Dual Core"]
        def get_crystal_freq(self): return 40
        def get_flash_encryption_enabled(self): return False
        def get_secure_boot_enabled(self): return False
        def get_encrypted_download_disabled(self): return False

        def run_stub(self):
            state["stub_calls"] += 1
            self.IS_STUB = True
            return self

        def flash_spi_attach(self, value): assert value == 0
        def flash_id(self): return 0x104020  # 64 KiB test fixture, JEDEC code 0x10.
        def flash_set_parameters(self, size): assert size == len(data)

        def change_baud(self, baud):
            state["bauds"].append(baud)
            if baud == 115200 and cleanup_failure:
                raise RuntimeError("cleanup UART request failed")
            self.serial.baudrate = baud

        def read_flash(self, offset, size):
            assert offset == 0 and size == len(data)
            state["reads"] += 1
            if interrupt_read:
                raise KeyboardInterrupt("owner interrupted read")
            return data[:-1] if short_read else data

        def flash_md5sum(self, offset, size):
            return "0" * 32 if bad_md5 else hashlib.md5(data).hexdigest().upper()

        def __getattr__(self, name):
            # An accidental erase/write/reset/eFuse operation fails the test.
            raise AssertionError("Unexpected esptool operation: " + name)

    api = types.SimpleNamespace(__version__="4.14.0", CHIP_DEFS={"esp32": ESP},
                               ESPLoader=types.SimpleNamespace(CHIP_DETECT_MAGIC_REG_ADDR=0x40001000))
    serial = types.SimpleNamespace(Serial=Serial, VERSION="3.5")
    return api, serial, state, data


class BackupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.out = pathlib.Path(self.temp.name) / "new-private-backup"

    def run_backup(self, **options):
        api, serial, state, data = fake_apis(**options)
        with contextlib.redirect_stdout(io.StringIO()):
            receipt = backup_board(port="FAKE-NO-HARDWARE", chip="esp32", out=self.out,
                                   backup_bytes=len(data), esptool_api=api, serial_api=serial)
        return receipt, state, data

    def test_success_hash_verified_and_stub_left_at_known_baud(self):
        receipt, state, data = self.run_backup()
        self.assertTrue(receipt["verified"])
        self.assertTrue(receipt["ram_stub_left_at_115200"])
        self.assertEqual(receipt["backup"]["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(state["bauds"], [460800, 115200])
        self.assertFalse(state["serials"][0].is_open)
        self.assertFalse(receipt["flash_written"] or receipt["flash_erased"] or receipt["efuses_written"])
        operations = receipt["operations"]
        self.assertEqual(operations[1]["stdout"], "fake serial sync\n")
        read = next(item for item in operations if item["operation"] == "esptool.read_flash")
        self.assertEqual(read["result"], {"bytes_received": len(data)})
        self.assertNotIn("data", read["result"])

    def test_wrong_capacity_fails_before_read_and_still_restores_stub_baud(self):
        api, serial, state, data = fake_apis()
        with self.assertRaisesRegex(BackupError, "detected JEDEC capacity"):
            backup_board(port="FAKE", chip="esp32", out=self.out, backup_bytes=2 * len(data), esptool_api=api, serial_api=serial)
        self.assertEqual(state["reads"], 0)
        self.assertEqual(state["bauds"], [115200])
        self.assertFalse((self.out / "original-full-flash.bin").exists())

    def test_md5_failure_preserves_unverified_file_and_error_receipt(self):
        with self.assertRaisesRegex(BackupError, "MD5 verification failed"):
            self.run_backup(bad_md5=True)
        receipt = json.loads((self.out / "backup-receipt.json").read_text())
        self.assertFalse(receipt["verified"])
        self.assertFalse(receipt["completed"])
        self.assertTrue(receipt["ram_stub_left_at_115200"])
        self.assertTrue((self.out / "original-full-flash.bin").exists())

    def test_existing_directory_and_backup_are_not_overwritten(self):
        self.out.mkdir()
        original = self.out / "original-full-flash.bin"
        original.write_bytes(b"owner backup")
        api, serial, state, data = fake_apis()
        with self.assertRaises(FileExistsError):
            backup_board(port="FAKE", chip="esp32", out=self.out, backup_bytes=len(data), esptool_api=api, serial_api=serial)
        self.assertEqual(original.read_bytes(), b"owner backup")
        self.assertEqual(state["serials"], [])

    def test_failed_connect_reopens_port_and_uses_fresh_loader_at_fallback(self):
        receipt, state, _ = self.run_backup(fail_first_connect=True)
        self.assertEqual(state["connects"], [115200, 460800])
        self.assertEqual(len(state["instances"]), 2)
        self.assertEqual(state["serials"][0].opens, 2)
        self.assertEqual(receipt["connected_baud"], 460800)
        self.assertFalse(receipt["operations"][1]["ok"])

    def test_wrong_chip_rejected_before_ram_stub_or_flash_read(self):
        api, serial, state, data = fake_apis(wrong_chip=True)
        with self.assertRaisesRegex(BackupError, "identity mismatch"):
            backup_board(port="FAKE", chip="esp32", out=self.out, backup_bytes=len(data), esptool_api=api, serial_api=serial)
        self.assertEqual(state["stub_calls"], 0)
        self.assertEqual(state["reads"], 0)
        self.assertEqual(state["bauds"], [])

    def test_short_flash_read_never_claims_verification(self):
        with self.assertRaisesRegex(BackupError, "Short flash read"):
            self.run_backup(short_read=True)
        receipt = json.loads((self.out / "backup-receipt.json").read_text())
        self.assertFalse(receipt["verified"])
        self.assertFalse((self.out / "original-full-flash.bin").exists())

    def test_unknown_capacity_is_not_extrapolated(self):
        with self.assertRaises(BackupError):
            flash_capacity(0xFF4020)
        self.assertEqual(flash_capacity(0x164020), 0x400000)

    def test_interrupt_is_retained_as_failure_and_serial_is_closed(self):
        api, serial, state, data = fake_apis(interrupt_read=True)
        with self.assertRaisesRegex(BackupError, "owner interrupted read"):
            backup_board(port="FAKE", chip="esp32", out=self.out, backup_bytes=len(data), esptool_api=api, serial_api=serial)
        receipt = json.loads((self.out / "backup-receipt.json").read_text())
        self.assertEqual(receipt["error"]["type"], "KeyboardInterrupt")
        self.assertFalse(receipt["completed"])
        self.assertFalse(receipt["verified"])
        self.assertTrue(receipt["ram_stub_left_at_115200"])
        self.assertFalse(state["serials"][0].is_open)

    def test_cleanup_failure_does_not_claim_known_final_stub_state(self):
        api, serial, state, data = fake_apis(cleanup_failure=True)
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(BackupError, "cleanup UART request failed"):
            backup_board(port="FAKE", chip="esp32", out=self.out, backup_bytes=len(data), esptool_api=api, serial_api=serial)
        receipt = json.loads((self.out / "backup-receipt.json").read_text())
        self.assertTrue(receipt["verified"])  # The file remains a verified backup.
        self.assertFalse(receipt["completed"])  # Recovery, however, failed.
        self.assertFalse(receipt["ram_stub_left_at_115200"])
        self.assertFalse(state["serials"][0].is_open)

    def test_unpinned_esptool_rejected_before_serial_open(self):
        api, serial, state, data = fake_apis()
        api.__version__ = "5.0.0"
        with self.assertRaisesRegex(BackupError, "Require esptool==4.14.0"):
            backup_board(port="FAKE", chip="esp32", out=self.out, backup_bytes=len(data), esptool_api=api, serial_api=serial)
        self.assertEqual(state["serials"], [])

    def test_c3_identity_uses_chip_id_instead_of_esp32_magic_register(self):
        api, serial, state, data = fake_apis()
        loader = api.CHIP_DEFS["esp32"]
        loader.USES_MAGIC_VALUE = False
        loader.IMAGE_CHIP_ID = 5
        loader.get_chip_id = lambda self: 5
        loader.get_chip_description = lambda self: "ESP32-C3 (FAKE)"
        loader.read_reg = lambda self, address: self.fail("ESP32 magic read must not run")
        api.CHIP_DEFS["esp32c3"] = loader
        with contextlib.redirect_stdout(io.StringIO()):
            receipt = backup_board(port="FAKE", chip="esp32c3", out=self.out, backup_bytes=len(data), esptool_api=api, serial_api=serial)
        self.assertTrue(receipt["chip_identity_verified"])
        self.assertTrue(receipt["verified"])
        self.assertTrue(any(item["operation"] == "esptool.get_chip_id" for item in receipt["operations"]))


if __name__ == "__main__":
    unittest.main()
