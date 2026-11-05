#!/usr/bin/env python3
"""Archive matching dSYMs, preserving the symbols used by far2l's macOS crash logger."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


MACH_MAGICS = {bytes.fromhex(value) for value in (
    "feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca",
    "cafebabf", "bfbafeca",
)}


def run(*args):
    return subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE).stdout


def uuids(path):
    result = sorted(re.findall(r"UUID: ([0-9A-Fa-f-]+) \(([^)]+)\)", run("/usr/bin/dwarfdump", "--uuid", str(path))))
    if not result:
        raise RuntimeError(f"No Mach-O UUID in {path}")
    return result


def validate_dsym(path, expected):
    if uuids(path) != expected:
        raise RuntimeError(f"dSYM UUID mismatch: {path}")
    info = run("/usr/bin/dwarfdump", "--debug-info", "--recurse-depth=0", str(path))
    if "DW_TAG_compile_unit" not in info:
        raise RuntimeError(f"No DWARF compile units in {path}; build with -g before packaging")


def archive_dsym(binary, symbols, image_uuids):
    key = "-".join(uuid for uuid, _ in image_uuids)
    destination = symbols / key / (binary.name + ".dSYM")
    if destination.exists():
        validate_dsym(destination, image_uuids)
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".dsym-", dir=symbols) as temporary:
        output = Path(temporary) / destination.name
        run("/usr/bin/dsymutil", str(binary), "-o", str(output))
        validate_dsym(output, image_uuids)
        output.rename(destination)
    return destination


def prepare(app, symbols, config, source):
    app = app.resolve()
    symbols = symbols.resolve()
    if app == symbols or app in symbols.parents:
        raise RuntimeError("The symbol archive must be outside the .app")
    if not (app / "Contents/MacOS/far2l").is_file():
        raise RuntimeError(f"Not a far2l application bundle: {app}")
    symbols.mkdir(parents=True, exist_ok=True)
    images = []
    for binary in sorted(app.rglob("*")):
        if binary.is_symlink() or not binary.is_file():
            continue
        with binary.open("rb") as stream:
            if stream.read(4) not in MACH_MAGICS:
                continue
        relative = binary.relative_to(app)
        image_uuids = uuids(binary)
        entry = {"path": relative.as_posix(),
                 "uuids": [{"uuid": uuid, "arch": arch} for uuid, arch in image_uuids],
                 "dsym": None}
        if relative.parts[:2] == ("Contents", "MacOS"):
            dsym = archive_dsym(binary, symbols, image_uuids)
            entry["dsym"] = dsym.relative_to(symbols).as_posix()
            if config != "Debug":
                # Keep all nlist local/global names, UUIDs and unwind information.
                # dladdr() needs local function names in the running process; dSYMs
                # are for offline symbolication and cannot replace these names.
                run("/usr/bin/strip", "-S", str(binary))
                if uuids(binary) != image_uuids:
                    raise RuntimeError(f"UUID changed while stripping {binary}")
        # Third-party frameworks retain their existing symbol tables unchanged.
        images.append(entry)
    revision = None
    dirty = None
    try:
        revision = run("git", "-C", str(source), "rev-parse", "HEAD").strip()
        dirty = bool(run("git", "-C", str(source), "status", "--porcelain", "--untracked-files=no").strip())
    except (subprocess.CalledProcessError, FileNotFoundError):
        # Source archives can be built without Git; image UUIDs still identify them.
        pass
    manifest = {"format_version": 1, "build_type": config, "git_revision": revision,
                "git_dirty": dirty, "images": images}
    main_uuid = next(image["uuids"][0]["uuid"] for image in images
                     if image["path"] == "Contents/MacOS/far2l")
    output = symbols / f"far2l-{config}-{main_uuid}.json"
    temporary = output.with_suffix(f".tmp-{os.getpid()}")
    temporary.write_text(json.dumps(manifest, indent=2) + "\n")
    temporary.replace(output)
    count = sum(image["dsym"] is not None for image in images)
    print(f"Archived {count} project dSYMs; image UUID manifest: {output}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--symbols", type=Path, required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("This packaging step is only supported on macOS")
    try:
        prepare(args.app, args.symbols, args.config, args.source)
    except subprocess.CalledProcessError as error:
        parser.exit(1, f"{error.cmd}:\n{error.stderr}\n")
    except (RuntimeError, OSError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
