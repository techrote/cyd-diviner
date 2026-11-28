#!/usr/bin/env python3
"""Generate the explicit, reviewable physical workload matrix; performs no I/O to a board."""
from __future__ import annotations

import argparse
import json
import pathlib


def plans() -> dict[str, dict]:
    full = {"schema": "cyd-perf-plan/1", "seed": 23063, "repetitions": 3,
            "duration_ms": 10000, "warmup_ms": 2000, "workloads": []}
    cases = full["workloads"]

    def add(name: str, commands: list[str], **settings) -> None:
        cases.append({"name": name, "commands": ["perf enable 1", "perf radio off", "perf seed 23063", *commands], **settings})

    for orientation, orientation_name in enumerate(("landscape", "portrait")):
        for backdrop, backdrop_name in enumerate(("theme", "rain", "ashfire", "plasma")):
            add(f"menu/{orientation_name}/{backdrop_name}",
                [f"perf orient {orientation}", f"perf backdrop {backdrop}", "perf scene menu"],
                orientation=orientation_name, backdrop=backdrop_name, audio="local_dac")

    for orientation, orientation_name in enumerate(("landscape", "portrait")):
        for fullscreen in (0, 1):
            display = "fullscreen" if fullscreen else "ui"
            for effect, effect_name in enumerate(("plasma", "gate", "ashfire", "rain", "kaleido", "trails")):
                for fmt in ((0, 1) if effect == 5 else (0,)):
                    trail = "/rgb565" if fmt else ("/rgb444" if effect == 5 else "")
                    add(f"visions/{orientation_name}/{display}/{effect_name}{trail}",
                        [f"perf orient {orientation}", "perf scene visions",
                         f"perf vision {effect} 0 {fullscreen} {fmt}"],
                        orientation=orientation_name, fullscreen=bool(fullscreen), effect=effect_name,
                        layered=False, trail_format="rgb565" if fmt else "rgb444", audio="local_dac")
            for effect, effect_name in enumerate(("plasma-kaleido", "gate-trails", "ashfire-rain")):
                add(f"visions/{orientation_name}/{display}/{effect_name}",
                    [f"perf orient {orientation}", "perf scene visions",
                     f"perf vision {effect} 1 {fullscreen} 0"],
                    orientation=orientation_name, fullscreen=bool(fullscreen), effect=effect_name,
                    layered=True, trail_format="indexed" if effect == 1 else "rgb444", audio="local_dac")

    for scene in ("drone", "seq", "beat"):
        for orientation, orientation_name in enumerate(("landscape", "portrait")):
            add(f"audio/{scene}/{orientation_name}/local", [f"perf orient {orientation}", f"perf scene {scene}"],
                orientation=orientation_name, audio="local_dac", scene=scene)
        add(f"audio/{scene}/local-and-pcm", ["perf orient 0", f"perf scene {scene}"],
            audio="local_dac_and_usb_pcm", pcm=True, scene=scene)

    add("visions/fullscreen/layered/pcm", ["perf orient 0", "perf scene visions", "perf vision 1 1 1 0"],
        pcm=True, audio="local_dac_and_usb_pcm", effect="gate-trails", fullscreen=True, layered=True)
    for audio in ("drone", "seq", "beat"):
        add(f"combined/fullscreen/gate-trails/{audio}", ["perf orient 0", "perf scene visions",
            "perf vision 1 1 1 0", f"perf combined {audio}"], audio=audio, layered=True, fullscreen=True)
    add("instrumentation/off/menu", ["perf orient 0", "perf backdrop 0", "perf scene menu", "perf enable 0"],
        instrumentation=False, limitation="Sampling/tick hooks remain; disables histogram accumulation only. Compare true present counts with enabled control.")
    add("instrumentation/on/menu", ["perf orient 0", "perf backdrop 0", "perf scene menu"], instrumentation=True)
    for radio in ("wifi", "ble", "espnow"):
        add(f"radio-init/{radio}/menu", ["perf orient 0", "perf backdrop 0", "perf scene menu", f"perf radio {radio}"],
            radio=radio, traffic="none", limitation="Controller initialization only; no scans, association, peers, traffic, or goodput claim.")
        add(f"radio-init/{radio}/fullscreen-layered", ["perf orient 0", "perf scene visions", "perf vision 1 1 1 0", f"perf radio {radio}"],
            radio=radio, traffic="none", fullscreen=True, layered=True,
            limitation="Controller initialization only; traffic/coexistence measurements require owned peers.")
    add("touch/scripted-menu-rapid", ["perf orient 0", "perf backdrop 0", "perf scene menu"],
        actions=[{"at_ms": index * 220 + 1000, "command": f"tap {x} {y}"}
                 for index, (x, y) in enumerate(((20, 120), (300, 120), (20, 120), (300, 120),
                                                (20, 120), (300, 120), (20, 120), (300, 120)))],
        limitation="Scripted diagnostic injection measures firmware processing only, not physical contact latency.")
    add("touch/visions-fullscreen-transitions", ["perf orient 0", "perf scene visions", "perf vision 1 1 0 0"],
        actions=[{"at_ms": index * 500 + 1000, "command": "tap 160 120"} for index in range(16)],
        limitation="Artwork taps remain within Visions; firmware processing only, not physical latency.")
    add("sustained/cpu-both-cores", ["perf orient 0", "perf scene menu"],
        stress_seconds=365, warmup_ms=0, duration_ms=365000, repetitions=1,
        limitation="Near 100% CPU requires measured busy percentages; sensor indication is uncalibrated unless independently calibrated.")
    add("post-stress/visions-fullscreen-layered", ["perf orient 0", "perf scene visions", "perf vision 1 1 1 0"],
        audio="local_dac", effect="gate-trails", fullscreen=True, layered=True)
    add("spi/full-frame-rgb565", ["perf orient 0", "perf scene menu"], kind="spi", samples=128)
    for kernel in ("osc-f32", "osc-q15", "filter-f32", "filter-q15", "modal-f32", "modal-q15",
                   "fft-f32", "cellular", "particles", "feedback", "stripe-rgb565"):
        for large in (False, True):
            add(f"kernel/{kernel}/{'large' if large else 'realistic'}", ["perf scene menu"],
                kind="kernel", kernel=kernel, kernel_samples=256, large=large,
                limitation="Target compute window blocks foreground UI, while the ordinary audio worker remains scheduled.")
    for name, reason in (
        ("physical/contact-to-photon", "Requires owner-operated physical touch stimulus and external optical timing fixture."),
        ("audio/usb-only-dac-disabled", "Existing PCM streams the same mixed blocks sent to the local DAC; independent DAC-disable path is not assumed."),
        ("audio/external-dac-loopback", "External acquisition fixture required to detect analog gaps/distortion independently."),
        ("wireless/wifi-goodput-rtt", "Requires explicitly authorized owned AP/peer and configured traffic endpoint; no network probes are automatic."),
        ("wireless/ble-goodput-rtt", "Requires explicitly authorized BLE peer; no scans/probes of unrelated devices."),
        ("wireless/espnow-goodput-rtt", "Requires explicitly authorized ESP-NOW peer and owned channel."),
        ("wireless/coexistence", "Requires explicitly authorized Wi-Fi/BLE/ESP-NOW peers and separately captured local-workload baseline."),
        ("portable/esp32-s3-physical", "Physical S3 board identity/backup/flash authorization required; compile-only is distinct evidence."),
        ("portable/esp32-c3-physical", "Physical C3 board identity/backup/flash authorization required; compile-only is distinct evidence."),
    ):
        cases.append({"name": name, "status": "NOT RUN", "reason": reason})
    smoke_names = {"menu/landscape/theme", "visions/landscape/fullscreen/gate-trails",
                   "audio/drone/landscape/local", "audio/drone/local-and-pcm",
                   "sustained/cpu-both-cores", "post-stress/visions-fullscreen-layered"}
    smoke = {**full, "workloads": [case for case in cases if case["name"] in smoke_names or case.get("status") == "NOT RUN"]}
    stress = {**full, "workloads": [case for case in cases if case["name"] == "sustained/cpu-both-cores"]}
    return {"full": full, "smoke": smoke, "stress": stress}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path(__file__).parent)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name, plan in plans().items():
        path = args.output / f"plan-{name}.json"
        path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
        cases = [case for case in plan["workloads"] if case.get("status") != "NOT RUN"]
        seconds = sum((case.get("duration_ms", plan["duration_ms"]) + case.get("warmup_ms", plan["warmup_ms"]))
                      / 1000 * case.get("repetitions", plan["repetitions"])
                      for case in cases if case.get("kind") not in ("kernel", "spi"))
        print(f"{path}: {len(cases)} workloads; requested scene warmup+measurement {seconds:g} s plus kernels/SPI/setup/export")


if __name__ == "__main__":
    main()
