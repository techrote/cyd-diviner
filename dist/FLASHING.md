# Historical bundled Cardputer ADV images

> **Historical reference only.** These binaries are not the PR #8 convergence
> candidate and must not be substituted for its exact-source CI artifacts.
> Candidate installation starts from the owner-evaluation procedure and the
> current CI bundle, not from the commands on this historical page.

These two historical images came from the upstream Daedric Grimoire project.
They target the M5Stack Cardputer ADV with an ESP32-S3, 8 MB flash and USB CDC
at boot. [Upstream Cardputer ADV project](https://github.com/numbpill3d/cardputer-adv-diviner)

See the [project README](../README.md) to build current firmware from source.
For an ESP32 CYD, follow the [CYD quick start](../README.md#cyd-quick-start) or
the [USB PCM guide](../README.md#usb-pcm-into-vlc-on-windows) for audio over USB.

## Full image

`daedric-grimoire-cardputer-adv.bin` combines the bootloader, partition table
and application for a clean installation at offset `0x0`. Install esptool with
`pip install esptool` and run this command from `dist/`.

```sh
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write-flash 0x0 daedric-grimoire-cardputer-adv.bin
```

- Replace `/dev/ttyACM0` with your port. Typical ports are `COMx` on Windows
  and `/dev/cu.usbmodem*` on macOS.
- If the port does not appear, hold **G0** while tapping **Reset** to enter
  download mode, then run the command.
- To flash from Chrome or Edge, load the image at offset `0x0` in
  [Espressif's Web Serial flasher](https://espressif.github.io/esptool-js/).

## App-only image

`daedric-grimoire.bin` is the historical upstream app from the integrated
`c1b1175` lineage. It is **not** a qualified binary for this convergence candidate.
It already exceeds the old 1600 KiB Launcher `grimoi` slot. Do not use a hard-coded
`0x170000` update instruction or assume that the previous Launcher layout fits.

Use the current CI artifact's `firmware.bin` only with an independently verified
application offset and sufficient partition capacity. Its standalone full image
has a different partition table; review
[owner evaluation](../docs/convergence/OWNER-EVALUATION.md) before installation.

## Verify the images

Run the checksum command from `dist/`. [SHA256SUMS.txt](SHA256SUMS.txt)

```sh
sha256sum -c SHA256SUMS.txt
```
