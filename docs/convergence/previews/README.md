# Native-CYD rendered previews

These are actual PNG conversions of simulator PPM captures from the corrected
scripted-touch qualification, not design mockups. [Manifest](manifest.json) gives
the clean source SHA/tree/snapshot, executable SHA-256 and PNG hashes.

[Source-qualified run](https://github.com/techrote/cyd-diviner/actions/runs/46113077668)
passed all eight processes, required-state glyph assertions and five non-silent
instrument recordings. The source is `dcb793572cbcec2d3395980cf6fbcac46d696392`, which
contains the production fix `502ffde689f7e03f22a4d3114a11877276a43b56` and an isolated
CI workflow. That temporary workflow is not part of the integrated application.
These are component capture receipts; final combined-source captures and WAVs
come from the corresponding exact-head simulator artifact linked on PR #8.

| Specimen | Control or regression exercised |
| --- | --- |
| [CANON](canon.png) | Direct phrase grid, tempo/spread, Aeolian and hold |
| [DRONE](drone.png) | Tubular style, drift/brightness XY and accessible style controls |
| [GLYPHSONG singing](glyphsong.png) | Hidden keyboard, syllabic singing and piano roll |
| [GLYPHSONG composition](glyphsong-keyboard.png) | Text keyboard, symbols, cursor, Hide and Sing |
| [SPECTRA](spectra.png) | BLE mode, invented navi-buds fixture, Lydian and waterfall |
| [COVEN](coven.png) | Musical circle, A-sharp/Dorian and local note controls |
| [Fullscreen Visions](visions-fullscreen.png) | Preserved fullscreen effect rendering |
| [Layered Visions](visions-layered.png) | Gate + Trails return path and controls |

The principal reviewer and platform specialist inspected these pixels. No
concrete clipping/overlap defect was identified in these specimens. Resistive
touch accuracy, finger comfort, physical display timing and audio continuity
remain **NOT RUN** and require owner evaluation.
