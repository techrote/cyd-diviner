#!/usr/bin/env python3
"""Collect exact-target atomic macros and codegen without executing target code.

Run after: pio run -e cardputer -t compiledb
This probe changes no production source and does not qualify physical execution.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from source_provenance import capture

SOURCE = r"""
#include <atomic>
#include <cstdint>
#include "sdkconfig.h"
#if defined(__XTENSA__)
#include "xtensa/config/core-isa.h"
#endif
struct alignas(4) ProbeWord { std::atomic<uint32_t> word{0}; };
extern "C" {
__attribute__((used,noinline)) bool probe_trait() {
    return std::atomic<uint32_t>::is_always_lock_free;
}
__attribute__((used,noinline)) bool probe_builtin_trait() {
    return __atomic_always_lock_free(sizeof(uint32_t), nullptr);
}
__attribute__((used,noinline)) bool probe_pointer_trait(ProbeWord *p) {
    return p->word.is_lock_free();
}
__attribute__((used,noinline)) uint32_t probe_load(ProbeWord *p) {
    return p->word.load(std::memory_order_acquire);
}
__attribute__((used,noinline)) void probe_store(ProbeWord *p, uint32_t n) {
    p->word.store(n, std::memory_order_release);
}
__attribute__((used,noinline)) uint32_t probe_exchange(ProbeWord *p, uint32_t n) {
    return p->word.exchange(n, std::memory_order_acq_rel);
}
__attribute__((used,noinline)) bool probe_cas(ProbeWord *p, uint32_t *expected, uint32_t desired) {
    return p->word.compare_exchange_strong(*expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
}
__attribute__((used,noinline)) bool probe_flag(std::atomic_flag *p) {
    return p->test_and_set(std::memory_order_acquire);
}
__attribute__((used,noinline)) void probe_flag_clear(std::atomic_flag *p) {
    p->clear(std::memory_order_release);
}
}
"""

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, default=ROOT / "compile_commands.json")
    parser.add_argument("--environment", default="cardputer")
    parser.add_argument("--output", type=Path, default=ROOT / "delivery/atomic-probe-cardputer")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {"status": "RUNNING", **capture(ROOT), "environment": args.environment,
              "scope": "Cross-compiler macros, object disassembly and installed runtime archive inspection; target code NOT EXECUTED",
              "commands": [], "headers": [], "archives": []}
    try:
        database = json.loads(args.database.read_text())
        candidates = [item for item in database if str(item["file"]).replace("\\", "/").endswith("src/apps/canon.cpp")]
        if not candidates:
            raise RuntimeError("No actual CANON command in compilation database")
        # Prefer this environment's command if a database contains more than one.
        selected = next((item for item in candidates if args.environment in (item.get("command", "") + str(item.get("arguments", [])))), candidates[0])
        raw = selected.get("arguments") or shlex.split(selected["command"])
        directory = Path(selected["directory"])
        compiler = Path(raw[0]).resolve()
        base = [str(compiler)]
        skip_next = False
        source_file = Path(selected["file"])
        if not source_file.is_absolute():
            source_file = (directory / source_file).resolve()
        for flag in raw[1:]:
            if skip_next:
                skip_next = False
                continue
            if flag in ("-o", "-MF", "-MT", "-MQ"):
                skip_next = True
                continue
            if flag in ("-c", "-MMD", "-MD", "-MP"):
                continue
            candidate = Path(flag)
            if not flag.startswith("-"):
                if not candidate.is_absolute():
                    candidate = directory / candidate
                if candidate.resolve() == source_file:
                    continue
            base.append(flag)
        (out / "selected-command.json").write_text(json.dumps(selected, indent=2) + "\n")
        probe = out / "atomic-probe.cpp"
        probe.write_text(SOURCE)

        def run(name, command, required=True, timeout=180):
            result = subprocess.run(command, cwd=directory, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=timeout)
            (out / (name + ".log")).write_text(result.stdout)
            report["commands"].append({"name": name, "command": command, "exit": result.returncode})
            if required and result.returncode:
                raise RuntimeError(name + " failed: " + result.stdout[-2500:])
            return result.stdout

        report["compiler"] = run("compiler-version", [str(compiler), "--version"]).splitlines()[0]
        macros = run("macros", [*base, "-dM", "-E", str(probe)])
        relevant = re.compile(r"(ATOMIC|S32C1I|SPIRAM|SOC_CPU_CORES_NUM|__XTENSA|__GNUC|__SIZEOF_INT|__SIZEOF_LONG)")
        report["relevant_macros"] = [line for line in macros.splitlines() if relevant.search(line)]
        (out / "relevant-macros.txt").write_text("\n".join(report["relevant_macros"]) + "\n")
        dependencies = run("dependencies", [*base, "-M", str(probe)])
        dependencies = dependencies.replace("\\\n", " ").split(": ", 1)[-1]
        for path_text in shlex.split(dependencies):
            path = Path(path_text)
            if not path.is_absolute():
                path = directory / path
            if path.name in ("sdkconfig.h", "core-isa.h", "atomic_lockfree_defines.h", "c++config.h") and path.is_file():
                data = path.read_bytes()
                name = hashlib.sha256(str(path).encode()).hexdigest()[:10] + "-" + path.name
                (out / name).write_bytes(data)
                report["headers"].append({"path": str(path), "file": name, "sha256": hashlib.sha256(data).hexdigest()})
        assembly = out / "atomic-probe.s"
        obj = out / "atomic-probe.o"
        run("compile-assembly", [*base, "-S", str(probe), "-o", str(assembly)])
        run("compile-object", [*base, "-c", str(probe), "-o", str(obj)])
        objdump = compiler.with_name(compiler.name.removesuffix("g++") + "objdump")
        nm = compiler.with_name(compiler.name.removesuffix("g++") + "nm")
        run("probe-disassembly", [str(objdump), "-dr", "-C", str(obj)])
        packages = compiler.parents[2]
        archives = set()
        for library in ("libnewlib.a", "libesp_libc.a"):
            archives.update(path for path in packages.rglob(library) if "esp32s3" in str(path))
        for library in ("libatomic.a", "libgcc.a"):
            found = run("locate-" + library, [*base, "-print-file-name=" + library]).strip().splitlines()[-1]
            path = Path(found)
            if path.is_file():
                archives.add(path.resolve())
        function = re.compile(r"^[0-9a-fA-F]+ <([^>]+)>:")
        for index, archive in enumerate(sorted(archives)):
            prefix = "runtime-" + str(index)
            symbols = run(prefix + "-symbols", [str(nm), "-A", str(archive)], required=False)
            if not re.search(r"__atomic_(?:s32c1i_)?(?:exchange|compare_exchange|load|store)_4", symbols):
                continue
            dump = run(prefix + "-disassembly", [str(objdump), "-dr", "-C", str(archive)])
            lines = []
            keep = False
            for line in dump.splitlines():
                match = function.match(line)
                if match:
                    keep = bool(re.search(r"__atomic_(?:s32c1i_)?(?:exchange|compare_exchange|load|store)_4|__atomic_is_lock_free", match.group(1)))
                if keep:
                    lines.append(line)
            name = prefix + "-atomic-functions.txt"
            (out / name).write_text("\n".join(lines) + "\n")
            report["archives"].append({"path": str(archive), "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(), "functions": name})
        if capture(ROOT) != {key: report[key] for key in ("source_sha", "source_tree", "dirty", "source_snapshot")}:
            raise RuntimeError("Source changed during probe")
        report["status"] = "COLLECTED"
    except Exception as error:
        report["status"] = "FAIL"
        report["error"] = str(error)
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "source_sha": report["source_sha"],
                      "compiler": report.get("compiler"), "error": report.get("error"),
                      "output": str(out)}, indent=2))
    return 0 if report["status"] == "COLLECTED" else 1

if __name__ == "__main__":
    raise SystemExit(main())
