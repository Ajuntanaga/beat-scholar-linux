#!/usr/bin/env python3
"""Copy the licensed Beat Scholar data library into the private XDG store.

This never reads license material or modifies the Modalics installation. Files
are copied byte-for-byte and described by a SHA-256 manifest so malformed but
loadable legacy JSON remains available to the compatibility importer.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile

DEFAULT_SOURCE = (
    Path.home() / ".wine-reaper-plugins/drive_c/users" / Path.home().name
    / "AppData/Roaming/Modalics/Beat Scholar"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def inventory(source: Path) -> list[dict[str, object]]:
    allowed_roots = {"Presets", "Kits", "Step Randomizer"}
    entries: list[dict[str, object]] = []
    candidates: list[Path] = []
    for name in sorted(allowed_roots):
        root = source / name
        if root.is_symlink() or not root.is_dir():
            continue
        for directory, folders, files in os.walk(root, followlinks=False):
            folders[:] = [folder for folder in folders if not (Path(directory) / folder).is_symlink()]
            candidates.extend(Path(directory) / name for name in files if name.endswith(".json"))
    for path in sorted(candidates):
        # lstat excludes both linked private files and FIFOs/devices that could block a migration.
        if not stat.S_ISREG(path.lstat().st_mode):
            continue
        relative = path.relative_to(source)
        entries.append(
            {
                "path": relative.as_posix(),
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
            }
        )
    return entries


def migrate(source: Path, destination: Path, dry_run: bool = False) -> dict[str, object]:
    if not source.is_dir():
        raise FileNotFoundError(f"Beat Scholar content root does not exist: {source}")
    entries = inventory(source)
    manifest: dict[str, object] = {
        "schema": 1,
        "product": "Beat Scholar Linux private factory migration",
        "source": str(source),
        "files": entries,
    }
    if dry_run:
        return manifest

    destination.mkdir(parents=True, exist_ok=True)
    for entry in entries:
        relative = Path(str(entry["path"]))
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        source_file = source / relative
        if target.exists() and sha256(target) == entry["sha256"]:
            continue
        with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as temporary:
            temporary_path = Path(temporary.name)
        try:
            shutil.copy2(source_file, temporary_path)
            temporary_path.replace(target)
        finally:
            temporary_path.unlink(missing_ok=True)

    manifest_path = destination / "factory-manifest.json"
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=destination, delete=False) as temporary:
        json.dump(manifest, temporary, indent=2, sort_keys=True)
        temporary.write("\n")
        temporary_path = Path(temporary.name)
    temporary_path.replace(manifest_path)
    return manifest


def main() -> int:
    configured = os.environ.get("XDG_DATA_HOME", "")
    data_home = Path(configured) if Path(configured).is_absolute() else Path.home() / ".local/share"
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument(
        "--destination", type=Path, default=data_home / "BeatScholarLinux/Factory"
    )
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    manifest = migrate(args.source.resolve(), args.destination.resolve(), args.dry_run)
    roots: dict[str, int] = {}
    for entry in manifest["files"]:
        root = str(entry["path"]).split("/", 1)[0]
        roots[root] = roots.get(root, 0) + 1
    print(json.dumps({"destination": str(args.destination), "files": len(manifest["files"]), "groups": roots}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
