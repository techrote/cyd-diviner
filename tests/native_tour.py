#!/usr/bin/env python3
"""Exercise the real native application, capture pixels/WAVs and retain failures."""
import argparse
import array
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from source_provenance import capture

SOURCE_FIELDS = ("source_sha", "source_tree", "dirty", "source_snapshot")


def prepare_identity(program, profile, allow_dirty=False):
    """Bind an executed simulator to its recorded build and current checkout."""
    current = capture(ROOT)
    recorded = json.loads((program.parent / "build-source.json").read_text())
    if any(recorded.get(key) != current[key] for key in SOURCE_FIELDS):
        raise ValueError("Simulator source identity is stale; rebuild this checkout before qualification")
    if current["dirty"] and not allow_dirty:
        raise ValueError("Uncommitted source requires --allow-dirty for an explicitly labelled development run")
    return {**current, "build_profile": profile,
            "binary_sha256": hashlib.sha256(program.read_bytes()).hexdigest(),
            "qualification_source": "dirty development snapshot" if current["dirty"] else "clean committed source"}


def verify_runtime_identity(log, identity):
    """Reject an adjacent manifest that belongs to a different executable."""
    state = "dirty" if identity["dirty"] else "clean"
    expected = (f"diviner_identity source={identity['source_sha']} tree={identity['source_tree']} "
                f"profile={identity['build_profile']} state={state}")
    observed = [line.strip() for line in log.splitlines() if line.startswith("diviner_identity ")]
    if observed != [expected]:
        raise ValueError(f"Executed simulator identity mismatch: expected {expected!r}; observed {observed!r}")


def verify_unchanged(program, identity):
    if capture(ROOT) != {key: identity[key] for key in SOURCE_FIELDS}:
        raise ValueError("Source changed during simulator qualification; discard and rerun")
    if hashlib.sha256(program.read_bytes()).hexdigest() != identity["binary_sha256"]:
        raise ValueError("Simulator executable changed during qualification; discard and rerun")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", type=Path, default=ROOT / ".pio/build/native/program")
    parser.add_argument("--output", type=Path, default=ROOT / "delivery/native-tour")
    parser.add_argument("--allow-dirty", action="store_true", help="explicitly label an uncommitted development build; never CI qualification")
    args = parser.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    program = args.program.resolve()
    report = {"scope": "Native real application; simulated radio; desktop timings; no physical qualification", "cases": []}
    try:
        report.update(prepare_identity(program, "native", args.allow_dirty))
    except Exception as error:
        report.update(status="FAIL", error=f"{type(error).__name__}: {error}")
        (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print("native-tour: FAIL:", report["error"], file=sys.stderr)
        return 1
    cases = [
        ("canon", "4", "700:key:awsedftg;950:up;1100:right;1400:enter", 4200),
        ("drone", "1", "700:key:p;950:up;1100:right;1400:key:pp;2200:key:p", 4500),
        ("glyphsong", "6", "600:key:day3_ time5;900:enter;1200:tab;1700:tab;2400:tab", 5000),
        ("spectra", "5", "2200:key:b", 6500),
        ("coven", "7", "900:key:c;1400:key:a;2400:key: ", 4500),
    ]
    for name, shortcut, actions, duration in cases:
        shot = out / (name + ".ppm")
        audio = out / (name + ".wav")
        metrics = out / (name + "-metrics.json")
        script = f"50:enter;300:key:{shortcut};{actions};{duration-300}:shot:{shot};{duration}:quit"
        env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", GRIM_TRACE="1",
                   GRIM_SCRIPT=script, GRIM_WAV=str(audio), GRIM_METRICS=str(metrics))
        result = subprocess.run([str(program)], cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=duration/1000+20)
        log = result.stdout.decode(errors="replace"); (out / (name + ".log")).write_text(log)
        item = {"name": name, "exit": result.returncode, "status": "FAIL"}
        try:
            assert result.returncode == 0, log[-2000:]
            verify_runtime_identity(log, report)
            assert f"rite_enter name={name}" in log, "Expected actual rite entry was not observed"
            assert f"rite_leave name={name}" in log, "Expected graceful state destruction was not observed"
            from PIL import Image
            image = Image.open(shot)
            assert image.size == (240, 135), image.size
            assert len(image.getcolors(100000) or []) > 8, "Blank/flat screenshot"
            image.save(out / (name + ".png"))
            with wave.open(str(audio)) as w:
                assert (w.getnchannels(), w.getsampwidth(), w.getframerate()) == (1, 2, 24000)
                samples = array.array("h", w.readframes(w.getnframes()))
                if sys.byteorder != "little": samples.byteswap()
            tail = samples[-24000:]
            assert len(tail) == 24000 and max(abs(x) for x in tail) > 20, "Instrument tail is silent"
            item.update(status="PASS", duration_s=len(samples)/24000, tail_peak=max(abs(x) for x in tail),
                        tail_rms=(sum(x*x for x in tail)/len(tail))**0.5, timing=json.loads(metrics.read_text()))
        except Exception as e:
            item["error"] = str(e)
        report["cases"].append(item)
        print(f"{name}: {item['status']}", flush=True)

    # Real shell navigation through every original and new rite, repeated.
    names = ["drone", "seq", "beat", "canon", "spectra", "glyphsong", "coven", "wired", "visions",
             "oracle", "wifi", "scry", "listen", "kin", "net", "gemini", "banish", "sentinel", "pulse", "probe"]
    script = ["50:enter", "250:key:1"]
    now = 250
    for i in range(len(names) * 3):
        script.append(f"{now+220}:esc")
        if i + 1 < len(names) * 3:
            script += [f"{now+270}:right", f"{now+300}:enter"]
        now += 300
    script.append(f"{now+300}:quit")
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", GRIM_TRACE="1",
               GRIM_SCRIPT=";".join(script), GRIM_WAV=str(out / "lifecycle.wav"), GRIM_METRICS=str(out / "lifecycle-metrics.json"))
    result = subprocess.run([str(program)], cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
    log = result.stdout.decode(errors="replace"); (out / "lifecycle.log").write_text(log)
    entered = [line.split("name=", 1)[1] for line in log.splitlines() if line.startswith("rite_enter name=")]
    left = [line.split("name=", 1)[1] for line in log.splitlines() if line.startswith("rite_leave name=")]
    identity_error = None
    try:
        verify_runtime_identity(log, report)
    except Exception as error:
        identity_error = str(error)
    ok = result.returncode == 0 and entered == names*3 and left == names*3 and identity_error is None
    report["cases"].append({"name": "full-carousel-lifecycle", "status": "PASS" if ok else "FAIL", "exit": result.returncode,
                            "expected_entries": len(names)*3, "entered": entered, "left": left, "identity_error": identity_error})
    report["status"] = "PASS" if all(c["status"] == "PASS" for c in report["cases"]) else "FAIL"
    try:
        verify_unchanged(program, report)
    except Exception as error:
        report.update(status="FAIL", error=f"{type(error).__name__}: {error}")
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    (out / "SHA256SUMS.txt").write_text("".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in sorted(out.iterdir()) if p.is_file() and p.name != "SHA256SUMS.txt"))
    print("native-tour:", report["status"])
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
