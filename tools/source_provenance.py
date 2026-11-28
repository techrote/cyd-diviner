"""Content identity for a source checkout, including nonignored untracked files."""
import hashlib
import os
from pathlib import Path
import subprocess


def capture(root):
    root = Path(root)
    def git(*args):
        return subprocess.check_output(["git", *args], cwd=root)
    sha = git("rev-parse", "HEAD").decode().strip()
    tree = git("rev-parse", "HEAD^{tree}").decode().strip()
    status = git("status", "--porcelain", "--untracked-files=all")
    paths = sorted(set(git("ls-files", "-z", "--cached", "--others", "--exclude-standard").split(b"\0")) - {b""})
    digest = hashlib.sha256()
    for raw in paths:
        path = root / os.fsdecode(raw)
        digest.update(raw + b"\0")
        if path.is_symlink():
            digest.update(b"symlink\0" + os.fsencode(os.readlink(path)))
        elif path.is_file():
            digest.update((b"executable\0" if path.stat().st_mode & 0o111 else b"file\0"))
            digest.update(hashlib.sha256(path.read_bytes()).digest())
        else:
            digest.update(b"missing-or-directory\0")
    if sha != git("rev-parse", "HEAD").decode().strip() or status != git("status", "--porcelain", "--untracked-files=all"):
        raise RuntimeError("Source changed while recording provenance")
    return {"source_sha": sha, "source_tree": tree, "dirty": bool(status), "source_snapshot": digest.hexdigest()}
