"""PlatformIO build identity. Identity refers to source files, not an assumed clean SHA."""
Import("env")
import hashlib
import pathlib
import subprocess

root = pathlib.Path(env.subst("$PROJECT_DIR"))
sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
dirty = bool(subprocess.check_output(["git", "status", "--porcelain", "--", "platformio.ini", "src", "tools/perf/identity.py"], cwd=root, text=True).strip())
source = hashlib.sha256()
paths = [root / "platformio.ini"] + sorted((root / "src").rglob("*"))
for path in paths:
    if path.is_file():
        source.update(path.relative_to(root).as_posix().encode() + b"\0" + path.read_bytes())
env.Append(CPPDEFINES=[
    ("GRIM_PERF_SOURCE", env.StringifyMacro(sha)),
    ("GRIM_PERF_DIRTY", int(dirty)),
    ("GRIM_PERF_SOURCE_DIGEST", env.StringifyMacro(source.hexdigest())),
    ("GRIM_PERF_PROFILE", env.StringifyMacro(env.subst("$PIOENV"))),
])
