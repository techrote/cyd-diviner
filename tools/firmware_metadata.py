"""PlatformIO post-build: record the actual flash inputs without flashing a device."""
Import("env")
import hashlib
import json
import pathlib
import subprocess
import sys
sys.path.insert(0, str(pathlib.Path(env.subst("$PROJECT_DIR")) / "tools"))
from source_provenance import capture


def git(*args):
    return subprocess.check_output(["git", *args], cwd=env.subst("$PROJECT_DIR"), text=True).strip()



def library_identity(directory, manifest):
    """Read installed resolution as well as the library's declared version.

    PlatformIO 6.2 stores .piopm under VCS metadata first, then the package root.
    Reading its JSON avoids a PlatformIO import in standalone qualification.
    """
    directory = pathlib.Path(directory)
    installed = {}
    for relative in (".git/.piopm", ".hg/.piopm", ".svn/.piopm", ".piopm"):
        metadata = directory / relative
        if metadata.is_file():
            installed = json.loads(metadata.read_text(encoding="utf-8"))
            break
    version = manifest.get("version") or installed.get("version")
    result = {"name": manifest.get("name") or installed.get("name") or directory.name,
              "version": version}
    if installed.get("version"):
        result["resolved_version"] = installed["version"]
    if installed.get("spec"):
        result["source_spec"] = {key: value for key, value in installed["spec"].items()
                                 if key in ("owner", "id", "name", "requirements", "uri")
                                 and value is not None}
    if (directory / ".git").exists():
        revision = subprocess.check_output(["git", "-C", str(directory), "rev-parse", "HEAD"],
                                           text=True, timeout=15).strip()
        if len(revision) != 40 or any(c not in "0123456789abcdef" for c in revision):
            raise ValueError("Invalid installed dependency Git revision")
        result["resolved_revision"] = revision
    return result

def record(source, target, env):
    build = pathlib.Path(env.subst("$BUILD_DIR"))
    firmware = build / (env.subst("$PROGNAME") + ".bin")
    identity = json.loads((build / "build-source.json").read_text())
    if identity != capture(env.subst("$PROJECT_DIR")):
        raise RuntimeError("Source changed during compilation; discard and rebuild")
    images = list(env.get("FLASH_EXTRA_IMAGES", []))
    images.append((env.subst("$ESP32_APP_OFFSET"), str(firmware)))
    entries = []
    for offset, path in images:
        path = pathlib.Path(env.subst(str(path))).resolve()
        data = path.read_bytes()
        entries.append({"offset": int(str(offset), 0), "source_file": str(path),
                        "name": path.name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    platform = env.PioPlatform()
    packages = []
    for name in platform.packages:
        path = platform.get_package_dir(name)
        if not path:
            continue
        manifest = pathlib.Path(path) / "package.json"
        if manifest.is_file():
            data = json.loads(manifest.read_text())
            packages.append({"name": data.get("name", name), "version": data.get("version")})
    libs = []
    libroot = pathlib.Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
    directories = ({p.parent for p in libroot.glob("*/library.json")} |
                   {p.parent for p in libroot.glob("*/library.properties")})
    for directory in sorted(directories):
        manifest = directory / "library.json"
        if manifest.is_file():
            data = json.loads(manifest.read_text())
        else:
            data = {}
            for line in (directory / "library.properties").read_text(encoding="utf-8-sig").splitlines():
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                key, separator, value = line.partition("=")
                if separator:
                    data[key.strip()] = value.strip()
        libs.append(library_identity(directory, data))
    debug = []
    for suffix in ("elf", "map"):
        path = build / (env.subst("$PROGNAME") + "." + suffix)
        if path.exists():
            data = path.read_bytes()
            debug.append({"name": path.name, "source_file": str(path.resolve()), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    info = {
        "schema": 1, "environment": env.subst("$PIOENV"), **identity,
        "platform": platform.name, "platform_version": platform.version,
        "chip": env.BoardConfig().get("build.mcu"),
        "flash_size": env.BoardConfig().get("upload.flash_size"),
        "app_offset": int(env.subst("$ESP32_APP_OFFSET"), 0),
        "images": entries, "debug_files": debug, "packages": packages, "libraries": libs,
    }
    (build / "flash-layout.json").write_text(json.dumps(info, indent=2) + "\n")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", record)
