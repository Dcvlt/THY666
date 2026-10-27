"""Build the portable Rust updater policy for an explicitly permissioned browser.

This compiles package validation and the shared pull session only. It never
opens USB or packages firmware, settings, captures or private local bindings.
Install the official rustup wasm32-unknown-unknown target before the first build.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from build_updater import build_environment, existing_flags

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/browser-updater"
TARGET = "wasm32-unknown-unknown"
SOURCES = ("updater/Cargo.toml", "updater/Cargo.lock", "updater/src/lib.rs",
           "updater/src/package.rs", "updater/src/protocol.rs", "updater/src/session.rs",
           "updater/src/browser.rs", "updater/src/backup.rs", "tools/build_browser_updater.py")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def leb(data: bytes, cursor: int) -> tuple[int, int]:
    result = shift = 0
    for _ in range(5):
        if cursor >= len(data):
            raise ValueError("Truncated WebAssembly section")
        byte = data[cursor]
        cursor += 1
        result |= (byte & 127) << shift
        if byte < 128:
            return result, cursor
        shift += 7
    raise ValueError("Oversized WebAssembly section")


def validate(data: bytes) -> None:
    if len(data) > 4 * 1024 * 1024 or data[:8] != b"\0asm\x01\0\0\0":
        raise ValueError("Browser updater is not a bounded WebAssembly module")
    cursor = 8
    while cursor < len(data):
        kind = data[cursor]
        size, cursor = leb(data, cursor + 1)
        end = cursor + size
        if end > len(data):
            raise ValueError("WebAssembly section leaves the artifact")
        if kind == 2:
            count, _ = leb(data, cursor)
            if count:
                raise ValueError("Browser updater must not import host capabilities")
        cursor = end
    if re.search(rb"(?i)\b[A-Z]:[\\/]+Users[\\/]", data) or str(ROOT).encode() in data:
        raise ValueError("Browser updater contains a private local build path")
    for name in (b"thy_update_abi_version", b"thy_update_input_ptr", b"thy_update_input_capacity",
                 b"thy_update_request", b"thy_update_output_ptr", b"thy_update_reset"):
        if name not in data:
            raise ValueError("Browser updater is missing a required ABI export")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    cargo = shutil.which("cargo")
    rustc = os.environ.get("RUSTC") or shutil.which("rustc")
    if not cargo or not rustc:
        parser.error("Rust and Cargo are required")
    compiler = subprocess.check_output([rustc, "--version", "--verbose"], text=True, cwd=ROOT)
    host = re.search(r"(?m)^host: (\S+)$", compiler)
    if host is None:
        parser.error("Unable to identify the Rust toolchain")
    environment = build_environment(release=False, host=host[1])
    environment["CARGO_TARGET_DIR"] = str(OUTPUT / "target")
    flags = existing_flags(environment, "RUSTFLAGS")
    flags += ["-C", "panic=abort", "-C", "link-arg=--max-memory=16777216",
              "-C", "link-arg=-zstack-size=262144"]
    environment["CARGO_ENCODED_RUSTFLAGS"] = "\x1f".join(flags)
    command = [cargo, "rustc", "--manifest-path", "updater/Cargo.toml", "--locked",
               "--lib", "--crate-type", "cdylib", "--release", "--target", TARGET]
    if args.offline:
        command.append("--offline")
    subprocess.run(command, cwd=ROOT, env=environment, check=True)
    source = OUTPUT / "target" / TARGET / "release/thy666_updater.wasm"
    data = source.read_bytes()
    validate(data)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    artifact = {"file": "updater-core.wasm", "bytes": len(data), "sha256": sha(data)}
    hashes = {name: sha((ROOT / name).read_bytes()) for name in SOURCES}
    manifest = {"format": "thy666.browser-updater-build", "version": 1, "abi": 1,
                "rustc": compiler.splitlines()[0], "target": TARGET,
                "device_access_performed": False, "artifact": artifact, "source_sha256": hashes}
    (OUTPUT / "updater-core.wasm").write_bytes(data)
    (OUTPUT / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    generated = ROOT / "web/generated"
    generated.mkdir(parents=True, exist_ok=True)
    (generated / "browser-updater.wasm").write_bytes(data)
    public = {**manifest, "format": "thy666.browser-updater",
              "artifact": {**artifact, "file": "browser-updater.wasm"}}
    (generated / "browser-updater.json").write_text(json.dumps(public, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"artifact": artifact, "abi": 1, "imports": 0, "device_access_performed": False}))


if __name__ == "__main__":
    main()
