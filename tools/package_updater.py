"""Package a verified native updater into ignored, deterministic download ZIPs.

Only the checked executable, project/dependency notices, quick start and an
optional explicit sibling release catalog are included. Package inspection uses
the executable's offline `inspect` command; no USB or browser command is run.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import stat
import subprocess
import tempfile
import zipfile

from build_updater import verify_artifact
from prepare_updater_releases import inspect_package, regular_bytes, PACKAGE_BYTES_MAX

ROOT = Path(__file__).resolve().parents[1]
MAX_EXECUTABLE = 32 * 1024 * 1024
MAX_NOTICE = 2 * 1024 * 1024
MAX_TOTAL = 64 * 1024 * 1024
PLATFORMS = {"x86_64-pc-windows-msvc": "windows-x64", "aarch64-apple-darwin": "macos-arm64",
             "x86_64-apple-darwin": "macos-x64", "x86_64-unknown-linux-gnu": "linux-x64",
             "aarch64-unknown-linux-gnu": "linux-arm64"}
SHA = re.compile(r"[0-9a-f]{64}\Z")
BUILD = re.compile(r"[0-9a-f]{16}\Z")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def object_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Repeated JSON field in packaging input")
        result[key] = value
    return result


def read_json(path: Path) -> tuple[bytes, dict]:
    raw = regular_bytes(path, 65536, "packaging manifest")
    try:
        value = json.loads(raw, object_pairs_hook=object_pairs)
    except (ValueError, UnicodeError) as error:
        raise ValueError("Malformed or repeated packaging JSON fields") from error
    if not isinstance(value, dict):
        raise ValueError("Packaging manifest must be an object")
    return raw, value


def safe_basename(name: str, *, package: bool = False) -> bool:
    if (not isinstance(name, str) or not name or len(name) > 255 or not name.isascii()
            or name.startswith(".") or name.endswith((" ", "."))
            or any(ord(char) < 32 or char in '\\/:*?"<>|' for char in name)):
        return False
    stem = name.split(".")[0].upper()
    reserved = {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)), *(f"LPT{i}" for i in range(1, 10))}
    return stem not in reserved and (not package or name.endswith(".333"))


def source_record(record: dict, limit: int) -> tuple[Path, bytes]:
    if (not isinstance(record, dict) or type(record.get("bytes")) is not int
            or not 0 < record["bytes"] <= limit or not isinstance(record.get("sha256"), str)
            or SHA.fullmatch(record["sha256"]) is None):
        raise ValueError("Build artifact requires a bounded size and exact SHA-256")
    name = record.get("output")
    if (not isinstance(name, str) or not name or "\\" in name or ":" in name
            or PureWindowsPath(name).drive or PurePosixPath(name).is_absolute()
            or any(part in (".", "..", "") for part in name.split("/"))):
        raise ValueError("Build artifact path must be canonical and repository-relative")
    path = ROOT / name
    resolved = path.resolve()
    if not resolved.is_relative_to(ROOT.resolve()) or not resolved.is_relative_to((ROOT / "build").resolve()):
        raise ValueError("Build artifact must remain in the ignored build directory")
    # Also reject links on the route to the file, not merely at its leaf.
    for component in (path, *path.parents):
        if component == ROOT:
            break
        if component.is_symlink():
            raise ValueError("Build artifact paths must not contain links")
    data = regular_bytes(path, limit, "verified build artifact")
    if len(data) != record["bytes"] or digest(data) != record["sha256"]:
        raise ValueError("Build artifact differs from its verified build manifest")
    return path, data


def catalog_files(executable: Path) -> tuple[dict[str, bytes], dict[Path, bytes]]:
    path = executable.parent / "releases.json"
    if not os.path.lexists(path):
        return {}, {}
    raw, catalog = read_json(path)
    if (set(catalog) != {"format", "version", "releases"}
            or catalog["format"] != "thy666.release-catalog"
            or type(catalog["version"]) is not int or catalog["version"] != 1
            or not isinstance(catalog["releases"], list) or len(catalog["releases"]) > 64):
        raise ValueError("Unsupported curated release catalog")
    included, snapshots, ids = {"releases.json": raw}, {path: raw}, set()
    for entry in catalog["releases"]:
        if (not isinstance(entry, dict) or not {"id", "label", "kind", "file", "sha256"}.issubset(entry)
                or set(entry) - {"id", "label", "kind", "file", "sha256", "build_id"}):
            raise ValueError("Malformed curated release entry")
        identifier, label, name, kind = entry["id"], entry["label"], entry["file"], entry["kind"]
        if (not isinstance(identifier, str) or re.fullmatch(r"[A-Za-z0-9_-]{1,64}", identifier) is None
                or identifier in ids or not isinstance(label, str) or not 0 < len(label) <= 128
                or len(label.encode()) > 256 or any(ord(char) < 32 or ord(char) == 127 for char in label)):
            raise ValueError("Curated release IDs and labels must be bounded and unique")
        ids.add(identifier)
        if (not safe_basename(name, package=True) or name.lower() in {key.lower() for key in included}
                or kind not in ("thy666", "stock") or not isinstance(entry["sha256"], str)
                or SHA.fullmatch(entry["sha256"]) is None):
            raise ValueError("Curated release needs a unique sibling basename, kind and exact SHA-256")
        build = entry.get("build_id")
        if (kind == "thy666" and (not isinstance(build, str) or BUILD.fullmatch(build) is None)) or (kind == "stock" and build is not None):
            raise ValueError("Curated firmware build expectation does not match its kind")
        source = executable.parent / name
        if source.resolve().parent != executable.parent.resolve():
            raise ValueError("Curated package leaves its catalog directory")
        data, info = inspect_package(executable, source, kind, build)
        if digest(data) != entry["sha256"] or info["file_sha256"] != entry["sha256"]:
            raise ValueError("Curated package differs from its reviewed catalog digest")
        included[name] = data
        snapshots[source] = data
    return included, snapshots


def quickstart(platform: str, version: str, releases: bool) -> bytes:
    executable = "thy666-updater.exe" if platform.startswith("windows") else "thy666-updater"
    text = f"""THY666 updater {version} - {platform}

1. Extract this entire ZIP into one folder. Keep the files together.
2. Keep the local Client server running at http://127.0.0.1:3330/.
3. Open {executable}. It starts the local helper and opens the Client's
   Update tab. Opening it does not detect, open or update a board automatically.
4. Follow the Update tab: detect the board, save configuration while the
   application can connect, choose firmware, then confirm installation.
5. Keep the helper open until the operation ends. Verify the running board
   afterward. A completed transfer is not a firmware readback or recovery test.

{"A curated release catalog and its listed firmware files are included." if releases else "No firmware catalog is included. Use the Update tab's custom package option."}
Never move releases.json away from its listed firmware files. Keep LICENSE and
THIRD_PARTY_LICENSES.txt with the updater.

This package has local Windows software/simulation validation. macOS/Linux
source builds and CI are prepared; end-to-end USB/update behavior on those
platforms is not qualified. Configuration exports are not raw-device backups.
Interrupted-update recovery and automatic rollback remain unqualified.

Original manufacturer firmware, when included from a local input, retains its
existing rights. This local bundle does not grant additional redistribution
rights to that firmware.
"""
    return text.encode("utf-8")


def check_private_paths(data: bytes) -> None:
    folded = data.lower()
    for prefix in {str(ROOT), ROOT.as_posix(), str(Path.home()), Path.home().as_posix()}:
        for encoding in ("utf-8", "utf-16le"):
            if prefix.encode(encoding).lower() in folded:
                raise ValueError("A packaged file retains a private local path")


def atomic_output(path: Path, data: bytes) -> None:
    allowed = [(ROOT / "build").resolve(), (ROOT / "web/generated").resolve()]
    directory = path.parent.resolve()
    if not directory.is_relative_to(ROOT.resolve()) or not any(directory.is_relative_to(base) for base in allowed):
        raise ValueError("Download outputs must remain in ignored build/generated directories")
    for component in (path, *path.parents):
        if component == ROOT:
            break
        if component.is_symlink():
            raise ValueError("Download output paths must not contain links")
    if os.path.lexists(path) and not stat.S_ISREG(path.lstat().st_mode):
        raise ValueError("Download destination must not be a link or directory")
    directory.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile("wb", prefix=".updater-", suffix=".tmp", dir=directory, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data); stream.flush(); os.fsync(stream.fileno())
        if temporary.parent.resolve() != directory or path.parent.resolve() != directory:
            raise ValueError("Download destination changed during packaging")
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def package_updater(manifest_path: Path | None = None) -> dict:
    manifest_path = Path(manifest_path) if manifest_path else ROOT / "build/updater-target/updater-build.json"
    raw_manifest, manifest = read_json(manifest_path)
    if (manifest.get("format") != "thy666.updater-build" or type(manifest.get("version")) is not int
            or manifest["version"] != 1 or manifest.get("device_access_performed") is not False
            or manifest.get("host") not in PLATFORMS):
        raise ValueError("A verified native updater build manifest is required")
    host = manifest["host"]; platform = PLATFORMS[host]
    executable, executable_data = source_record(manifest.get("artifact"), MAX_EXECUTABLE)
    binary_name = "thy666-updater.exe" if platform.startswith("windows") else "thy666-updater"
    if (executable.name != binary_name or manifest["artifact"].get("private_path_check") != "passed"
            or (platform.startswith("windows") and manifest["artifact"].get("static_msvc_runtime_checked") is not True)):
        raise ValueError("Build manifest lacks the required binary/path/runtime checks")
    verified = verify_artifact(executable, host)
    if any(verified.get(key) != manifest["artifact"].get(key) for key in ("bytes", "sha256", "private_path_check", "static_msvc_runtime_checked")):
        raise ValueError("Native executable verification differs from its build record")
    version_match = re.search(r'(?m)^version\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?)"', (ROOT / "updater/Cargo.toml").read_text(encoding="utf-8"))
    if version_match is None:
        raise ValueError("Updater source has no supported package version")
    version = version_match[1]
    try:
        reported = subprocess.run([str(executable), "--version"], check=True, capture_output=True, text=True,
                                  timeout=10, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    except (OSError, subprocess.SubprocessError) as error:
        raise ValueError("Offline executable version check failed") from error
    if reported.stdout.strip() != version:
        raise ValueError("Built executable version differs from its source package version")
    files = {binary_name: executable_data}; snapshots = {manifest_path: raw_manifest, executable: executable_data}
    notices = manifest.get("license_files")
    if not isinstance(notices, list) or len(notices) != 2:
        raise ValueError("Build manifest must identify both required license files")
    for notice in notices:
        path, data = source_record(notice, MAX_NOTICE)
        original = {"LICENSE": ROOT / "LICENSE", "THIRD_PARTY_LICENSES.txt": ROOT / "updater/THIRD_PARTY_LICENSES.txt"}.get(path.name)
        if original is None or path.parent.resolve() != executable.parent.resolve() or path.name in files:
            raise ValueError("Unexpected or duplicate release license file")
        if regular_bytes(original, MAX_NOTICE, "source license") != data:
            raise ValueError("Release license text differs from its maintained source")
        files[path.name] = data; snapshots[path] = data; snapshots[original] = data
    curated, sources = catalog_files(executable); files.update(curated); snapshots.update(sources)
    files["QUICKSTART.txt"] = quickstart(platform, version, "releases.json" in curated)
    if sum(map(len, files.values())) > MAX_TOTAL:
        raise ValueError("Updater bundle exceeds its bounded total size")
    for data in files.values():
        check_private_paths(data)
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            item = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0)); item.create_system = 3
            item.external_attr = (0o100755 if name == binary_name else 0o100644) << 16
            archive.writestr(item, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    packaged = output.getvalue()
    with zipfile.ZipFile(io.BytesIO(packaged)) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(files) or any(archive.read(name) != data for name, data in files.items()):
            raise ValueError("Generated updater ZIP failed byte-for-byte verification")
    # Recheck all sources after inspection/compression and before publishing.
    for source, expected in snapshots.items():
        if regular_bytes(source, max(len(expected), 1), "packaging source") != expected:
            raise ValueError("A packaging source changed; previous downloads were preserved")
    release = ROOT / "build/releases" / f"THY666-updater-{version}-{platform}.zip"
    download = ROOT / "web/generated/updater" / f"THY666-updater-{platform}.zip"
    atomic_output(release, packaged); atomic_output(download, packaged)
    return {"format": "thy666.updater-package", "version": version, "host": host,
            "release": release.relative_to(ROOT).as_posix(), "download": download.relative_to(ROOT).as_posix(),
            "bytes": len(packaged), "sha256": digest(packaged), "files": sorted(files),
            "device_access_performed": False}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-manifest", type=Path, help="Explicit verified local updater build manifest")
    args = parser.parse_args()
    try:
        result = package_updater(args.build_manifest)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Updater packaging failed: {error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
