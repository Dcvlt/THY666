"""Read only the verified portable updater assets for site/desktop packaging."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

from build_browser_updater import SOURCES, validate

ROOT = Path(__file__).resolve().parents[1]


def checked_bytes(path: Path, limit: int) -> bytes:
    if not path.resolve().is_relative_to(ROOT.resolve()):
        raise ValueError("Browser updater input leaves the workspace")
    for parent in (path, *path.parents):
        if parent == ROOT:
            break
        if parent.is_symlink():
            raise ValueError("Browser updater inputs must not contain links")
    if not path.is_file() or path.stat().st_size > limit:
        raise ValueError("Build the bounded browser updater before packaging")
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError("Browser updater input exceeded its size limit")
    return data


def browser_core_assets() -> dict[str, bytes]:
    generated = ROOT / "web/generated"
    raw = checked_bytes(generated / "browser-updater.json", 65536)
    manifest = json.loads(raw)
    wasm = checked_bytes(generated / "browser-updater.wasm", 4 * 1024 * 1024)
    artifact = manifest.get("artifact", {})
    if (manifest.get("format") != "thy666.browser-updater" or manifest.get("version") != 1
            or manifest.get("abi") != 1 or artifact.get("bytes") != len(wasm)
            or artifact.get("sha256") != hashlib.sha256(wasm).hexdigest()):
        raise ValueError("Browser updater differs from its build manifest")
    validate(wasm)
    sources = manifest.get("source_sha256", {})
    if set(sources) != set(SOURCES):
        raise ValueError("Browser updater has an incomplete source identity")
    for name, expected in sources.items():
        if hashlib.sha256(checked_bytes(ROOT / name, 2 * 1024 * 1024)).hexdigest() != expected:
            raise ValueError("Browser updater sources changed; rebuild before packaging")
    notices = checked_bytes(ROOT / "updater/THIRD_PARTY_LICENSES.txt", 1024 * 1024)
    lock_hash = hashlib.sha256(checked_bytes(ROOT / "updater/Cargo.lock", 1024 * 1024)).hexdigest()
    if f"Cargo.lock SHA-256: {lock_hash}\n".encode() not in notices:
        raise ValueError("Updater dependency notices do not match the locked packages")
    return {"browser-updater.wasm": wasm, "browser-updater.json": raw,
            "THIRD_PARTY_LICENSES.txt": notices}
