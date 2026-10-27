"""Build the bundled Tauri desktop app without opening USB or launching it.

Full builds stage the public Client and generated portable core assets. --check uses a clearly
labelled compile-only frontend fixture and never produces a distributable app.
The wrapper uses the exact Cargo.lock, workspace caches and private-path remaps.
"""
from __future__ import annotations

import argparse
import io
import json
import os
import plistlib
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tomllib
import zipfile

from build_updater import existing_flags, pe_imports
from package_updater import atomic_output, check_private_paths, PLATFORMS
from prepare_desktop import prepare, bounded_file, digest, replace_stage

ROOT = Path(__file__).resolve().parents[1]
CARGO_CACHE = ROOT / ".tools/cargo"
TARGET = ROOT / "build/desktop-target"


def native_source_identities() -> dict[str, str]:
    paths = [ROOT / "desktop" / name for name in ("Cargo.toml", "Cargo.lock", "build.rs", "tauri.conf.json")]
    paths += [ROOT / "updater" / name for name in ("Cargo.toml", "Cargo.lock")]
    for folder, pattern in (("desktop/src", "*.rs"), ("desktop/capabilities", "*.json"),
                            ("desktop/permissions", "*.toml"), ("desktop/icons", "*"), ("updater/src", "*.rs")):
        paths += sorted(path for path in (ROOT / folder).rglob(pattern) if path.is_file())
    return {path.relative_to(ROOT).as_posix(): digest(bounded_file(path, parent=ROOT)) for path in paths}


def build_environment(host: str) -> dict[str, str]:
    environment = os.environ.copy()
    if environment.get("CARGO_BUILD_TARGET", host) != host:
        raise ValueError("The desktop wrapper builds its native host; cross builds need explicit Cargo setup")
    temporary = ROOT / ".tools/desktop-temp"
    for directory in (CARGO_CACHE, TARGET, temporary):
        directory.mkdir(parents=True, exist_ok=True)
    environment.update(CARGO_HOME=str(CARGO_CACHE), CARGO_TARGET_DIR=str(TARGET), CARGO_INCREMENTAL="0")
    environment.setdefault("CARGO_BUILD_JOBS", "4")
    environment.update({name: str(temporary) for name in ("TEMP", "TMP", "TMPDIR")})
    remaps = []
    for path, replacement in [(Path.home(), "/user"),
                              (Path(os.environ.get("RUSTUP_HOME", Path.home() / ".rustup")), "/rustup"),
                              (ROOT, "/thy666"), (CARGO_CACHE, "/cargo")]:
        for name in dict.fromkeys((str(path.resolve()), path.resolve().as_posix())):
            remaps.append(f"--remap-path-prefix={name}={replacement}")
    for name in ("RUSTFLAGS", "RUSTDOCFLAGS"):
        environment["CARGO_ENCODED_" + name] = "\x1f".join(existing_flags(environment, name) + remaps)
    return environment


def verify_artifact(path: Path, host: str) -> dict:
    data = bounded_file(path, parent=ROOT / "build", limit=128 * 1024 * 1024)
    check_private_paths(data)
    imports = pe_imports(data) if host.endswith("windows-msvc") else []
    runtimes = [name for name in imports if re.match(r"(?i)(?:vcruntime|msvcp|msvcr|ucrtbase|api-ms-win-crt)", name)]
    return {"output": path.relative_to(ROOT).as_posix(), "bytes": len(data), "sha256": digest(data),
            "private_path_check": "passed", "windows_imports": imports,
            "windows_runtime_imports": runtimes}


def dependency_notices(lock: Path, cache: Path) -> bytes:
    """Retain exact source notices and hash-bound upstream license supplements."""
    lock_bytes = lock.read_bytes()
    packages = tomllib.loads(lock_bytes.decode("utf-8"))["package"]
    supplement_root = ROOT / "docs/licenses/desktop"
    supplement_manifest = supplement_root / "manifest.json"
    supplements = json.loads(supplement_manifest.read_text(encoding="utf-8"))["packages"] if supplement_manifest.is_file() else {}
    lines = ["THY666 desktop third-party notices", "Bundle format: THY666-DESKTOP-LICENSES/1",
             "Cargo.lock SHA-256: " + digest(lock_bytes), "",
             "Scope: all locked registry packages, including build-only and target-specific dependencies.",
             "Not every listed package is linked on every OS. Original alternative license texts remain",
             "alternatives. Native system libraries, fonts and vendor firmware retain separate terms.",
             "Upstream texts omitted by some crate archives are retained from the recorded revisions.", ""]
    texts: dict[str, tuple[str, bytes]] = {}
    for package in sorted(packages, key=lambda item: (item["name"], item["version"])):
        if not package.get("source", "").startswith("registry+"):
            continue
        name, version = package["name"], package["version"]
        candidates = list((cache / "registry/src").glob(f"*/{name}-{version}"))
        if len(candidates) != 1:
            raise ValueError(f"Fetch the exact locked dependency before licensing: {name} {version}")
        source = candidates[0]
        metadata = tomllib.loads((source / "Cargo.toml").read_text(encoding="utf-8"))["package"]
        expression = metadata.get("license", "See source license file")
        notices = sorted(path for path in source.rglob("*") if path.is_file()
                         and re.match(r"(?i)^(?:licen[cs]e|copying|copyright|notice|authors)(?:[._-].*)?$", path.name))
        if metadata.get("license-file"):
            explicit = source / metadata["license-file"]
            if explicit.resolve().is_relative_to(source.resolve()) and explicit.is_file() and explicit not in notices:
                notices.append(explicit)
        supplement = supplements.get(f"{name}@{version}")
        if not notices and supplement is None:
            raise ValueError(f"Locked dependency has no source-shipped license notice: {name} {version}")
        lines += [f"Package: {name} {version}", f"Declared license: {expression}",
                  f"Cargo registry checksum (SHA-256): {package.get('checksum', 'not recorded')}"]
        if metadata.get("repository"):
            lines.append("Source repository: " + metadata["repository"])
        if metadata.get("authors"):
            lines.append("Upstream declared authors: " + "; ".join(metadata["authors"]))
        checksum_file = source / ".cargo-checksum.json"
        checksums = json.loads(checksum_file.read_text(encoding="utf-8"))["files"] if checksum_file.is_file() else None
        archived = {}
        if checksums is None:
            crate_file = cache / "registry/cache" / source.parent.name / f"{name}-{version}.crate"
            crate_bytes = bounded_file(crate_file, parent=cache, limit=128 * 1024 * 1024)
            if digest(crate_bytes) != package.get("checksum"):
                raise ValueError(f"Cached dependency differs from its Cargo.lock checksum: {name} {version}")
            with tarfile.open(fileobj=io.BytesIO(crate_bytes), mode="r:gz") as archive:
                for notice in notices:
                    relative = notice.relative_to(source).as_posix()
                    member = archive.getmember(f"{name}-{version}/{relative}")
                    if not member.isfile() or member.size > 2 * 1024 * 1024:
                        raise ValueError("Dependency license archive entry must be a bounded regular file")
                    stream = archive.extractfile(member)
                    if stream is None:
                        raise ValueError("Missing dependency license archive entry")
                    archived[relative] = stream.read(2 * 1024 * 1024 + 1)
        for notice in sorted(notices):
            data = bounded_file(notice, parent=source, limit=2 * 1024 * 1024)
            relative = notice.relative_to(source).as_posix()
            if (checksums is not None and checksums.get(relative) != digest(data)) or (checksums is None and archived.get(relative) != data):
                raise ValueError(f"Dependency license differs from its Cargo source checksum: {name} {version}")
            identity = digest(data)
            if identity not in texts:
                texts[identity] = (f"TEXT-{len(texts) + 1:03}", data)
            lines.append(f"  {name}-{version}/{relative} -> {texts[identity][0]}")
        if supplement is not None:
            if supplement.get("crate_sha256") != package.get("checksum"):
                raise ValueError(f"Upstream license supplement differs from locked dependency: {name} {version}")
            lines.append("Upstream source revision: " + supplement["revision"])
            for item in supplement["files"]:
                if Path(item["file"]).name != item["file"] or ":" in item["file"] or "\\" in item["file"]:
                    raise ValueError("Unsafe upstream license supplement path")
                data = bounded_file(supplement_root / item["file"], parent=supplement_root, limit=2 * 1024 * 1024)
                if digest(data) != item["sha256"]:
                    raise ValueError("Upstream license supplement differs from its recorded digest")
                identity = digest(data)
                if identity not in texts:
                    texts[identity] = (f"TEXT-{len(texts) + 1:03}", data)
                lines.append(f"  {item['url']} -> {texts[identity][0]}")
        lines.append("")
    output = ("\n".join(lines) + "\nORIGINAL NOTICE TEXTS\n=====================\n").encode()
    for identity, (label, data) in texts.items():
        output += f"\n{label} | SHA-256 {identity}\n".encode() + data + b"\n"
    if any(package["name"] == "thy666-updater" for package in packages):
        updater_lock = (ROOT / "updater/Cargo.lock").read_bytes()
        updater_notices = bounded_file(ROOT / "updater/THIRD_PARTY_LICENSES.txt", parent=ROOT, limit=4 * 1024 * 1024)
        if f"Cargo.lock SHA-256: {digest(updater_lock)}\n".encode() not in updater_notices:
            raise ValueError("The shared updater license bundle does not match its lockfile")
        output += b"\nSHARED UPDATER AND HIDAPI PLATFORM ATTRIBUTIONS\n=============================================\n" + updater_notices
    check_private_paths(output)
    return output


def quickstart(version: str, runtime_imports: list[str]) -> bytes:
    text = f"""THY666 desktop {version}

1. Extract this entire ZIP and keep the files together.
2. Open thy666-desktop.exe. The UI is bundled; no Python, web server, browser
   helper process or session token is needed.
3. Connect board opens a native device selector. Opening the app never connects
   or writes to a board automatically. Firmware installation needs the explicit
   confirmation in the Update wizard.

Windows requires the Microsoft Edge WebView2 runtime. If Windows reports a
missing Visual C++ runtime, install Microsoft's supported redistributable.
Recorded C runtime imports: {', '.join(runtime_imports) or 'none'}.

To test without physical USB: thy666-desktop.exe --simulate
   Simulation does not prove physical installation or recovery.

The optional resources/ folder contains a reviewed local release catalog and
firmware packages. Original vendor firmware retains its existing rights; this
local bundle does not grant redistribution permission. Configuration backups
are not full flash dumps. Interrupted-update recovery and rollback remain
unqualified. Keep LICENSE and THIRD_PARTY_LICENSES.txt with the app.

Windows is the initial build target. macOS/Linux source and CI preparation are
separate from runtime USB and distribution qualification on those platforms.
"""
    return text.encode("utf-8")


def package_desktop(executable: Path, host: str, assets: dict, notices: bytes) -> dict:
    if assets.get("compile_check_only") is not False:
        raise ValueError("A compile-check fixture cannot become a desktop release")
    if not host.endswith("windows-msvc"):
        raise ValueError("Portable ZIP packaging currently targets Windows; native macOS/Linux bundling is not qualified")
    config = tomllib.loads((ROOT / "desktop/Cargo.toml").read_text(encoding="utf-8"))
    version = config["package"]["version"]
    if host not in PLATFORMS or not re.fullmatch(r"\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?", version):
        raise ValueError("Unsupported desktop release host or version")
    artifact = verify_artifact(executable, host)
    executable_bytes = bounded_file(executable, parent=ROOT / "build", limit=128 * 1024 * 1024)
    if len(executable_bytes) != artifact["bytes"] or digest(executable_bytes) != artifact["sha256"]:
        raise ValueError("Desktop executable changed after verification")
    files = {executable.name: executable_bytes, "LICENSE": (ROOT / "LICENSE").read_bytes(),
             "THIRD_PARTY_LICENSES.txt": notices,
             "QUICKSTART.txt": quickstart(version, artifact["windows_runtime_imports"])}
    font_licenses = b"THY666 desktop bundled font licenses\n\n"
    font_manifest = json.loads(bounded_file(ROOT / "build/desktop-web/assets/fonts/manifest.json", parent=ROOT / "build/desktop-web"))
    for face in font_manifest["fonts"]:
        notice = bounded_file(ROOT / "build/desktop-web/assets/fonts" / face["license_path"], parent=ROOT / "build/desktop-web")
        font_licenses += (face["family"] + "\n" + "=" * len(face["family"]) + "\n").encode() + notice + b"\n\n"
    files["FONT_LICENSES.txt"] = font_licenses
    for row in assets["resources"]:
        data = bounded_file(ROOT / "build/desktop-resources" / row["file"], parent=ROOT / "build/desktop-resources")
        if len(data) != row["bytes"] or digest(data) != row["sha256"]:
            raise ValueError("Desktop resource changed after staging")
        files["resources/" + row["file"]] = data
    for row in assets["web"]:
        staged = bounded_file(ROOT / "build/desktop-web" / row["file"], parent=ROOT / "build/desktop-web")
        if len(staged) != row["bytes"] or digest(staged) != row["sha256"]:
            raise ValueError("Desktop web asset changed after compilation")
    for data in files.values():
        check_private_paths(data)
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = (0o100755 if name == executable.name else 0o100644) << 16
            archive.writestr(entry, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    data = stream.getvalue()
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(files) or any(archive.read(name) != value for name, value in files.items()):
            raise ValueError("Desktop ZIP failed verification")
    if bounded_file(executable, parent=ROOT / "build", limit=128 * 1024 * 1024) != executable_bytes:
        raise ValueError("Desktop executable changed during packaging")
    output = ROOT / "build/releases" / f"THY666-desktop-{version}-{PLATFORMS[host]}.zip"
    atomic_output(output, data)
    return {"file": output.relative_to(ROOT).as_posix(), "bytes": len(data), "sha256": digest(data), "files": sorted(files), "artifact": artifact}


def macos_development_bundle(executable: Path, assets: dict, notices: bytes) -> Path:
    """Stage an unsigned local app for Finder; this is not a release installer."""
    if assets.get("compile_check_only") is not False:
        raise ValueError("A compile-check fixture cannot become a desktop app")
    config = tomllib.loads((ROOT / "desktop/Cargo.toml").read_text(encoding="utf-8"))
    files = {
        "Contents/MacOS/thy666-desktop": bounded_file(executable, parent=ROOT / "build", limit=128 * 1024 * 1024),
        "Contents/Info.plist": plistlib.dumps({
            "CFBundleExecutable": "thy666-desktop", "CFBundleIdentifier": "org.thy666.companion",
            "CFBundleName": "THY666", "CFBundleDisplayName": "THY666", "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": config["package"]["version"], "CFBundleVersion": config["package"]["version"],
            "LSMinimumSystemVersion": "13.0", "NSHighResolutionCapable": True,
        }),
        "Contents/Resources/LICENSE": (ROOT / "LICENSE").read_bytes(),
        "Contents/Resources/THIRD_PARTY_LICENSES.txt": notices,
    }
    for row in assets["resources"]:
        data = bounded_file(ROOT / "build/desktop-resources" / row["file"], parent=ROOT / "build/desktop-resources")
        if digest(data) != row["sha256"]:
            raise ValueError("Desktop resource changed after staging")
        files["Contents/Resources/resources/" + row["file"]] = data
    destination = executable.parent / "THY666.app"
    replace_stage(destination, files)
    (destination / "Contents/MacOS/thy666-desktop").chmod(0o755)
    return destination


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--test", action="store_true", help="Run synthetic Rust tests, then build release")
    group.add_argument("--test-only", action="store_true", help="Stage a compile-only fixture and run synthetic Rust tests")
    group.add_argument("--check", action="store_true", help="Compile-check with a labelled static fixture, without a release artifact")
    parser.add_argument("--offline", action="store_true", help="Use only dependencies already in the workspace Cargo cache")
    parser.add_argument("--without-releases", action="store_true", help="Omit the optional local firmware catalog")
    parser.add_argument("--cargo", help="Explicit installed Cargo executable")
    args = parser.parse_args()
    cargo, rustc = args.cargo or shutil.which("cargo"), os.environ.get("RUSTC") or shutil.which("rustc")
    if not cargo or not rustc:
        parser.error("Rust and the platform C/WebView build prerequisites are required")
    compiler = subprocess.check_output([rustc, "--version", "--verbose"], text=True)
    host = re.search(r"(?m)^host: (\S+)$", compiler)
    if host is None:
        parser.error("Could not identify the native Rust host")
    host = host[1]
    assets = prepare(compile_check=args.check or args.test_only, with_releases=not args.without_releases)
    native_inputs = native_source_identities()
    environment = build_environment(host)
    common = ["--manifest-path", "desktop/Cargo.toml", "--locked"] + (["--offline"] if args.offline else [])
    # The notice bundle covers every locked target, so cache those exact source
    # archives up front rather than silently omitting other-platform notices.
    subprocess.run([cargo, "fetch", *common], cwd=ROOT, env=environment, check=True)
    if args.test or args.test_only:
        subprocess.run([cargo, "test", *common], cwd=ROOT, env=environment, check=True)
    if args.test_only:
        return
    if args.check:
        subprocess.run([cargo, "check", *common], cwd=ROOT, env=environment, check=True)
        return
    subprocess.run([cargo, "build", "--release", *common], cwd=ROOT, env=environment, check=True)
    if native_source_identities() != native_inputs:
        raise ValueError("Native inputs changed during compilation; rebuild before packaging")
    target = TARGET / host if os.environ.get("CARGO_BUILD_TARGET") else TARGET
    executable = target / "release" / ("thy666-desktop.exe" if "windows" in host else "thy666-desktop")
    notices = dependency_notices(ROOT / "desktop/Cargo.lock", CARGO_CACHE)
    (target / "release/THIRD_PARTY_LICENSES.txt").write_bytes(notices)
    shutil.copyfile(ROOT / "LICENSE", target / "release/LICENSE")
    replace_stage(target / "release/resources", {row["file"]: bounded_file(ROOT / "build/desktop-resources" / row["file"], parent=ROOT / "build/desktop-resources") for row in assets["resources"]})
    app_bundle = macos_development_bundle(executable, assets, notices) if host.endswith("apple-darwin") else None
    release = package_desktop(executable, host, assets, notices) if host.endswith("windows-msvc") else None
    result = {"format": "thy666.desktop-build", "version": 1, "host": host,
              "rustc": compiler.splitlines()[0], "tests_run": bool(args.test),
              "native_source_sha256": native_inputs, "assets": assets,
              "hardware_access_performed": False, "artifact": verify_artifact(executable, host), "package": release,
              "development_app": str(app_bundle.relative_to(ROOT)) if app_bundle else None}
    (TARGET / "desktop-build.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
