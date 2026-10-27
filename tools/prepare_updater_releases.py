"""Prepare an ignored, curated release catalog beside the native updater.

Both input packages are explicit; no directory scan, network request or date
guess selects a release. The executable's offline `inspect` command supplies
authoritative package validation. This tool never discovers or opens hardware.
Original vendor packages remain local inputs; keep generated output ignored and
review redistribution rights before distributing a prepared bundle.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "build/updater-target/release"
DEFAULT_UPDATER = DEFAULT_OUTPUT / ("thy666-updater.exe" if os.name == "nt" else "thy666-updater")
PACKAGE_BYTES_MAX = 78 + 0x80000
MANIFEST_BYTES_MAX = 65536
PROTOCOL = "THY666-UPDATER/1"
STOCK_V5_SHA256 = "125e18005b4e6f797d2b300649f5207d3e7a6f4b2e0af5e3000234b4f4efe990"
BUILD = re.compile(r"[0-9a-f]{16}\Z")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def regular_bytes(path: Path, limit: int, label: str) -> bytes:
    """Bound the actual read and reject links, directories and special files."""
    try:
        if not stat.S_ISREG(path.lstat().st_mode):
            raise ValueError(f"{label} must be a regular file, not a link")
        with path.open("rb") as stream:
            metadata = os.fstat(stream.fileno())
            if not stat.S_ISREG(metadata.st_mode) or metadata.st_size > limit:
                raise ValueError(f"{label} exceeds its format limit or is not a regular file")
            data = stream.read(limit + 1)
        if len(data) > limit:
            raise ValueError(f"{label} grew beyond its format limit")
        if not stat.S_ISREG(path.lstat().st_mode):
            raise ValueError(f"{label} changed while reading")
        return data
    except OSError as error:
        raise ValueError(f"Could not read {label}") from error


def inspect_package(updater: Path, path: Path, kind: str,
                    expected_build: str | None = None) -> tuple[bytes, dict]:
    """Invoke only the no-USB inspector and bind its result to exact input bytes."""
    before = regular_bytes(path, PACKAGE_BYTES_MAX, f"{kind} package")
    if len(before) < 78:
        raise ValueError(f"{kind} package has no complete header")
    command = [str(updater), "inspect", str(path)]
    if expected_build is not None:
        command += ["--expected-build", expected_build]
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=True,
                                timeout=30, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    except (OSError, subprocess.SubprocessError) as error:
        raise ValueError(f"Offline updater inspection failed for {kind} package") from error
    if len(result.stdout) > MANIFEST_BYTES_MAX:
        raise ValueError("Updater inspection response exceeded its format limit")
    try:
        response = json.loads(result.stdout)
    except (ValueError, TypeError) as error:
        raise ValueError("Updater inspection returned malformed JSON") from error
    info = response.get("package") if isinstance(response, dict) else None
    if (not isinstance(info, dict) or response.get("protocol") != PROTOCOL
            or response.get("simulation") is not False):
        raise ValueError("Updater inspection returned an unsupported response")
    if (info.get("kind") != kind or info.get("installable") is not True
            or info.get("file_sha256") != digest(before)
            or type(info.get("file_bytes")) is not int or info["file_bytes"] != len(before)):
        raise ValueError(f"{kind} package kind, integrity or transfer approval differs from its inspection")
    if kind == "thy666":
        build = info.get("build_id")
        if (not isinstance(build, str) or BUILD.fullmatch(build) is None
                or info.get("build_identity_verified") is not True
                or (expected_build is not None and build != expected_build)):
            raise ValueError("THY666 needs a checked full build; provide --expected-build for a renamed package")
    elif (digest(before) != STOCK_V5_SHA256 or info.get("build_id") is not None
          or info.get("build_identity_verified") is not False):
        raise ValueError("Stock package must be the exact allowlisted original v5 image without a THY666 build")
    if regular_bytes(path, PACKAGE_BYTES_MAX, f"{kind} package") != before:
        raise ValueError(f"{kind} source changed during inspection")
    return before, info


@dataclass(frozen=True)
class Release:
    source: Path
    data: bytes
    entry: dict


def release(updater: Path, source: Path, kind: str, expected_build: str | None) -> Release:
    data, info = inspect_package(updater, source, kind, expected_build)
    checksum = digest(data)
    build = info["build_id"]
    if kind == "thy666":
        name = f"THY666-{build}-{checksum[:16]}.333"
        identifier, label = "latest-thy666", f"THY666 build {build}"
    else:
        name = f"THY333-original-v5-{checksum[:16]}.333"
        identifier, label = "original-thy333", "Original THY333 v5"
    return Release(source, data, {"id": identifier, "label": label, "kind": kind,
                                  "file": name, "build_id": build, "sha256": checksum})


def existing_manifest(path: Path) -> bytes | None:
    if not os.path.lexists(path):
        return None
    return regular_bytes(path, MANIFEST_BYTES_MAX, "existing release catalog")


def publish_manifest(path: Path, data: bytes, previous: bytes | None) -> None:
    """Replace only the catalog, atomically, and preserve concurrent changes."""
    if existing_manifest(path) != previous:
        raise ValueError("Release catalog changed during preparation; existing catalog was preserved")
    if previous == data:
        return
    temporary = None
    try:
        with tempfile.NamedTemporaryFile("wb", prefix=".releases-", suffix=".tmp",
                                         dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if existing_manifest(path) != previous:
            raise ValueError("Release catalog changed during preparation; existing catalog was preserved")
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def prepare_releases(*, thy666: Path, stock: Path, output_dir: Path = DEFAULT_OUTPUT,
                     updater: Path = DEFAULT_UPDATER, expected_build: str | None = None) -> Path:
    if expected_build is not None and (not isinstance(expected_build, str)
                                       or BUILD.fullmatch(expected_build) is None):
        raise ValueError("Expected build must contain exactly 16 lowercase hexadecimal characters")
    updater = Path(updater).absolute()
    try:
        if not stat.S_ISREG(updater.lstat().st_mode):
            raise ValueError("Updater must be a built regular executable, not a link")
    except OSError as error:
        raise ValueError("Build the Rust updater first or supply --updater") from error
    inputs = [(Path(thy666).absolute(), "thy666", expected_build),
              (Path(stock).absolute(), "stock", None)]
    releases = [release(updater, path, kind, build) for path, kind, build in inputs]
    output = Path(output_dir).absolute()
    if os.path.lexists(output) and (output.is_symlink() or not output.is_dir()):
        raise ValueError("Output must be a directory, not a link or file")
    output.mkdir(parents=True, exist_ok=True)
    output = output.resolve()
    manifest_path = output / "releases.json"
    previous = existing_manifest(manifest_path)
    for item in releases:
        target = output / item.entry["file"]
        if os.path.lexists(target):
            if regular_bytes(target, PACKAGE_BYTES_MAX, "existing curated package") != item.data:
                raise ValueError("A curated package filename already contains different bytes; no file was overwritten")
        else:
            # Exclusive creation never overwrites an existing package. A failed
            # copy remains local evidence; it is never published in the catalog.
            try:
                with target.open("xb") as stream:
                    stream.write(item.data)
                    stream.flush()
                    os.fsync(stream.fileno())
            except OSError as error:
                raise ValueError("Could not create curated package; existing files were preserved") from error
        copied, _ = inspect_package(updater, target, item.entry["kind"], item.entry["build_id"])
        if copied != item.data:
            raise ValueError("Curated package changed during copy verification")
    # Recheck every copy and source immediately before publishing either entry.
    for item in releases:
        if (regular_bytes(output / item.entry["file"], PACKAGE_BYTES_MAX, "curated package") != item.data
                or regular_bytes(item.source, PACKAGE_BYTES_MAX, "source package") != item.data):
            raise ValueError("A package changed before catalog publication; existing catalog was preserved")
    manifest = {"format": "thy666.release-catalog", "version": 1,
                "releases": [item.entry for item in releases]}
    encoded = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
    publish_manifest(manifest_path, encoded, previous)
    return manifest_path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, epilog=(
        "Example: python tools/prepare_updater_releases.py --thy666 PATH/THY666-instrument-alpha-12345678.333 "
        "--stock PATH/THY333_v5.333\n"
        "The generated releases.json uses format thy666.release-catalog, version 1, and sibling .333 files. "
        "THY666 entries bind a full build and SHA-256; stock entries bind its exact known SHA-256. "
        "No original firmware is added to public source, and no hardware is accessed."))
    parser.add_argument("--thy666", type=Path, required=True, help="Explicit reviewed THY666 package; no latest-file search")
    parser.add_argument("--stock", type=Path, required=True, help="Explicit locally supplied original THY333 v5 package")
    parser.add_argument("--expected-build", help="Full 16-digit lowercase THY666 build, needed for a renamed package")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT, help="Ignored local output directory; default build/updater-target/release")
    parser.add_argument("--updater", type=Path, default=DEFAULT_UPDATER, help="Built native updater executable; default release executable")
    args = parser.parse_args()
    try:
        path = prepare_releases(**vars(args))
    except ValueError as error:
        parser.exit(1, f"Release preparation failed: {error}\n")
    except OSError:
        parser.exit(1, "Release preparation failed during a local file operation; the previous catalog was preserved.\n")
    print(f"Prepared {path.name} with two explicitly selected releases. No hardware was accessed.")


if __name__ == "__main__":
    main()
