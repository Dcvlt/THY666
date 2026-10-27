"""Build and inspect the candidate STM32 diagnostic image; never access a device."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from inspect_diagnostic_elf import inspect_elf
from local_env import env_path, env_value
from anonymize_elf import anonymize_debug_paths

ROOT = Path(__file__).resolve().parents[1]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compiler_identity():
    """Select a local Zig executable without accepting an unpinned override."""
    manifest = ROOT / ".tools/config/core-toolchain.json"
    record = json.loads(manifest.read_text(encoding="utf-8")) if manifest.exists() else None
    configured = env_path("THY666_ZIG_EXECUTABLE")
    configured_sha = env_value("THY666_ZIG_SHA256")
    if bool(configured) != bool(configured_sha):
        raise ValueError("THY666_ZIG_EXECUTABLE and THY666_ZIG_SHA256 must be set together")
    if configured:
        if (len(configured_sha) != 64 or
                any(char not in "0123456789abcdefABCDEF" for char in configured_sha)):
            raise ValueError("THY666_ZIG_SHA256 must be a SHA-256 hexadecimal digest")
        expected = configured_sha.lower()
        if sha(configured) != expected:
            raise ValueError("Configured portable compiler identity changed")
        if record is None or record.get("executable_sha256") != expected:
            record = {"version": "configured", "identity_source": "local configuration"}
        record = {**record, "executable": str(configured), "executable_sha256": expected}
        return configured, record
    if record is None:
        raise ValueError("Run python tools/setup_core_tools.py first")
    executable = Path(record["executable"])
    if sha(executable) != record["executable_sha256"]:
        raise ValueError("Portable compiler identity changed")
    return executable, record


def source_prefix_flags():
    """Keep checkout locations out of debug information and __FILE__ strings."""
    locations = [(Path.home(), "/home"), (ROOT, ".")]
    configured = env_path("THY666_ZIG_EXECUTABLE")
    if configured and not configured.is_relative_to(ROOT):
        locations.append((configured.parent, "/toolchain"))
    flags = []
    for path, replacement in locations:
        # Clang can record either slash convention on Windows.
        for prefix in dict.fromkeys((str(path), path.as_posix())):
            flags.extend((f"-ffile-prefix-map={prefix}={replacement}",
                          f"-fdebug-prefix-map={prefix}={replacement}"))
    return [*flags, "-fdebug-compilation-dir=."]


def anonymize_elf_file(path):
    locations = [(ROOT, "/repo"), (Path.home(), "/home")]
    configured = env_path("THY666_ZIG_EXECUTABLE")
    if configured:
        locations.append((configured.parent, "/tool"))
    prefixes = [(prefix, label) for location, label in locations
                for prefix in dict.fromkeys((str(location), location.as_posix()))]
    original = path.read_bytes()
    clean, report = anonymize_debug_paths(original, prefixes)
    path.write_bytes(clean)
    return report


def compiler_environment():
    executable, record = compiler_identity()
    env = os.environ.copy()
    for name, directory in (("ZIG_GLOBAL_CACHE_DIR", ROOT / ".tools/zig-cache/global"),
                            ("ZIG_LOCAL_CACHE_DIR", ROOT / ".tools/zig-cache/diagnostic"),
                            ("TEMP", ROOT / ".tools/tmp"), ("TMP", ROOT / ".tools/tmp")):
        directory.mkdir(parents=True, exist_ok=True)
        env[name] = str(directory)
    return executable, record, env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", action="store_true", help="Check ARM cross-compilation only")
    parser.add_argument("--board-only", action="store_true", help="Build without USB dependencies or transport")
    parser.add_argument("--test", action="store_true", help="Run native diagnostic protocol/descriptor tests after build")
    parser.add_argument("--keep-debug", action="store_true",
                        help="Keep private toolchain DWARF paths for local debugging; default ELF anonymizes them")
    parser.add_argument("--output-dir", type=Path, help="New directory under build/; preserve earlier diagnostic evidence")
    layout = parser.add_mutually_exclusive_group()
    layout.add_argument("--ram-reader", action="store_true", help="Link a volatile SWD-loaded SRAM reader; never install to flash")
    layout.add_argument("--rom-dfu-reader", action="store_true", help="Link a volatile reader above the ST ROM DFU RAM reservation; never install to flash")
    args = parser.parse_args()
    executable, toolchain, env = compiler_environment()
    output = args.output_dir.resolve() if args.output_dir else ROOT / "build/diagnostic"
    if args.output_dir:
        if not output.is_relative_to((ROOT / "build").resolve()):
            raise ValueError("Explicit diagnostic output must be under workspace build/")
        if output.exists():
            raise ValueError("Explicit output directory exists; preserve earlier build evidence")
    output.mkdir(parents=True, exist_ok=True)
    flags = ["-target", "thumb-freestanding-eabi", "-mcpu=cortex_m4", "-mfloat-abi=soft",
             "-ffreestanding", "-fno-builtin", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
             "-fdata-sections", "-ffunction-sections", "-Os", "-g", "-Wall", "-Wextra", "-Werror", "-std=c11"]
    flags.extend(source_prefix_flags())
    if args.probe:
        source = output / "toolchain-probe.c"
        source.write_text('void Reset_Handler(void) { for (;;) __asm__ volatile("wfi"); }\n', encoding="utf-8")
        subprocess.run([str(executable), "cc", *flags, "-c", str(source), "-o", str(output / "toolchain-probe.o")],
                       cwd=ROOT, env=env, check=True)
        print("Cortex-M4 soft-float object cross-compilation passed")
        return
    firmware = ROOT / "firmware/diagnostic"
    sources = [firmware / name for name in ("startup.S", "main.c", "board.c", "runtime.c", "led.c", "controls.c", "pads.c", "center.c")]
    includes = [firmware, output, ROOT / "core"]
    dependency_record = None
    if not args.board_only:
        from setup_diagnostic_deps import setup, USB_SOURCES, TINYUSB_ROOT, DEPS
        dependency_record = setup()
        sources.extend(firmware / name for name in ("usb.c", "usb_descriptors.c", "protocol.c"))
        sources.extend([ROOT / "core/thy_core.c", ROOT / "core/thy_midi.c"])
        sources.extend(TINYUSB_ROOT / name for name in USB_SOURCES)
        includes.extend([TINYUSB_ROOT / "src", DEPS / "cmsis-core", DEPS / "cmsis-device-f4"])
        flags.extend(["-DSTM32F405xx", "-DTHY_DIAG_WITH_USB=1"])
    else:
        flags.append("-DTHY_DIAG_WITH_USB=0")
    linker_name = "linker-rom-dfu.ld" if args.rom_dfu_reader else "linker-ram.ld" if args.ram_reader else "linker.ld"
    execution = "rom-dfu-ram" if args.rom_dfu_reader else "ram" if args.ram_reader else "flash"
    linker = firmware / linker_name
    inputs = sources + list(firmware.rglob("*.h")) + [linker, ROOT / "core/thy_core.h", ROOT / "core/thy_midi.h"]
    missing = [str(path.relative_to(ROOT)) for path in inputs if not path.exists()]
    if missing:
        raise ValueError("Missing diagnostic inputs: " + ", ".join(missing))
    source_hashes = {str(path.relative_to(ROOT)).replace("\\", "/"): sha(path) for path in sorted(set(inputs))}
    identity = hashlib.sha256(json.dumps(source_hashes, sort_keys=True).encode("utf-8")).hexdigest()
    (output / "diag_build_info.h").write_text(f'#define THY_DIAG_BUILD_ID "{identity[:16]}"\n', encoding="utf-8")
    name = "THY666-diagnostic-board-only" if args.board_only else "THY666-diagnostic"
    if args.ram_reader:
        name += "-ram-reader"
    if args.rom_dfu_reader:
        name += "-rom-dfu-reader"
    target = output / (name + ".elf")
    map_file = output / (name + ".map")
    print(f"Building candidate STM32F405 diagnostic: {name}", flush=True)
    arguments = [str(executable), "cc", *flags, "-nostdlib", "-include", str(output / "diag_build_info.h")]
    for directory in includes:
        arguments.extend(["-I", str(directory)])
    arguments.extend(["-I", str(firmware / "freestanding")])
    arguments.extend(["-Wl,-T," + str(linker), "-Wl,--gc-sections", "-Wl,--build-id=none",
                      "-Wl,--entry=Reset_Handler", "-Wl,-z,stack-size=8192"])
    arguments.extend(str(path) for path in sources)
    arguments.extend(["-o", str(target)])
    subprocess.run(arguments, cwd=ROOT, env=env, check=True)
    debug_paths = None if args.keep_debug else anonymize_elf_file(target)
    inspection, image = inspect_elf(target.read_bytes(), execution=execution)
    map_lines = ["Offline load/symbol map from the validated ARM ELF", ""]
    for segment in inspection["segments"]:
        map_lines.append(f'{segment["region"]:18} VMA=0x{segment["virtual_address"]:08x} LMA=0x{segment["load_address"]:08x} file={segment["file_bytes"]} memory={segment["memory_bytes"]}')
    map_lines.append("")
    for symbol, record in sorted(inspection["symbols"].items(), key=lambda item: (item[1]["address"], item[0])):
        map_lines.append(f'0x{record["address"]:08x} {record["bytes"]:8} {symbol}')
    map_file.write_text("\n".join(map_lines) + "\n", encoding="utf-8")
    binary = output / (name + ".bin")
    binary.write_bytes(image)
    report = output / (name + ".inspection.json")
    report.write_text(json.dumps(inspection, indent=2) + "\n", encoding="utf-8")
    manifest = {"format": "thy666.stm32-diagnostic-build", "version": 1, "build_id": identity[:16],
                "build_script_sha256": sha(Path(__file__)),
                "toolchain": {"version": toolchain["version"], "executable_sha256": toolchain["executable_sha256"]},
                "target": "thumb-freestanding-eabi / Cortex-M4 / soft-float", "usb_enabled": not args.board_only,
                "private_debug_paths_retained": args.keep_debug,
                "debug_path_anonymization": debug_paths,
                "compiler_flags": flags, "reserved_stack_bytes": 8192,
                "source_sha256": source_hashes, "dependencies": dependency_record,
                "artifacts": {path.name: {"bytes": path.stat().st_size, "sha256": sha(path)}
                              for path in (target, binary, map_file, report)},
                "inspection": {key: inspection[key] for key in ("entry", "execution_mode", "image_start", "image_end", "initial_stack", "memory_bytes")},
                "flash_ready": False, "device_execution_ready": False,
                "scope": "Build and offline validation only. No device access or .333 installation container."}
    manifest_path = output / (name + ".manifest.json")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Validated image: {binary} ({len(image):,} bytes)", flush=True)
    print(json.dumps(inspection["memory_bytes"], indent=2), flush=True)
    print("Offline candidate only: device acceptance, execution and recovery remain unverified.", flush=True)
    if args.test:
        if args.board_only:
            raise ValueError("--test requires the USB/protocol build")
        test_source = ROOT / "tests/core/test_diagnostic.c"
        if not test_source.exists():
            raise ValueError(f"Missing native diagnostic test: {test_source}")
        # Tests use board stubs and descriptor bytes; no STM32 register access.
        native_flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-DTHY_DIAG_HOST_TEST=1", "-DSTM32F405xx"]
        for directory in includes:
            native_flags.extend(["-I", str(directory)])
        native_jobs = ((test_source, [firmware / "protocol.c", firmware / "usb_descriptors.c", firmware / "controls.c",
                                     ROOT / "core/thy_core.c", ROOT / "core/thy_midi.c"]),
                       (ROOT / "tests/core/test_diagnostic_controls.c", [firmware / "controls.c"]),
                       (ROOT / "tests/core/test_diagnostic_led.c", []),
                       (ROOT / "tests/core/test_diagnostic_pads.c", []),
                       (ROOT / "tests/core/test_diagnostic_center.c", []))
        manifest["additional_native_validation"] = []
        for test, tested_sources in native_jobs:
            native = output / (test.stem + (".exe" if os.name == "nt" else ""))
            subprocess.run([str(executable), "cc", *native_flags, *map(str, tested_sources), str(test), "-o", str(native)],
                           cwd=ROOT, env=env, check=True)
            result = subprocess.run([str(native)], cwd=ROOT, env=env, text=True, capture_output=True, check=True)
            print(result.stdout, end="", flush=True)
            if result.stderr:
                print(result.stderr, file=sys.stderr, end="")
            validation = {"passed": True, "source": str(test.relative_to(ROOT)).replace("\\", "/"),
                          "source_sha256": sha(test), "executable_sha256": sha(native),
                          "output": result.stdout.strip()}
            if test == test_source:
                manifest["native_validation"] = validation
            else:
                manifest["additional_native_validation"].append(validation)
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
