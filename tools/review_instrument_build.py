"""Audit a frozen instrument build offline; never open hardware or alter a build.

Verifies every declared source, snapshot, artifact and native pass record before
running the existing bounded actual-reset-byte model and direct-call inspection.
Build identity, snapshot size and test count come from the selected manifest.
An audit is not device validation and never changes its offline readiness flags.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "tests"), str(ROOT / ".tools/python-deps")]
from inspect_diagnostic_elf import APP_START, APP_END, RAM_START, RAM_END, inspect_elf
from review_diagnostic_startup import reset_contexts, direct_call_review

SETTINGS_START = 0x080C0000
SETTINGS_END = 0x08100000


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def relative_name(value):
    """Manifest names must be genuinely relative on both supported path styles."""
    require(isinstance(value, str) and value, "Empty or non-text manifest path")
    require(not PureWindowsPath(value).drive and not PureWindowsPath(value).is_absolute(),
            f"Absolute manifest path: {value}")
    normalized = value.replace("\\", "/")
    path = PurePosixPath(normalized)
    require(not path.is_absolute() and ".." not in path.parts and ":" not in normalized,
            f"Manifest path leaves its declared root: {value}")
    return path.as_posix()


def review_settings_partition(inspection):
    """Validate actual ELF allocations, not just a linker script's text.

    The generic diagnostic inspector retains the historical 768KiB application
    envelope. An instrument that owns the settings journal has a stricter
    512KiB image/load envelope and must expose the exact reserved boundaries.
    """
    symbols = inspection["symbols"]
    require(symbols.get("__settings_start__", {}).get("address") == SETTINGS_START,
            "ELF settings start must be 0x080c0000")
    require(symbols.get("__settings_end__", {}).get("address") == SETTINGS_END,
            "ELF settings end must be 0x08100000")
    require(inspection["image_start"] == APP_START and
            APP_START < inspection["image_end"] <= SETTINGS_START,
            "Application image overlaps reserved settings sectors")
    for segment in inspection["segments"]:
        virtual, load = segment["virtual_address"], segment["load_address"]
        memory, files = segment["memory_bytes"], segment["file_bytes"]
        if segment["region"] == "application_flash":
            require(APP_START <= virtual <= SETTINGS_START and
                    memory <= SETTINGS_START - virtual,
                    "ELF flash allocation overlaps reserved settings sectors")
        if files:
            require(APP_START <= load <= SETTINGS_START and files <= SETTINGS_START - load,
                    "ELF initialized load bytes overlap reserved settings sectors")
    return {"start": f"0x{SETTINGS_START:08x}", "end_exclusive": f"0x{SETTINGS_END:08x}",
            "reserved_bytes": SETTINGS_END - SETTINGS_START,
            "application_capacity_bytes": SETTINGS_START - APP_START,
            "all_flash_allocations_and_initializers_outside_settings": True}


def review_build(build_dir):
    build = Path(build_dir).resolve()
    require(build.is_relative_to(ROOT / "build") and build != ROOT / "build",
            "Build directory must be inside build/")
    require(build.is_dir(), "Selected build directory does not exist")
    seen = {}

    def read(path):
        path = Path(path).resolve()
        require(path.is_relative_to(ROOT), f"Input outside workspace: {path}")
        if path not in seen:
            seen[path] = path.read_bytes()
        return seen[path]

    def match(path, expected):
        raw = read(path)
        record = {"sha256": expected} if isinstance(expected, str) else expected
        require(isinstance(record, dict) and isinstance(record.get("sha256"), str),
                f"Invalid identity record for {path}")
        require(digest(raw) == record["sha256"], f"SHA256 mismatch: {path}")
        require("bytes" not in record or len(raw) == record["bytes"], f"Size mismatch: {path}")
        return raw

    def current_source(name):
        name = relative_name(name)
        return build / "instrument_data.c" if name == "generated/instrument_data.c" else ROOT / name

    manifest_path = build / "THY666-instrument-alpha.manifest.json"
    manifest = json.loads(read(manifest_path))
    require(manifest["format"] == "thy666.integrated-instrument-alpha", "Wrong build manifest format")
    require(manifest["flash_ready"] is False and manifest["device_validated"] is False,
            "Frozen manifest offline readiness flags must remain false")
    require(manifest["sources_unchanged_during_build"] is True, "Build recorded source drift")
    sources = manifest["source_sha256"]
    require(isinstance(sources, dict) and bool(sources), "Build declares no source identities")
    identity = digest(json.dumps(sources, sort_keys=True).encode())[:16]
    require(identity == manifest["build_id"], "Manifest build ID does not match its source identities")
    for name, expected in sources.items():
        match(current_source(name), expected)

    snapshot_path = build / "source-snapshot/manifest.json"
    snapshot = json.loads(match(snapshot_path, manifest["source_snapshot"]["manifest_sha256"]))
    require(isinstance(snapshot, dict) and bool(snapshot), "Build declares no frozen source snapshot")
    count = manifest["source_snapshot"]["files"]
    require(isinstance(count, int) and not isinstance(count, bool) and len(snapshot) == count,
            "Frozen source snapshot file count mismatch")
    for name, expected in snapshot.items():
        match(build / "source-snapshot" / relative_name(name), expected)
        match(current_source(name), expected)
    for name, expected in sources.items():
        require(snapshot.get(relative_name(name)) == expected,
                f"Compiled input is absent or differs in frozen snapshot: {name}")

    artifacts = manifest["artifacts"]
    required_artifacts = {"THY666-instrument-alpha.elf", "THY666-instrument-alpha.bin",
                          "THY666-instrument-alpha.inspection.json"}
    require(required_artifacts.issubset(artifacts), "ELF/BIN/inspection identities must all be declared")
    for name, expected in artifacts.items():
        require(relative_name(name) == Path(name).name, "Artifact path leaves the build directory")
        match(build / name, expected)
    elf_path = build / "THY666-instrument-alpha.elf"
    elf = read(elf_path)
    inspection, image = inspect_elf(elf, execution="flash")
    require(image == read(build / "THY666-instrument-alpha.bin"), "Actual ELF/BIN mismatch")
    require(inspection == json.loads(read(build / "THY666-instrument-alpha.inspection.json")),
            "Saved inspection does not match the actual ELF")
    require(inspection["memory_bytes"] == manifest["memory_bytes"], "Manifest memory usage mismatch")
    symbols = inspection["symbols"]
    ram_end = symbols["__ram_end__"]["address"]
    stack_limit = symbols["__stack_limit__"]["address"]
    stack_top = symbols["__stack_top__"]["address"]
    require(stack_top - stack_limit == manifest["reserved_stack_bytes"] == 8192,
            "Expected 8KiB reserved stack")
    require(RAM_START <= ram_end <= stack_limit < stack_top <= RAM_END,
            "Application RAM overlaps reserved stack or exceeds SRAM")
    require(inspection["initial_stack"] == stack_top and inspection["image_start"] == APP_START and
            inspection["image_end"] <= APP_END, "Stack vector/application bounds mismatch")
    # Older frozen diagnostic/instrument evidence used the full application
    # slot. Only a build that compiles this backend owns these sectors; it must
    # prove the new partition in its actual ELF before packaging can proceed.
    has_settings = (any(relative_name(name) == "firmware/instrument/settings.c" for name in sources) or
                    "thy_instrument_settings_io" in symbols or "__settings_start__" in symbols or
                    "__settings_end__" in symbols)
    partition = review_settings_partition(inspection) if has_settings else None

    # The generated header is not a compiled source identity because it contains
    # that identity. Check it explicitly, then verify the ELF's live ID symbol.
    expected_header = f'#define THY_DIAG_BUILD_ID "{identity}"'.encode("ascii")
    require(read(build / "diag_build_info.h") in (expected_header + b"\n", expected_header + b"\r\n"),
            "Generated build identity header mismatch")
    build_symbol = symbols["thy_diag_build_id"]
    offset = build_symbol["address"] - inspection["image_start"]
    require(0 <= offset <= len(image) and build_symbol["bytes"] <= len(image) - offset,
            "ELF build identity symbol leaves the image")
    require(image[offset:offset + build_symbol["bytes"]] == identity.encode() + b"\0",
            "Actual ELF contains a different build identity")

    tests = manifest["tests"]
    require(isinstance(tests, list) and bool(tests), "Build must contain recorded native passes")
    native, names = [], set()
    for test in tests:
        name = test["name"]
        require(isinstance(name, str) and re.fullmatch(r"test_[A-Za-z0-9_]+", name) and name not in names,
                "Native test name is invalid or repeated")
        names.add(name)
        require(test["passed"] is True and isinstance(test["output"], str) and test["output"].strip(),
                f"Missing recorded native pass: {name}")
        binary = build / (name + ".exe")
        match(binary, test["binary_sha256"])
        native_sources = test["source_sha256"]
        require(isinstance(native_sources, dict) and bool(native_sources),
                f"Native pass declares no sources: {name}")
        for source, expected in native_sources.items():
            match(current_source(source), expected)
        native.append({"name": name, "recorded_pass": True, "output": test["output"],
                       "binary_sha256": test["binary_sha256"], "source_count": len(native_sources)})

    # Hash every implementation file used by the bounded model, in addition to
    # the selected firmware. Unsupported reset instructions fail in that model.
    for name in ("tools/review_diagnostic_startup.py", "tools/inspect_diagnostic_elf.py",
                 "tests/test_diagnostic_startup.py", "tools/build_validation.py"):
        read(ROOT / name)
    implementation = Path(__file__).resolve()
    read(implementation)
    model = reset_contexts(image, inspection)
    calls = direct_call_review(elf, image, inspection)
    require(all(path.read_bytes() == raw for path, raw in seen.items()), "Input changed during audit")
    return {
        "format": "thy666.instrument-startup-review", "version": 2,
        "build_id": identity, "build_directory": build.relative_to(ROOT).as_posix(),
        "scope": "Fresh offline audit of selected frozen ELF and all declared source identities; no device access",
        "hardware_checked": False, "device_execution_performed": False,
        "flash_ready": False, "device_validated": False,
        "auditor_sha256": digest(seen[implementation]),
        "checks": {
            "manifest_build_id_matches_sources": True, "elf_build_id_matches_manifest": True,
            "all_current_snapshot_files_match": count, "all_frozen_snapshot_files_match": count,
            "all_compiled_sources_in_snapshot": True, "compiled_source_count": len(sources),
            "all_build_artifact_identities_match": True, "elf_reconstructed_bin_exact": True,
            "saved_inspection_matches_actual_elf": True, "native_validation_record_identities_match": True,
            "native_validation_records_checked": len(native), "native_validation_rerun": False,
            "msp_and_psp_reset_invariants_pass": True, "stack_and_application_bounds_pass": True,
            "settings_partition_checked": has_settings,
            "inputs_unchanged_during_review": True,
        },
        "memory": {"image_bytes": len(image), "bytes": inspection["memory_bytes"],
                   "stack_reserved_bytes": stack_top - stack_limit,
                   "unused_sram_below_stack_bytes": stack_limit - ram_end,
                   "initial_msp": f"0x{inspection['initial_stack']:08x}",
                   "entry": f"0x{inspection['entry']:08x}", "settings_partition": partition},
        "startup_model": {"contexts": model,
            "limitations": "Bounded synchronous actual-reset-byte model ends before main; does not establish peripheral behavior, asynchronous faults, instruction timing, MPU permissions or physical updater behavior."},
        "direct_calls": {**calls,
            "limitations": "Symbol-based direct BL/BLX audit only; indirect call sites are reported, not dataflow-proven."},
        "native_pass_records": native,
        "source_identities": {path.relative_to(ROOT).as_posix(): {"bytes": len(raw), "sha256": digest(raw)}
                              for path, raw in seen.items()},
        "device_actions": [],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="Fresh JSON file inside analysis/local/")
    args = parser.parse_args(argv)
    try:
        output = args.output.resolve()
        require(output.is_relative_to(ROOT / "analysis/local") and output.suffix.lower() == ".json",
                "Output must be a JSON file inside analysis/local/")
        require(not output.exists(), "Existing audit evidence must be preserved")
        require(output.parent.is_dir(), "Output parent directory must already exist")
        report = review_build(args.build_dir)
        require(output not in {ROOT / name for name in report["source_identities"]},
                "Audit output cannot replace an input")
        with output.open("x", encoding="utf-8", newline="\n") as handle:
            json.dump(report, handle, indent=2)
            handle.write("\n")
        print(json.dumps({"build_id": report["build_id"], "checks": report["checks"],
            "memory": report["memory"], "direct_calls": report["direct_calls"],
            "output": output.relative_to(ROOT).as_posix()}, indent=2))
        return 0
    except (OSError, ValueError, KeyError, TypeError, AssertionError, ImportError) as exc:
        print(f"Instrument build audit failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
