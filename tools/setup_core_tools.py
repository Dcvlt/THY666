"""Install a SHA-verified portable Zig C/WebAssembly toolchain in .tools.

Archive identities are from https://ziglang.org/download/index.json, release
0.14.1. This does not change the global PATH or system compiler installation.
"""
import hashlib
import json
from pathlib import Path
import platform
import tarfile
import urllib.request
import zipfile
from local_env import env_path, env_value

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.14.1"
PACKAGES = {
    ("Windows", "x86_64"): ("x86_64-windows", "zip", 82229343, "554f5378228923ffd558eac35e21af020c73789d87afeabf4bfd16f2e6feed2c"),
    ("Linux", "x86_64"): ("x86_64-linux", "tar.xz", 49086504, "24aeeec8af16c381934a6cd7d95c807a8cb2cf7df9fa40d359aa884195c4716c"),
    ("Linux", "aarch64"): ("aarch64-linux", "tar.xz", 44954692, "f7a654acc967864f7a050ddacfaa778c7504a0eca8d2b678839c21eea47c992b"),
    ("Darwin", "x86_64"): ("x86_64-macos", "tar.xz", 51044512, "b0f8bdfb9035783db58dd6c19d7dea89892acc3814421853e5752fe4573e5f43"),
    ("Darwin", "aarch64"): ("aarch64-macos", "tar.xz", 45903552, "39f3dc5e79c22088ce878edc821dedb4ca5a1cd9f5ef915e9b3cc3053e8faefa"),
}


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            result.update(chunk)
    return result.hexdigest()


def main():
    if env_path("THY666_ZIG_EXECUTABLE") or env_value("THY666_ZIG_SHA256"):
        from build_diagnostic import compiler_identity
        executable, record = compiler_identity()
        output = ROOT / ".tools/config/core-toolchain.json"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        print("Verified configured portable compiler", flush=True)
        return
    machine = platform.machine().lower()
    architecture = {"amd64": "x86_64", "arm64": "aarch64"}.get(machine, machine)
    key = (platform.system(), architecture)
    if key not in PACKAGES:
        raise SystemExit(f"No pinned package for {key}; supply an installed clang through build_core.py --cc")
    target, extension, size, expected = PACKAGES[key]
    folder = f"zig-{target}-{VERSION}"
    filename = f"{folder}.{extension}"
    url = f"https://ziglang.org/download/{VERSION}/{filename}"
    tools = ROOT / ".tools"
    downloads = tools / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / filename
    if not archive.exists() or archive.stat().st_size != size or digest(archive) != expected:
        partial = downloads / (filename + ".part")
        print(f"Downloading portable Zig {VERSION} ({size:,} bytes)", flush=True)
        request = urllib.request.Request(url, headers={"User-Agent": "THY666-musical-core"})
        with urllib.request.urlopen(request, timeout=60) as source, partial.open("wb") as output:
            while chunk := source.read(1024 * 1024):
                output.write(chunk)
        if partial.stat().st_size != size or digest(partial) != expected:
            raise ValueError("Portable compiler archive identity mismatch")
        partial.replace(archive)
    installation = tools / folder
    executable = installation / ("zig.exe" if key[0] == "Windows" else "zig")
    marker = installation / ".thy666-verified.json"
    if not marker.exists():
        print(f"Extracting verified compiler under {tools}", flush=True)
        if extension == "zip":
            with zipfile.ZipFile(archive) as package:
                for item in package.infolist():
                    if not (tools / item.filename).resolve().is_relative_to(tools.resolve()):
                        raise ValueError("Compiler archive member escapes .tools")
                package.extractall(tools)
        else:
            with tarfile.open(archive) as package:
                for item in package.getmembers():
                    destination = (tools / item.name).resolve()
                    if not destination.is_relative_to(tools.resolve()) or item.isdev():
                        raise ValueError("Unsupported compiler archive member")
                    if item.issym() or item.islnk():
                        link = (destination.parent if item.issym() else tools) / item.linkname
                        if not link.resolve().is_relative_to(tools.resolve()):
                            raise ValueError("Compiler archive link escapes .tools")
                package.extractall(tools)
        marker.write_text(json.dumps({"archive_sha256": expected}) + "\n", encoding="utf-8")
    if not executable.is_file():
        raise ValueError("Compiler installation is incomplete")
    record = {"version": VERSION, "executable": str(executable), "executable_sha256": digest(executable),
              "archive": str(archive), "archive_sha256": expected, "source_url": url,
              "identity_source": "https://ziglang.org/download/index.json"}
    output = ROOT / ".tools/config/core-toolchain.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(f"Verified compiler: {executable}")


if __name__ == "__main__":
    main()
