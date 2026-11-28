# CYD instruments and shared input

The converged instruments keep their original Cardputer keyboard controls and
240×135 logical artwork. CYD adds direct manipulation of that artwork plus a
small instrument-specific dock. The dock opens on entering the five instruments;
the bottom-left chevron hides it and restores a centred performance view. Leaving
an instrument restores the carousel's earlier dock visibility. Existing Palette,
Visions fullscreen, layered effects and keyboard workflows keep their controls.

## Instrument surfaces

| Rite | Direct canvas interaction | Dock controls |
| --- | --- | --- |
| CANON | Select a cell in the 4×2 phrase grid; drag vertically to change its note. Drag tempo and spread meters. Tap the phase reels to realign. | Align, hold, new phrase, mode, notes, rest, erase, volume and Menu. |
| DRONE | Drag the left artwork horizontally for drift and vertically for brightness. | Six-style selector, root notes, scale, hold, wind, bells, safe mode, reseed, volume and Menu. |
| GLYPHSONG | Tap the text line to compose. Drag the right pitch area or pace meter to perform. | Compose, sing, speak/chant/choir/sing mode cycle, pitch, volume and Menu. The keyboard offers symbols (including `_` duration annotations), cursor movement, delete, Hide and Sing. |
| SPECTRA | Tap the waterfall to select the nearest voice. Tap band or mode; drag the air meter. | Explicit Wi-Fi/BLE selection, musical mode, hold, octave, volume and Menu. |
| COVEN | Touch around the circle to choose a local scale degree; tap its centre to call. Root and mode controls apply only on the elected leader. | Call, scale-degree notes, mode, hush, root adjustment, volume and Menu. |

CANON modes are Ionian, Aeolian, pentatonic and Lydian. SPECTRA modes are Dorian,
Lydian, pentatonic and Phrygian. COVEN modes are Aeolian, Dorian, Phrygian and
Lydian. The degree pad in COVEN is labelled I–VII, because those controls select
scale degrees rather than fixed chromatic note names. DRONE retains its six
existing scales. GLYPHSONG cursor buttons emit Ctrl+Left/Right; Fn+arrows retain
the original performance meanings. Performance adjustments do not trigger the
shell's menu-click overlay.

These surfaces control the local synthesis UI. SPECTRA's RSSI remains an
observation of radio signal strength; no control presents it as physical distance.
COVEN's circle is a musical membership display, not a spatial locator.

## Input ownership and bounds

`Rite::pointer` is optional and appended to the existing aggregate. Existing rites
therefore do not need an input adapter. `InstrumentPointer` contains logical
`x`, `y`, `start`, `down` and a wrapping millisecond timestamp. The UI thread alone
calls it. Returning `true` on the initial event captures the gesture; subsequent
motion is clamped to the logical canvas. Release is delivered once after the
existing 30 ms touch debounce. Instruments publish audio controls through their
own bounded audio exchanges; the pointer callback does not render audio.

The platform HAL calibrates and rotates physical coordinates first. The common
controller then maps the visible viewport into 240×135 logical space. Initial
contacts in letterboxing, keyboard overlays or the control dock do not reach the
instrument. Captured gestures remain attached to the same instrument. Changing
rites clears capture, and a still-held contact cannot start a gesture in the new
rite. Landscape, portrait and both 180-degree variants use the same contract.

The touch test fixture checks the actual controller with recording instrument
callbacks and the production palette codec. It does not substitute a simplified
input model for the controller.

## Native CYD simulator

`pio run -e native-cyd` builds the same CYD control layer against the SDL HAL.
The ordinary `native` environment retains the original Cardputer-sized keyboard
simulator. Install SDL2 development headers for either native build. Native radio
is simulated; native timing does not qualify ESP32 deadlines, DAC output, USB
serial delivery or physical touch calibration.

The native CYD UI persists settings only when `GRIM_SETTINGS` names a file. With
no path, settings last for the process session. The file is a bounded 32-byte
record with a version marker, orientation, backdrop, the same V4 palette payload
used by device NVS, and an integrity checksum. Reads reject truncated, appended,
corrupt and out-of-range records. Saves replace a temporary file only after a
complete write; failed saves leave the current palette selection unchanged. This
is host UI storage, not an emulation of ESP32 flash wear or power-cut behaviour.
It contains no network credentials.

## Reproducible control checks

After installing the native dependencies:

```sh
pio run -e native-cyd
bash tests/touch_tests.sh
SANITIZE=1 bash tests/touch_tests.sh
python3 tests/touch_tour.py --binary .pio/build/native-cyd/program --output .pio/touch-tour
```

For a separate pinned LovyanGFX source checkout, set `LGFX_SOURCE` to its `src`
directory and `SDL_INCLUDE` to the SDL2 include directory. `TOUCH_BUILD_DIR`
selects the output directory. The tests cover:

- 247,600 visible pixels across landscape, portrait, keyboard and dock geometry;
  monotonic mapping, edge clamps and clipped/margin rejection.
- Pointer start/motion/release and stale-contact isolation between instruments.
- Visible Menu controls, text-entry opening, cursor modifiers and keyboard Hide.
- All four orientations selected through the actual Settings control path.
- Native custom-palette save/load, exact-length validation, invalid orientation
  rejection, and failed-save retention of the previous palette.

`touch_tour.py` runs eight finite simulator processes. It exercises all five
instrument surfaces, the composing keyboard, musical mode changes, Visions
fullscreen and layered return, and palette/orientation persistence across a new
process. Fixed state labels are compared against the pinned Font0 glyph masks
(e.g. `tubular`, `aeolian`, `HOLD`, `ble`, `lydian`, `navi-buds`, `sing`,
`A# dorian`, `Gate + Trails` and `Glacier`). A deliberately wrong label must fail
the validator. Screenshot existence alone is insufficient. The five instrument
cases also require non-silent, correctly formatted 24 kHz mono WAV output after
the startup interval. Logs, timing JSON, screenshots, recordings and a result
manifest are retained in the specified output directory. A timeout, unexpected
exit, missing evidence or failed rendered-state check fails the tour.

The native test fixture's address/undefined-behaviour sanitizer pass does not
establish absence of data races. LeakSanitizer is unsupported in some ptrace
containers; if it fails before executing the test, retain that limitation and
run ASan/UBSan with `ASAN_OPTIONS=detect_leaks=0` explicitly. The final
qualification report records which full simulator tours, firmware builds and
physical evaluations were actually executed.

## Physical acceptance

Use a stylus and a finger in all four display orientations. Confirm that the
phrase grid and meters reach their visible endpoints, a drag survives leaving its
initial region, a held contact does not reopen a just-closed instrument, and text
composition remains visible above the keyboard. Check the user-selected palette,
leave and re-enter all five instruments, then revisit Palette and fullscreen
layered Visions. Listen while dragging and switching modes. Keep physical touch
feel, acoustic continuity, radio coexistence and NVS persistence as separate owner
acceptance results from the native controller tests.

## Rendered convergence specimens

The [preview gallery](previews/README.md) contains actual native-CYD captures of
all five instruments, the composition keyboard and layered/fullscreen Visions.
The manifest records the clean tested source, binary identity and each PNG hash.
These pixels were reviewed alongside required-state assertions. They establish
rendering and scripted interaction evidence, not physical resistive-touch feel.

The original frozen local tour exposed collapsed taps after a slow frame.
`native_script::Schedule` now dispatches one event per poll and preserves planned
spacing from actual dispatch. `TouchEdges` settles a known released contact
before its next down edge without interrupting held drags. A deterministic
600 ms stall and timer-rollover regression verifies the production controller;
no screenshot assertion was relaxed. The corrected eight-process remote tour
passed. See [qualification history](QUALIFICATION.md) for exact revisions.
