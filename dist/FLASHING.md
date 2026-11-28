# Flashing the Daedric Grimoire — M5Stack Cardputer ADV (ESP32-S3)

Two images are provided. Most people want the **full image**.

## Full image (clean install) — `daedric-grimoire-cardputer-adv.bin`
A merged factory image: bootloader + partition table + app. Flash it to a
blank or factory Cardputer ADV at offset **0x0**. Nothing else is required.

Using esptool (install with `pip install esptool`):

    esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
      write-flash 0x0 daedric-grimoire-cardputer-adv.bin

- Port is `/dev/ttyACM0` on Linux, `COMx` on Windows, `/dev/cu.usbmodem*` on macOS.
- If the port doesn't appear, hold **G0** while tapping **reset** to enter
  download mode, then run the command.
- You can also drag this file into https://espressif.github.io/esptool-js/
  (Web Serial, Chrome/Edge) and flash at offset 0.
- Back up first if you care about what's on the device:
  `esptool --chip esp32s3 --port /dev/ttyACM0 read-flash 0 0x800000 backup.bin`

## App-only image — `daedric-grimoire.bin` (about 1.66 MB)
Just the application, for updating a device that already runs the grimoire.

**Standalone install** (the full image above, or any stock 8 MB layout with
the app at 0x10000) — update the app and keep your saved Wi-Fi networks,
sequencer patterns and other settings:

    esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
      write-flash 0x10000 daedric-grimoire.bin

**Under the Launcher** — install it from the Launcher (SD card or its USB
serial installer) so the Launcher sizes the partition. Do **not** write it
over an older Launcher slot by offset: since November 2026 the app is larger
than the old 1600 KB `grimoi` slot, and writing past the end of a slot
corrupts whatever follows it.

## Verify
    sha256sum -c SHA256SUMS.txt

Board: ESP32-S3, 8 MB flash, USB-CDC on boot. Source and build instructions:
https://github.com/numbpill3d/cardputer-adv-diviner
