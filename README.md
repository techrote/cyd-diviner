# Diviner

Diviner is a pocket synthesiser, visual instrument, and radio companion in a
Daedric and Lain-inspired style. Its apps, called **rites**, share a carousel,
a hand-rasterised Daedric font, and a synth engine. This fork adds standalone
touch firmware for the **ESP32-2432S028R CYD** and optional USB serial audio
for VLC on Windows.

[CYD quick start](#cyd-quick-start) · [USB PCM into VLC](#usb-pcm-into-vlc-on-windows) ·
[Cardputer ADV](#cardputer-adv) · [Desktop simulator](#desktop-simulator)

<p align="center">
  <img src="docs/previews/splash.png" width="47%" alt="Diviner splash in the simulator">
  <img src="docs/previews/menu.png" width="47%" alt="Diviner rite carousel in the simulator">
</p>

## Build targets

| PlatformIO target | Hardware or runtime | Use |
| --- | --- | --- |
| `cyd` | Classic ESP32-2432S028R with 4 MB flash, an ILI9341 LCD, and XPT2046 resistive touch | Standalone landscape touchscreen firmware with 14 rites |
| `cyd-pcm` | The same CYD, with the Windows VLC plugin on the host | Touchscreen firmware with live USB serial audio |
| `cyd-diagnostics` | The same CYD | Serial touch injection, display readback, and hardware diagnostics |
| `cardputer` | M5Stack Cardputer ADV with the Daedric Launcher | Original keyboard workflow with 16 rites. This is PlatformIO's default target |
| `native` | Desktop with SDL2 development libraries and a POSIX-compatible C++ toolchain | Simulator with desktop audio, WAV export, and simulated radio data |

## CYD quick start

Use the ESP32-2432S028R revision with an ILI9341 display and XPT2046 touch
controller. PlatformIO builds the firmware and writes it over USB serial.
Substitute your device's port for the Windows example `COM21`.

```sh
pio run -e cyd -t upload --upload-port COM21
pio device monitor -e cyd --port COM21
```

Close the serial monitor or VLC stream before uploading. If the uploader reports
`Wrong boot mode`, hold **BOOT**, tap **RESET/EN**, and keep holding **BOOT**
until the uploader connects. Diviner starts directly after reset. The builds
for the CYD come from source. The bundled binaries in [`dist/`](dist/FLASHING.md)
are for the Cardputer ADV.

### Touch and sound

- Tap the centre card to open a rite. Tap either side of the carousel to browse.
- Use the labelled controls below each rite. **Menu** or the board's **BOOT**
  button takes you back to the carousel while the firmware is running.
- In **seq**, tap the grid to edit steps and the A–D buttons to select banks.
  **More** opens tempo and pattern controls.
- In **beat**, tap **Edit** to bring up the keyboard and **Hide** to see the
  whole view again. Wi-Fi and Gemini text prompts open the keyboard as needed.
  **Symbols** switches the keyboard layout.
- The **Volume** control adjusts the mix. Local audio uses the GPIO26 DAC and
  the two-pin speaker output. The USB stream takes the digital mix before the
  DAC and works with the speaker connector removed.

Wi-Fi runs on 2.4 GHz. Use **wifi** to join a network before opening a rite
that needs a connection. **Pulse** waits for CSI frames from the connected
access point. The CYD's Bluetooth scan tracks up to 32 recent devices.

## USB PCM into VLC on Windows

Use `cyd-pcm` to listen to the CYD over its existing USB serial connection.
The plugin was tested with VLC 3.0.24 on Windows x64. It opens a live source
such as `cydpcm://COM21` and feeds the synth mix to VLC's normal audio output.

With PlatformIO and VLC 3.0.24 (Windows x64) installed, run the following
commands from the repository root. Close any program using the CYD's COM port
before uploading.

```powershell
pio run -e cyd-pcm -t upload --upload-port COM21
.\tools\vlc\build.ps1
.\tools\vlc\open.ps1 -Port COM21
```

Open **drone**, **seq**, or **beat** on the CYD to hear the synth. The CYD's
blue LED indicates an active USB stream. Close VLC playback to release the
port for a monitor or another upload.

The build script uses PowerShell, `curl.exe`, and `tar.exe` to fetch and
verify the VLC SDK and LLVM-MinGW. It writes `libcydpcm_plugin.dll` to
`.pio/vlc-plugin/`. The launcher loads the DLL into the VLC instance it
starts and defaults to 150 ms of live caching. Use `-CachingMs` to adjust the
buffer and `-Port` for a different serial port. Each attached board can have
its own VLC instance and input. The boards run on independent clocks.

If you need a matching portable copy of VLC, stage one and point the launcher
at it.

```powershell
.\tools\vlc\build.ps1 -StagePortable
.\tools\vlc\open.ps1 -Port COM21 -VlcPath .\.pio\vlc-sdk\vlc-3.0.24\vlc.exe
```

The firmware and plugin use **533,333 baud, 8N1**. That rate was tested with
the CYD's CH340 on Windows. A different USB bridge or driver may handle it
differently. If playback breaks up, open **Tools → Messages** in VLC and set
verbosity to **2**. When the input closes, it reports CRC errors, missing
frames, and timing discontinuities.

### The VLC module and wire format

[`tools/vlc/cydpcm.c`](tools/vlc/cydpcm.c) is a native VLC input module. It
opens the COM port exclusively, verifies the incoming frames, and passes
timestamped **24 kHz signed 16-bit little-endian mono PCM** to VLC. Short gaps
are filled with silence. Larger gaps reset the stream clock.

The [shared protocol header](src/cyd_pcm_protocol.h) specifies the baud rate,
commands, and frame layout. The host sends `pcm start\n` to begin a stream,
`pcm ping\n` once a second, and `pcm stop\n` to close it. The CYD times
out after four seconds without a heartbeat. A frame contains the four-byte
marker `CYD1`, a little-endian 32-bit sequence number, 128 signed 16-bit
little-endian PCM samples, and a little-endian CRC-16/CCITT-FALSE covering
the sequence number and samples.

A port to another board can reuse the VLC module by implementing this protocol
in that board's audio and USB backend. The current USB sender is in the CYD
backend.

## Cardputer ADV

Build `cardputer`, then copy `.pio/build/cardputer/firmware.bin` to the SD
card and install it with the [Daedric Launcher](https://github.com/bmorcelli/Launcher).
See the [Cardputer flashing guide](dist/FLASHING.md) for the bundled upstream
images and their offsets.

```sh
pio run -e cardputer
```

On Cardputer, **G0** or the backtick key leaves a rite. Press Back twice in
the carousel to return to the Launcher. **Fn** with **-** or **=** changes
volume.

## Desktop simulator

```sh
pio run -e native
.pio/build/native/program
```

The simulator opens the 240×135 canvas at 3× by default. Use arrow keys for
navigation, **Alt** for **Fn**, and **Esc** for Back. **Left Ctrl + R** or
**Left Ctrl + L** rotates the display. **Left Ctrl + 1–6** changes the scale.
Use Left Ctrl on its own for these shortcuts.

Set `GRIM_WAV=out.wav` to write the synth stream to a file, or `GRIM_SCRIPT`
to drive the simulator. The [simulator HAL](src/hal_native.cpp) describes the
script actions. `tools/shots.sh shots` generates screenshots and WAV samples.

## Rites

The carousel shows each rite in Daedric script and reveals its name on selection. [Carousel animation](docs/previews/menu-reveal.gif)
The first 14 entries below are available on CYD and Cardputer. **Banish** and
**Probe** are included in the Cardputer build and have simulator views.

| Rite | What it does |
| --- | --- |
| [drone](docs/previews/drone.png) | Five drifting synth voices with resonant filtering, wind, bells, and delay |
| [seq](docs/previews/seq.png) | A 16-step kick, snare, hat, and acid groovebox with four pattern banks, Euclidean fills, song mode, and saved patterns on hardware |
| [beat](docs/previews/beat.png) | Bytebeat livecoding with expressions in `t` |
| [wired](docs/previews/wired-navi.png) | ASCII 3D torus, navi sphere, wireframe solids, and tunnel |
| [visions](docs/previews/vis-plasma.png) | Plasma, gate, ash-fire, glyph rain, and kaleidoscope |
| [oracle](docs/previews/oracle.png) | Three-rune casts with bell chimes |
| [wifi](docs/previews/link.png) | Wi-Fi join and saved networks, with detection and click-through support for simple captive portals. The source calls it Link |
| [scry](docs/previews/scry.png) | Nearby Wi-Fi networks as a constellation |
| [listen](docs/previews/listen.png) | Receive-only Wi-Fi channel monitor with sound |
| [kin](docs/previews/kin.png) | Nearby Bluetooth LE devices as drifting familiars |
| [net](docs/previews/net.png) | Local network inventory |
| [gemini](docs/previews/gemini.png) | Gemtext browsing with text prompts and bookmarks. The current TLS client accepts unverified certificates |
| [sentinel](docs/previews/sentinel.png) | Learns a Wi-Fi or Bluetooth baseline and alerts on new devices |
| [pulse](docs/previews/pulse.png) | Experimental motion and presence visualisation from Wi-Fi CSI |
| [banish](docs/previews/banish.png) | Cardputer IR remote rite. Actual transmission depends on the emitter and pin configuration |
| [probe](docs/previews/probe.png) | Cardputer host checks, including automated credential checks |

Use the radio and IR tools on networks and devices you own or have permission
to test. [Usage notice](NOTICE.md)

## Development and verification

- [`src/hal.h`](src/hal.h) is the platform seam. The Cardputer, CYD, and SDL
  backends live in `hal_device.cpp`, `hal_cyd.cpp`, and `hal_native.cpp`.
  [`src/cyd_controls.cpp`](src/cyd_controls.cpp) provides the CYD touch dock
  and keyboard.
- [`src/apps/`](src/apps) holds the rites. The ESP32 and simulated radio
  implementations are in [`src/net_device.cpp`](src/net_device.cpp) and
  [`src/net_native.cpp`](src/net_native.cpp).
- [`src/cyd_pcm_serial.cpp`](src/cyd_pcm_serial.cpp) sends frame data outside
  the audio callback. [`tools/vlc/`](tools/vlc) contains the Windows input
  module, build script, and launcher.
- At 115200 baud, `cyd-diagnostics` accepts `status` for heap, display time
  and audio counters, `tap X Y` for injected touch, and `shot` for 320×240
  little-endian RGB565 LCD readback.
- To generate a compilation database for clangd, run
  `pio run -e native -t compiledb`.

GitHub Actions builds the four firmware targets and the Windows VLC module.
The CYD hardware smoke test exercised all 14 rites with injected touches and
verified LCD readback, DAC sample delivery, Wi-Fi scanning, and BLE discovery.
The PCM bench test delivered 8,433 frames over 45 seconds with zero CRC errors
or sequence gaps, and VLC decoded a separate live synth capture. A release image
was then flashed and verified with VLC. Physical finger input, audible speaker
output, Wi-Fi association, live Gemini and LAN use, and connected CSI remain
unverified in that bench pass.

## Credits and license

Diviner is based on the [Cardputer ADV project](https://github.com/numbpill3d/cardputer-adv-diviner).
Its artwork, synths, and shared rites carry through to this fork. The project is
released under the [MIT license](LICENSE). The Daedric font is OMW Ayembedt
under the [SIL Open Font License](tools/OFL.txt). Hardware and rendering use
M5Cardputer, M5Unified, M5GFX, and LovyanGFX.
