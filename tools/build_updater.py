"""Build/test the native Rust updater offline from hardware, using local caches.

Cargo downloads locked dependencies on the first run unless --offline is set.
This never runs the updater, enumerates USB interfaces, or installs toolchains.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CARGO_CACHE = ROOT / ".tools/cargo"
TARGET = ROOT / "build/updater-target"
MINIMUM_RUST = (1, 89, 0)


def existing_flags(environment: dict[str, str], name: str) -> list[str]:
    """Preserve Cargo's encoded-first, whitespace-split environment semantics."""
    encoded = environment.get("CARGO_ENCODED_" + name)
    if encoded is not None:
        return encoded.split("\x1f") if encoded else []
    return environment.get(name, "").split()


def build_environment(*, release: bool, host: str) -> dict[str, str]:
    environment = os.environ.copy()
    # Do not silently reinterpret a caller's cross-compilation configuration.
    if environment.get("CARGO_BUILD_TARGET", host) != host:
        raise ValueError("This wrapper builds the native host; use Cargo directly for cross-compilation")
    temporary = ROOT / ".tools/updater-temp"
    for directory in (CARGO_CACHE, TARGET, temporary):
        directory.mkdir(parents=True, exist_ok=True)
    environment["CARGO_HOME"] = str(CARGO_CACHE)
    environment["CARGO_TARGET_DIR"] = str(TARGET)
    for key in ("TEMP", "TMP", "TMPDIR"):
        environment[key] = str(temporary)
    # Broad remaps precede specific ones: rustc uses the last matching prefix.
    prefixes = [
        (Path.home(), "/user"),
        (Path(os.environ.get("CARGO_HOME", Path.home() / ".cargo")), "/cargo"),
        (Path(os.environ.get("RUSTUP_HOME", Path.home() / ".rustup")), "/rustup"),
        (ROOT, "/thy666"),
        (CARGO_CACHE, "/cargo"),
    ]
    remaps = []
    for path, replacement in prefixes:
        for variant in dict.fromkeys((str(path.resolve()), path.resolve().as_posix())):
            remaps.append(f"--remap-path-prefix={variant}={replacement}")
    for name in ("RUSTFLAGS", "RUSTDOCFLAGS"):
        flags = existing_flags(environment, name) + remaps
        if name == "RUSTFLAGS" and release and host.endswith("-windows-msvc"):
            # hidapi's pinned C build also selects /MT from crt-static.
            flags += ["-C", "target-feature=+crt-static"]
        environment["CARGO_ENCODED_" + name] = "\x1f".join(flags)
    return environment


def pe_imports(data: bytes) -> list[str]:
    """Inspect the import directory of a compiler-produced PE32/PE32+ image."""
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError("Release executable is not a PE image")
    pe = struct.unpack_from("<I", data, 60)[0]
    if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Invalid PE header")
    sections, optional_bytes = struct.unpack_from("<H12xH", data, pe + 6)
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    directories = {0x10B: 96, 0x20B: 112}.get(magic)
    if directories is None or optional_bytes < directories + 16:
        raise ValueError("Missing PE import directory")
    import_rva, import_bytes = struct.unpack_from("<II", data, optional + directories + 8)
    table = optional + optional_bytes
    if table + sections * 40 > len(data):
        raise ValueError("PE section table leaves the image")

    def file_offset(rva: int, size: int) -> int:
        for index in range(sections):
            virtual_bytes, address, raw_bytes, offset = struct.unpack_from("<IIII", data, table + index * 40 + 8)
            if address <= rva and rva - address < max(virtual_bytes, raw_bytes):
                relative = rva - address
                if relative + size > raw_bytes or offset + relative + size > len(data):
                    break
                return offset + relative
        raise ValueError("PE import data leaves its section")

    if not import_rva:
        return []
    if import_bytes < 20:
        raise ValueError("Truncated PE import directory")
    names = []
    for index in range(import_bytes // 20):
        offset = file_offset(import_rva + index * 20, 20)
        descriptor = struct.unpack_from("<5I", data, offset)
        if not any(descriptor):
            return sorted(set(names))
        name_offset = file_offset(descriptor[3], 1)
        end = data.find(b"\0", name_offset, min(len(data), name_offset + 256))
        if end < 0:
            raise ValueError("Unterminated PE import name")
        names.append(data[name_offset:end].decode("ascii"))
    raise ValueError("PE import directory has no terminating descriptor")


def verify_artifact(path: Path, host: str) -> dict:
    data = path.read_bytes()
    private_prefixes = {str(ROOT), ROOT.as_posix(), str(Path.home()), Path.home().as_posix()}
    folded = data.lower()
    for prefix in private_prefixes:
        for encoding in ("utf-8", "utf-16le"):
            if prefix.encode(encoding).lower() in folded:
                raise ValueError("Release executable retains a private build path; do not distribute it")
    imports = pe_imports(data) if host.endswith("-windows-msvc") else []
    if any(re.match(r"(?i)(?:vcruntime|msvcp|msvcr|ucrtbase|api-ms-win-crt)", name) for name in imports):
        raise ValueError("Windows release imports a dynamic C runtime; static runtime verification failed")
    return {"output": path.relative_to(ROOT).as_posix(), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(), "private_path_check": "passed",
            "windows_imports": imports, "static_msvc_runtime_checked": host.endswith("-windows-msvc")}


def copy_release_notices(executable: Path) -> list[dict]:
    """Retain exact project/dependency notices beside a validated executable."""
    bundle = ROOT / "updater/THIRD_PARTY_LICENSES.txt"
    if not bundle.is_file() or not (ROOT / "LICENSE").is_file():
        raise ValueError("Project and updater dependency license files are required for a release")
    identity = hashlib.sha256((ROOT / "updater/Cargo.lock").read_bytes()).hexdigest()
    if f"Cargo.lock SHA-256: {identity}\n".encode() not in bundle.read_bytes():
        raise ValueError("Refresh the updater license bundle from the exact Cargo.lock packages before packaging")
    notices = []
    for source in (ROOT / "LICENSE", bundle):
        destination = executable.parent / source.name
        shutil.copyfile(source, destination)
        data = destination.read_bytes()
        if data != source.read_bytes():
            raise ValueError("Copied release license text differs from its source")
        notices.append({"output": destination.relative_to(ROOT).as_posix(),
                        "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    return notices


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--test", action="store_true", help="Run synthetic tests before the release build")
    group.add_argument("--test-only", action="store_true", help="Run synthetic tests without a release build")
    parser.add_argument("--offline", action="store_true", help="Require dependencies already in the workspace Cargo cache")
    parser.add_argument("--cargo", help="Explicit installed Cargo executable; defaults to PATH")
    args = parser.parse_args()
    cargo = args.cargo or shutil.which("cargo")
    rustc = os.environ.get("RUSTC") or shutil.which("rustc")
    if not cargo or not rustc:
        parser.error("Install Rust and the platform C build tools, then make cargo and rustc available")
    rust_info = subprocess.check_output([rustc, "--version", "--verbose"], cwd=ROOT, text=True)
    release = re.search(r"(?m)^release: (\d+)\.(\d+)\.(\d+)", rust_info)
    host = re.search(r"(?m)^host: (\S+)$", rust_info)
    if release is None or host is None or tuple(map(int, release.groups())) < MINIMUM_RUST:
        parser.error("Rust 1.89 or newer is required; this is not a claim that the minimum was qualified")
    host = host[1]
    common = ["--manifest-path", "updater/Cargo.toml", "--locked"]
    if args.offline:
        common.append("--offline")
    if args.test or args.test_only:
        subprocess.run([cargo, "test", *common], cwd=ROOT,
                       env=build_environment(release=False, host=host), check=True)
    if args.test_only:
        return
    subprocess.run([cargo, "build", "--release", *common], cwd=ROOT,
                   env=build_environment(release=True, host=host), check=True)
    platform_dir = TARGET / host if os.environ.get("CARGO_BUILD_TARGET") else TARGET
    executable = platform_dir / "release" / ("thy666-updater.exe" if "windows" in host else "thy666-updater")
    manifest = {"format": "thy666.updater-build", "version": 1, "host": host,
                "rustc": rust_info.splitlines()[0],
                "cargo": subprocess.check_output([cargo, "--version"], cwd=ROOT, text=True).strip(),
                "tests_run": bool(args.test), "device_access_performed": False,
                "artifact": verify_artifact(executable, host)}
    manifest["license_files"] = copy_release_notices(executable)
    (TARGET / "updater-build.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
