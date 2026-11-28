#!/usr/bin/env python3
"""Exercise the production native-CYD UI and verify rendered instrument state.

Requires an SDL native-cyd executable and the pinned LovyanGFX Font0 source.
No physical hardware or real radio is accessed. Artifacts include complete WAVs,
PPM screenshots, per-process timing records, logs and machine-readable results.
"""
from __future__ import annotations

import argparse
import array
import collections
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import wave
from native_tour import prepare_identity, verify_runtime_identity, verify_unchanged


def ppm(path: Path):
    with path.open("rb") as f:
        assert f.readline().strip() == b"P6", f"not a binary PPM: {path}"
        width, height = map(int, f.readline().split())
        assert f.readline().strip() == b"255"
        pixels = f.read()
    assert len(pixels) == width * height * 3, f"truncated screenshot: {path}"
    assert len(set(zip(pixels[::3], pixels[1::3], pixels[2::3]))) >= 8, f"blank screenshot: {path}"
    return width, height, pixels


def font_bytes(path: Path):
    source = path.read_text().split("font[] PROGMEM = {", 1)[1].split("};", 1)[0]
    source = re.sub(r"//[^\n]*", "", source)
    values = [int(x, 16) for x in re.findall(r"0x([a-fA-F0-9]+)", source)]
    assert len(values) == 1280, "expected the pinned 256-character, 5-column GLCD font"
    return values


def text_score(image, font, text, x, y, scale=4/3, top=0, size=1):
    """Compare actual glyph pixels to the expected word, not a saved screenshot.

    The foreground colour is inferred from expected ink pixels so user palette
    colour changes do not invalidate the glyph check. Both ink and background
    must agree; a blank field cannot pass. Animated graphics are excluded.
    """
    width, height, pixels = image
    on, off = [], []
    for i, ch in enumerate(text):
        for cy in range(8):
            for cx in range(6):
                ink = bool((font[ord(ch)*5 + cx] >> cy) & 1) if cx < 5 else False
                for sy in range(size):
                    for sx in range(size):
                        px = round((x + (i*6 + cx)*size + sx) * scale)
                        py = top + round((y + cy*size + sy) * scale)
                        assert 0 <= px < width and 0 <= py < height, "label outside screenshot"
                        offset = (py*width + px)*3
                        (on if ink else off).append(pixels[offset:offset+3])
    assert on and off
    foreground = collections.Counter(on).most_common(1)[0][0]
    return sum(p == foreground for p in on)/len(on), sum(p != foreground for p in off)/len(off)


def expect_text(path: Path, font, text, x, y, **kwargs):
    image = ppm(path)
    ink, empty = text_score(image, font, text, x, y, **kwargs)
    assert ink >= .98 and empty >= .98, f"{path.name}: expected {text!r}; ink={ink:.3f}, background={empty:.3f}"
    # A deliberate wrong word must fail; this guards against vacuous masks.
    replacement = ("Z" if text[0] != "Z" else "X") + text[1:]
    wrong = text_score(image, font, replacement, x, y, **kwargs)
    assert min(wrong) < .98, "glyph validator accepted the wrong label"
    return {"file": path.name, "expected": text, "ink_match": ink, "background_match": empty}


def inspect_audio(path: Path, required: bool, start=1.0):
    with wave.open(str(path), "rb") as w:
        assert (w.getnchannels(), w.getsampwidth(), w.getframerate()) == (1, 2, 24000)
        count = w.getnframes()
        samples = array.array("h", w.readframes(count))
    if sys.byteorder != "little": samples.byteswap()
    observed = samples[min(len(samples), int(start*24000)):]
    peak = max((abs(v) for v in observed), default=0)
    rms = math.sqrt(sum(v*v for v in observed)/len(observed)) if observed else 0
    assert count >= 24000, "audio recording too short"
    if required:
        assert peak > 64 and rms > 2, f"instrument remained silent after startup: peak={peak}, rms={rms}"
    return {"sample_rate": 24000, "frames": count, "observed_from_seconds": start,
            "observed_peak": peak, "observed_rms": rms, "non_silent_required": required,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--font", type=Path, default=Path(".pio/libdeps/native-cyd/LovyanGFX/src/lgfx/Fonts/glcdfont.h"))
    p.add_argument("--allow-dirty", action="store_true", help="explicitly label an uncommitted development build; never CI qualification")
    args = p.parse_args()
    binary, out = args.binary.resolve(), args.output.resolve()
    assert binary.is_file(), binary
    out.mkdir(parents=True, exist_ok=True)
    font = font_bytes(args.font)
    results = {"classification": "RUNNING", "scope": "native-CYD simulated radio; no physical execution", "cases": []}

    def run(name, actions, duration, audio=False, settings=None):
        record = {"case": name, "classification": "RUNNING"}
        results["cases"].append(record)
        env = dict(os.environ)
        for key in list(env):
            if key.startswith("GRIM_"): del env[key]
        env.update(SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", GRIM_COVEN_NODES="1")
        if settings: env["GRIM_SETTINGS"] = str(settings)
        env["GRIM_WAV"], env["GRIM_METRICS"] = str(out/f"{name}.wav"), str(out/f"{name}-metrics.json")
        steps = []
        for at, action in actions:
            if action.startswith("shot:"): action = f"shot:{out/action[5:]}"
            steps.append(f"{at}:{action}")
        steps.append(f"{duration}:quit")
        env["GRIM_SCRIPT"] = ";".join(steps)
        completed = subprocess.run([str(binary)], env=env, capture_output=True, timeout=duration/1000+20)
        (out/f"{name}.log").write_bytes(completed.stdout + completed.stderr)
        assert completed.returncode == 0, f"{name}: process exit {completed.returncode}"
        verify_runtime_identity((completed.stdout + completed.stderr).decode(errors="replace"), results)
        metrics = json.loads((out/f"{name}-metrics.json").read_text())
        assert metrics["audio_blocks"] > 0 and metrics["frames"] > 0, f"{name}: no executed work"
        record.update({"classification": "PASS", "audio": inspect_audio(out/f"{name}.wav", audio),
                       "metrics": metrics, "screenshots": []})
        for _, action in actions:
            if action.startswith("shot:"):
                path = out/action[5:]
                w, h, _ = ppm(path)
                record["screenshots"].append({"file": path.name, "width": w, "height": h,
                                              "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        return record

    try:
        results.update(prepare_identity(binary, "native-cyd", args.allow_dirty))
        r = run("drone", [(100,"enter"),(300,"key:1"),(600,"touch:145,55,1"),
            (800,"touch:70,120,1"),(900,"touch:70,120,0"),(1100,"tap:96,195"),
            (1600,"shot:drone.ppm")], 1900, True)
        r["labels"] = [expect_text(out/"drone.ppm", font, "tubular",172,96)]
        print("PASS drone: direct XY gesture and style dock", flush=True)

        r = run("canon", [(100,"enter"),(300,"key:4"),(600,"touch:190,58,1"),
            (850,"touch:190,35,1"),(950,"touch:190,35,0"),(1150,"tap:160,195"),
            (1350,"tap:288,195"),(1550,"tap:240,195"),(1750,"tap:270,225"),
            (2100,"shot:canon.ppm")], 2400, True)
        r["labels"] = [expect_text(out/"canon.ppm",font,"aeolian",172,97),
                       expect_text(out/"canon.ppm",font,"HOLD",132,107)]
        print("PASS canon: phrase drag, hold and explicit mode selection", flush=True)

        r = run("spectra", [(100,"enter"),(300,"key:5"),(700,"tap:160,195"),
            (1000,"tap:240,109"),(1250,"touch:238,129,1"),(1450,"touch:300,129,1"),
            (1550,"touch:300,129,0"),(4200,"shot:spectra.ppm")],4500,True)
        r["labels"] = [expect_text(out/"spectra.ppm",font,"ble",168,26),
                       expect_text(out/"spectra.ppm",font,"lydian",168,82),
                       expect_text(out/"spectra.ppm",font,"navi-buds",132,49)]
        print("PASS spectra: BLE population, mode and air gesture", flush=True)

        r = run("glyphsong", [(100,"enter"),(300,"key:6"),(600,"tap:130,50"),
            (800,"tap:16,149"),(1000,"shot:glyphsong-keyboard.ppm"),
            (1200,"key:a3_%20ou5"),(1700,"tap:295,228"),(1900,"tap:248,228"),
            (2100,"tap:280,195"),(2350,"tap:280,195"),(2600,"tap:280,195"),
            (3300,"shot:glyphsong.ppm")],3600,True)
        r["labels"] = [expect_text(out/"glyphsong.ppm",font,"sing",132,98)]
        print("PASS glyphsong: touchscreen composition, annotations and all four modes", flush=True)

        r = run("coven", [(100,"enter"),(300,"key:7"),(650,"tap:175,225"),
            (900,"tap:240,54"),(1200,"tap:125,85"),(1450,"tap:83,85"),
            (1900,"shot:coven.ppm")],2200,True)
        r["labels"] = [expect_text(out/"coven.ppm",font,"A# dorian",132,50)]
        print("PASS coven: local leadership, root/mode, circle note and call", flush=True)

        r = run("visions", [(100,"enter"),(300,"key:9"),(650,"tap:160,100"),
            (950,"shot:visions-fullscreen.ppm"),(1200,"tap:160,100"),
            (1500,"tap:15,230"),(1750,"tap:66,225"),(2000,"tap:160,195"),
            (2250,"tap:175,225"),(2600,"shot:visions-layered.ppm")],2900)
        # Hidden by default, then restored with its Gate + Trails header intact.
        r["labels"] = [expect_text(out/"visions-layered.ppm",font,"Gate + Trails",140,5,top=15)]
        print("PASS visions: fullscreen return and layered Gate + Trails", flush=True)

        settings = out/"native-settings.bin"
        settings.unlink(missing_ok=True)
        r = run("settings-save", [(100,"enter"),(300,"left"),(500,"enter"),
            (750,"tap:160,113"),(1000,"tap:196,79"),(1250,"tap:267,37"),
            (1500,"tap:160,143"),(1750,"tap:176,97"),(2000,"tap:200,142"),
            (2250,"tap:200,97"),(2500,"tap:200,97"),(2850,"shot:portrait180.ppm")],3150,settings=settings)
        record = settings.read_bytes()
        assert len(record) == 32 and record[4] == 3 and record[7] == 3, "settings UI did not save Portrait180 + Glacier"
        assert ppm(out/"portrait180.ppm")[:2] == (240,320)
        r = run("settings-restore", [(100,"enter"),(300,"left"),(500,"enter"),
            (850,"shot:settings-restored.ppm"),(1150,"tap:120,177"),(1400,"tap:52,142"),
            (1750,"shot:landscape180.ppm"),(2000,"tap:267,97"),
            (2300,"shot:landscape.ppm")],2600,settings=settings)
        r["labels"] = [expect_text(out/"settings-restored.ppm",font,"Glacier",164,58,scale=1,top=92)]
        assert ppm(out/"settings-restored.ppm")[:2] == (240,320)
        assert ppm(out/"landscape180.ppm")[:2] == (320,240)
        assert settings.read_bytes()[4] == 0, "rotation UI did not return to Landscape"
        print("PASS settings: palette/orientation persistence across restart and four display orientations",flush=True)
        verify_unchanged(binary, results)
        results["classification"] = "PASS"
    except Exception as exc:
        results["classification"] = "FAIL"
        results["error"] = f"{type(exc).__name__}: {exc}"
        if results["cases"]:
            results["cases"][-1]["classification"] = "FAIL"
            results["cases"][-1]["error"] = results["error"]
        raise
    finally:
        (out/"touch-tour-results.json").write_text(json.dumps(results,indent=2)+"\n")
    print(f"PASS native CYD tour: {len(results['cases'])} processes; result {out/'touch-tour-results.json'}")


if __name__ == "__main__":
    main()
