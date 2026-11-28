#!/usr/bin/env python3
"""Prepare a separate GPL-3.0 ESP-SDR IQ receiver; never build or flash implicitly.

The greenfield wire protocol lives in sibling protocol.*. This adapter uses the
licensed upstream receiver, retains its license/source, and records every edit.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess

UPSTREAM = "3ab3b65"
DSP = "a53a0756833c045311ea1d79a2badf495cdfde4c"


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args]).decode().strip()


def copy_tree(root, out):
    for name in subprocess.check_output(["git", "-C", str(root), "ls-files", "-z"]).decode().split("\0"):
        if not name:
            continue
        source, target = root / name, out / name
        if source.is_dir():
            target.mkdir(parents=True, exist_ok=True)
            copy_tree(source, target)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)


def prepare(upstream, output):
    revision = git(upstream, "rev-parse", "HEAD")
    if not revision.startswith(UPSTREAM) or git(upstream, "status", "--porcelain", "--untracked-files=no"):
        raise ValueError("Require the clean pinned upstream checkout")
    if git(upstream / "components/esp-dsp", "rev-parse", "HEAD") != DSP:
        raise ValueError("Wrong esp-dsp submodule")
    if output.exists():
        raise ValueError("Choose a fresh output path")
    output.mkdir(parents=True)
    copy_tree(upstream, output)
    changes = {}

    def replace(name, old, new):
        path = output / name
        text = path.read_text()
        if text.count(old) != 1:
            raise ValueError("Pinned patch context differs: " + name)
        path.write_text(text.replace(old, new), encoding="utf-8", newline="\n")
        changes.setdefault(name, []).append({"old": old, "new": new})

    # This dedicated IQ prototype does not need 2048-bin spectrum workspaces.
    # Keep all SRAM collision guards and IQ filtering/capture code intact.
    replace("main/common/ring_capture.h", "#define RING_SPEC_NFFT_MAX 2048u", "#define RING_SPEC_NFFT_MAX 256u")
    replace("main/targets/esp32s3/receiver.c",
            "        ESP_ERROR_CHECK(nvs_flash_erase());\n        e = nvs_flash_init();",
            "        ESP_ERROR_CHECK(e); /* Owner NVS is never erased by this adapter. */")
    adapter = hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()
    version = revision + "-cyd-iq-" + adapter[:12]
    replace("tools/generate_version.py", "return dict(revision=revision, build_date=stamp[:10], build_timestamp=stamp, profile=profile)",
            "return dict(revision=" + repr(version) + ", build_date=stamp[:10], build_timestamp=stamp, profile=profile)")
    config = output / "sdkconfig.defaults.esp32s3"
    config.write_text(config.read_text() + "\nCONFIG_ESPTOOLPY_FLASHSIZE_4MB=y\n"
                      "# CONFIG_ESPTOOLPY_FLASHSIZE_2MB is not set\n"
                      "# CONFIG_ESP_SDR_UART_ENABLED is not set\n"
                      "# CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE is not set\n", encoding="utf-8")
    hashes = {p.relative_to(output).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in sorted(output.rglob("*")) if p.is_file()}
    receipt = {"schema": "cyd-sdr-rx-preparation/1", "upstream_repository": "https://github.com/ESPARGOS/esp-sdr",
               "upstream_revision": revision, "upstream_license": "GPL-3.0", "esp_dsp_revision": DSP,
               "adapter_sha256": adapter, "expected_version_revision": version,
               "changes": changes, "files_sha256": hashes,
               "scope": "Standalone continuous IQ receive backend, FFT limited to 256 bins to fit reserved SRAM under SDK b774170. No radio TX, RF performance or on-device protocol demodulation asserted."}
    (output / "cyd-rx-preparation.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    receipt = prepare(args.upstream, args.output)
    print(json.dumps({"expected_version_revision": receipt["expected_version_revision"],
                      "copied_files": len(receipt["files_sha256"])}, indent=2))
