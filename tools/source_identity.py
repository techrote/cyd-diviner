"""Compile the checked-out revision/profile into diagnostic identity; no secrets."""
Import("env")
import json
import pathlib
import subprocess
import sys

root = env.subst("$PROJECT_DIR")
sys.path.insert(0, str(pathlib.Path(root) / "tools"))
from source_provenance import capture


def git(*args):
    return subprocess.check_output(["git", *args], cwd=root, text=True).strip()


build = pathlib.Path(env.subst("$BUILD_DIR"))
build.mkdir(parents=True, exist_ok=True)
identity = capture(root)
values = {"DIVINER_SOURCE_SHA": identity["source_sha"],
          "DIVINER_SOURCE_TREE": identity["source_tree"],
          "DIVINER_BUILD_ENV": env.subst("$PIOENV"),
          "DIVINER_TREE_STATE": "dirty" if identity["dirty"] else "clean"}
text = "#pragma once\n" + "".join(f"#define {name} {json.dumps(value)}\n" for name, value in values.items())
header = build / "diviner_build_identity_generated.h"
if not header.exists() or header.read_text() != text:
    header.write_text(text)
env.Append(CPPPATH=[str(build.resolve())])
(build / "build-source.json").write_text(json.dumps(identity, indent=2) + "\n")
