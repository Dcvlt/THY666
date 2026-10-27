"""Stage the desktop's bounded public web assets and optional reviewed releases.

This does not run a native app, discover USB or change the device. Firmware
resources remain ignored local inputs with their original redistribution terms.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import tempfile

from package_updater import catalog_files

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
GENERATED = ("core.wasm", "chords.runtime.json", "oled-profile.json", "build-info.json", "menu-schema.js")
MAX_ASSET = 16 * 1024 * 1024


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def bounded_file(path: Path, *, parent: Path, limit: int = MAX_ASSET) -> bytes:
    path, parent = Path(path), Path(parent).resolve()
    if not path.resolve().is_relative_to(parent):
        raise ValueError("Desktop input leaves its public source directory")
    for item in (path, *path.parents):
        if item.resolve() == parent:
            break
        if item.is_symlink():
            raise ValueError("Desktop input paths must not contain links")
    if not path.is_file() or path.stat().st_size > limit:
        raise ValueError("Desktop input is missing, non-regular or oversized")
    with path.open("rb") as source:
        data = source.read(limit + 1)
    if len(data) > limit:
        raise ValueError("Desktop input grew beyond its limit")
    return data


def web_assets(*, compile_check: bool = False) -> dict[str, bytes]:
    """Use a small explicit generated allowlist, never web/generated wholesale."""
    if compile_check:
        return {"index.html": b'<!doctype html><html lang="en"><meta charset="utf-8"><title>THY666 compile check only</title><body>Compile-check fixture. This is not the Companion release.</body></html>\n'}
    paths = [WEB / "index.html"]
    paths += sorted(path for path in WEB.iterdir() if path.suffix in (".js", ".css") and path.is_file())
    paths += [WEB / "generated" / name for name in GENERATED]
    paths += sorted(path for path in (WEB / "assets").rglob("*") if path.is_file())
    files = {}
    for path in paths:
        relative = path.relative_to(WEB).as_posix()
        if any(part.startswith(".") for part in path.relative_to(WEB).parts):
            raise ValueError("Private files must not be staged as desktop assets")
        files[relative] = bounded_file(path, parent=WEB)
    # Browser mode uses the same Rust policy; native desktop still selects its
    # native backend. Validate this optional pair before including either file.
    browser_files = [WEB / "generated" / name for name in ("browser-updater.wasm", "browser-updater.json")]
    if any(path.exists() for path in browser_files):
        from browser_updater_assets import browser_core_assets
        files.update({"generated/" + name: data for name, data in browser_core_assets().items()})
    build = json.loads(files["generated/build-info.json"])
    if build.get("artifacts", {}).get("webassembly", {}).get("sha256") != digest(files["generated/core.wasm"]):
        raise ValueError("Desktop WebAssembly differs from the recorded core build")
    for relative, expected in build.get("source_sha256", {}).items():
        source = ROOT / relative
        if not source.resolve().is_relative_to(ROOT.resolve()) or digest(bounded_file(source, parent=ROOT)) != expected:
            raise ValueError("Core sources changed since the generated WebAssembly build")
    fonts = json.loads(files["assets/fonts/manifest.json"])
    for face in fonts["fonts"]:
        for asset in face["files"]:
            if digest(files["assets/fonts/" + asset["path"]]) != asset["sha256"]:
                raise ValueError("Desktop font differs from its recorded licensed source")
        if "assets/fonts/" + face["license_path"] not in files:
            raise ValueError("A desktop font is missing its license")
    return files


def replace_stage(destination: Path, files: dict[str, bytes]) -> list[dict]:
    """Replace only a checked generated child of this workspace's build folder."""
    destination = Path(destination)
    build = (ROOT / "build").resolve()
    if destination.is_symlink() or destination.resolve() == build or not destination.resolve().is_relative_to(build):
        raise ValueError("Desktop staging output must be a child of the workspace build directory")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="desktop-stage-", dir=destination.parent) as temporary:
        staging = Path(temporary) / "ready"
        staging.mkdir()
        records = []
        for name, data in sorted(files.items()):
            target = staging / name
            if ("\\" in name or ":" in name or PurePosixPath(name).is_absolute() or PureWindowsPath(name).drive
                    or any(part in (".", "..", "") for part in name.split("/"))
                    or not target.resolve().is_relative_to(staging.resolve()) or target.resolve() == staging.resolve()):
                raise ValueError("Unsafe desktop asset name")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            records.append({"file": name, "bytes": len(data), "sha256": digest(data)})
        previous = Path(temporary) / "previous"
        if destination.exists():
            destination.rename(previous)
        try:
            staging.rename(destination)
        except OSError:
            if previous.exists():
                previous.rename(destination)
            raise
    return records


def prepare(*, compile_check: bool = False, updater: Path | None = None, with_releases: bool = True) -> dict:
    files = web_assets(compile_check=compile_check)
    resources = {"README.txt": b"Optional reviewed firmware packages are staged here. Original redistribution rights remain unchanged.\n"}
    if not compile_check and with_releases:
        updater = updater or ROOT / "build/updater-target/release" / ("thy666-updater.exe" if os.name == "nt" else "thy666-updater")
        if (updater.parent / "releases.json").is_file():
            releases, _ = catalog_files(updater)
            resources.update(releases)
    result = {"format": "thy666.desktop-assets", "version": 1, "compile_check_only": compile_check,
              "hardware_access_performed": False,
              "web": replace_stage(ROOT / "build/desktop-web", files),
              "resources": replace_stage(ROOT / "build/desktop-resources", resources)}
    (ROOT / "build/desktop-assets.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compile-check", action="store_true", help="Stage a labelled static fixture, never a usable Companion release")
    parser.add_argument("--without-releases", action="store_true", help="Do not include local firmware catalog/packages")
    parser.add_argument("--updater", type=Path, help="Verified offline inspector beside the optional release catalog")
    args = parser.parse_args()
    result = prepare(compile_check=args.compile_check, updater=args.updater, with_releases=not args.without_releases)
    print(f"Desktop assets: {len(result['web'])} files; resources: {len(result['resources'])} files; compile check only: {result['compile_check_only']}")


if __name__ == "__main__":
    main()
