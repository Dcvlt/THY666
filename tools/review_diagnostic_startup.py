"""Review a fresh diagnostic ELF with the bounded reset model; no device access.

The historical report format name is retained for preflight compatibility.
This tool checks the chosen build, rather than reusing conclusions about a
previous build or claiming to repeat its hardware observations.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / ".tools/python-deps"), str(ROOT / "tests")]
from inspect_diagnostic_elf import inspect_elf, slice_checked
from test_diagnostic_startup import ResetModel
from build_validation import verify_additional_native_validations


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def reset_contexts(image, inspection):
    """Execute actual reset bytes for both inherited stack selections."""
    symbols = inspection["symbols"]
    address = lambda name: symbols[name]["address"]
    contexts = []
    for use_psp in (False, True):
        model = ResetModel(image, inspection, use_psp=use_psp)
        model.execute()  # Unknown instructions and unbounded loops fail closed.
        require(model.stop_target == address("main") & ~1, "Reset model did not stop before main")
        require(model.special["msp"] == model.special["psp"] == address("__stack_top__"),
                "Reset did not establish both stack pointers")
        require({key: model.special[key] for key in ("control", "basepri", "faultmask", "primask")} ==
                {"control": 0, "basepri": 0, "faultmask": 0, "primask": 1},
                "Reset did not establish the expected processor masks")
        for register, expected in ((0xe000ed94, 0), (0xe000ed08, address("g_pfnVectors")),
                                   (0xe000ed0c, 0x05fa0300), (0xe000ed14, 0x210),
                                   (0xe000ed28, 0), (0xe000ed2c, 0)):
            require(model.mmio[register] == expected, f"Reset register mismatch at {register:#x}")
        dstart, dend, load = map(address, ("__data_start__", "__data_end__", "__data_load__"))
        require(model.ram[dstart - 0x20000000:dend - 0x20000000] ==
                image[load - inspection["image_start"]:load - inspection["image_start"] + dend - dstart],
                "Initialized data was not copied from this ELF")
        bstart, bend = map(address, ("__bss_start__", "__bss_end__"))
        require(model.ram[bstart - 0x20000000:bend - 0x20000000] == bytes(bend - bstart),
                "BSS was not cleared")
        entry, size = address("g_diag_entry"), symbols["g_diag_entry"]["bytes"]
        require(size == 112 and entry >= bend, "Entry snapshot layout/placement changed")
        expected = [0x5448454e, 1] + [model.incoming[key] for key in
                    ("control", "ipsr", "primask", "basepri", "faultmask", "msp", "psp")]
        expected += [model.initial_mmio[reg] for reg in
                     (0xe000ed08, 0xe000ed94, 0xe000ed88, 0xe000ef34, 0xe000ed0c,
                      0xe000ed14, 0xe000ed04, 0xe000ed24, 0xe000ed28, 0xe000ed2c,
                      0x40023800, 0x40023808, 0x40023874, 0x40023840,
                      0x4000300c, 0x40003004, 0x40003008, 0x40002c00, 0x40002c04)]
        snapshot = bytes(model.ram[entry - 0x20000000:entry - 0x20000000 + size])
        require(struct.unpack("<28I", snapshot) == tuple(expected), "Entry snapshot is not the inherited state")
        writes = [(addr, value) for _, addr, value in model.trace]
        snapshot_writes = [(addr, value) for addr, value in writes if entry <= addr < entry + size]
        require(len(snapshot_writes) == 28 and snapshot_writes[-1] == (entry, 0x5448454e),
                "Entry snapshot magic was not committed last")
        require(writes.index((entry, 0x5448454e)) < writes.index((0xe000ed0c, 0x05fa0300)) and
                writes.index((entry, 0x5448454e)) < writes.index((0xe000ed28, 0xffffffff)),
                "Inherited state was overwritten before snapshot completion")
        required_writes = [(0xe000ed04, 0x0a000000)]
        required_writes += [(base + index * 4, 0xffffffff)
                            for base in (0xe000e180, 0xe000e280) for index in range(3)]
        required_writes += [(reg, 0) for reg in (0xe000e010, 0xe000e014, 0xe000e018)]
        require(all(write in writes for write in required_writes), "Pending interrupts/SysTick were not quiesced")
        contexts.append({"incoming_stack_selection": "PSP" if use_psp else "MSP",
                         "incoming_special": model.incoming, "instructions_executed": model.steps,
                         "instruction_step_limit": model.step_limit,
                         "step_budget_basis": "1024 fixed entry instructions plus five instructions per word across the supported 128 KiB SRAM; finite model budget, not execution timing",
                         "stopped_before_main": True, "stopped_at_call_target": f"0x{model.stop_target:08x}",
                         "final_special": model.special, "snapshot_address": f"0x{entry:08x}",
                         "snapshot_bytes": size, "snapshot_hex": snapshot.hex(),
                         "snapshot_sha256": digest(snapshot), "ram_after_reset_sha256": digest(model.ram),
                         "complete_write_trace_sha256": digest(json.dumps(model.trace, separators=(",", ":")).encode())})
    return contexts


def direct_call_review(elf, image, inspection):
    """Bounded symbol-based call audit, not a computed-call/dataflow proof."""
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS
    from capstone.arm import ARM_OP_IMM
    shoff = struct.unpack_from("<I", elf, 32)[0]
    shsize, shcount = struct.unpack_from("<HH", elf, 46)
    sections = [struct.unpack_from("<IIIIIIIIII", elf, shoff + index * shsize) for index in range(shcount)]
    decoder = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    decoder.detail = True
    direct, indirect, count = [], [], 0
    for section in sections:
        if section[1] != 2:
            continue
        strings_section = sections[section[6]]
        strings = slice_checked(elf, strings_section[4], strings_section[5])
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, addr, size, info, _, _ = struct.unpack_from("<IIIBBH", elf, offset)
            addr &= ~1
            if info & 0xf != 2 or not size or not inspection["image_start"] <= addr < inspection["image_end"]:
                continue
            count += 1
            require(size <= inspection["image_end"] - addr, "Function extends beyond the ELF load image")
            label = strings[name:strings.index(b"\0", name)].decode()
            code = image[addr - inspection["image_start"]:addr - inspection["image_start"] + size]
            for instruction in decoder.disasm(code, addr):
                if instruction.mnemonic not in ("bl", "blx"):
                    continue
                record = {"function": label, "address": f"0x{instruction.address:08x}",
                          "bytes": instruction.bytes.hex(), "mnemonic": instruction.mnemonic,
                          "operands": instruction.op_str}
                if instruction.operands and instruction.operands[0].type == ARM_OP_IMM:
                    record["target"] = instruction.operands[0].imm
                    direct.append(record)
                else:
                    indirect.append(record)
    lower = [row for row in direct if 0x08000000 <= row["target"] < 0x08040000]
    require(count > 0, "ELF has no file-backed function symbols to review")
    require(not lower, "Direct call targets missing lower flash")
    return {"function_symbols_decoded": count, "direct_call_count": len(direct),
            "direct_calls_into_missing_lower_flash": lower, "indirect_call_sites": indirect}


def review_build(build_dir, *, historical_review=None, workspace_root=ROOT):
    root, snapshots = Path(workspace_root).resolve(), {}

    def read(path):
        path = Path(path).resolve()
        require(path.is_relative_to(root), "Review input leaves workspace")
        if path not in snapshots:
            snapshots[path] = path.read_bytes()
        return snapshots[path]

    def match(path, expected, label):
        raw = read(path)
        record = {"sha256": expected} if isinstance(expected, str) else expected
        require(digest(raw) == record["sha256"] and ("bytes" not in record or len(raw) == record["bytes"]),
                label + " identity mismatch")
        return raw

    build_dir = Path(build_dir).resolve()
    manifest_path = build_dir / "THY666-diagnostic.manifest.json"
    manifest = json.loads(read(manifest_path))
    require(manifest.get("format") == "thy666.stm32-diagnostic-build" and manifest.get("usb_enabled") is True,
            "Expected a USB diagnostic build manifest")
    require(manifest.get("flash_ready") is False and manifest.get("device_execution_ready") is False,
            "Build readiness flags must remain offline")
    match(root / "tools/build_diagnostic.py", manifest["build_script_sha256"], "Build script")
    sources = manifest["source_sha256"]
    require(isinstance(sources, dict) and sources, "Build source identities are missing")
    for name, expected in sources.items():
        match(root / name, expected, "Build source " + name)
    identity = digest(json.dumps(sources, sort_keys=True).encode())[:16]
    require(identity == manifest["build_id"], "Build ID does not match source identities")
    for name, expected in manifest["artifacts"].items():
        require(Path(name).name == name and name not in ("", ".", "..") and "/" not in name and "\\" not in name,
                "Artifact name leaves build directory")
        match(build_dir / name, expected, "Build artifact " + name)
    elf_path, binary_path = [build_dir / ("THY666-diagnostic." + suffix) for suffix in ("elf", "bin")]
    require(elf_path.name in manifest["artifacts"] and binary_path.name in manifest["artifacts"],
            "Build must declare its ELF and raw binary")
    elf = read(elf_path)
    inspection, image = inspect_elf(elf, execution="flash")
    require(image == read(binary_path), "ELF reconstructed image differs from raw binary")
    saved_path = build_dir / "THY666-diagnostic.inspection.json"
    require(saved_path.name in manifest["artifacts"] and json.loads(read(saved_path)) == inspection,
            "Saved inspection does not match actual ELF")
    require(all(manifest["inspection"].get(key) == inspection[key] for key in
                ("entry", "execution_mode", "image_start", "image_end", "initial_stack", "memory_bytes")),
            "Manifest inspection differs from actual ELF")
    native = manifest["native_validation"]
    require(native.get("passed") is True and isinstance(native.get("output"), str) and native["output"].strip(),
            "Build has no recorded native test pass")
    match(root / native["source"], native["source_sha256"], "Native test source")
    native_executable = build_dir / ("test_diagnostic.exe" if sys.platform == "win32" else "test_diagnostic")
    match(native_executable, native["executable_sha256"], "Native executable")
    _, additional_count = verify_additional_native_validations(
        manifest, root=root, build_dir=build_dir, match=match,
        primary_source=native["source"], primary_executable=native_executable)
    implementation = [Path(__file__), root / "tools/inspect_diagnostic_elf.py", root / "tests/test_diagnostic_startup.py",
                      root / "tools/build_validation.py"]
    for path in implementation:
        read(path)
    contexts = reset_contexts(image, inspection)
    calls = direct_call_review(elf, image, inspection)
    historical = None
    if historical_review is not None:
        old_path = Path(historical_review).resolve()
        old = json.loads(read(old_path))
        require(old.get("format") == "thy666.first-test-startup-review", "Wrong historical review format")
        old_sources = old.get("source_identities", {})
        old_startup = old_sources.get("firmware/diagnostic/startup.S", {})
        historical = {"path": old_path.relative_to(root).as_posix(), "sha256": digest(read(old_path)),
                      "build_id": old.get("prepared_build", {}).get("build_id"),
                      "startup_source_unchanged": sources.get("firmware/diagnostic/startup.S") == old_startup.get("sha256")
                                                  and "sha256" in old_startup,
                      "scope": "Hash-bound historical first-build context only; its observations and unknowns are not reacquired or copied as current conclusions."}
    require(all(path.read_bytes() == raw for path, raw in snapshots.items()), "An input changed during startup review")
    return {"format": "thy666.first-test-startup-review", "version": 2,
            "review_kind": "fresh-diagnostic-reset-regression",
            "scope": "Offline inspection of this build and bounded execution of its actual reset bytes; no device access.",
            "hardware_checked": False, "device_execution_performed": False, "flash_ready": False,
            "source_identities": {path.relative_to(root).as_posix(): {"bytes": len(raw), "sha256": digest(raw)}
                                  for path, raw in snapshots.items()},
            "checks": {"elf_reconstructed_bin_exact": True, "manifest_source_sha256_matches": True,
                       "manifest_artifact_matches": True, "saved_inspection_matches_actual_elf": True,
                       "native_validation_identities_match": True, "native_validation_rerun": False,
                       "additional_native_validation_records_checked": additional_count,
                       "additional_native_validation_identities_match": True,
                       "additional_native_validation_reported_passed": True,
                       "dirty_msp_and_psp_reset_invariants_pass": True, "inputs_unchanged_during_review": True},
            "prepared_build": {"build_id": identity, "inspection": inspection, **calls},
            "startup_model": {"kind": "Bounded Capstone-decoded actual reset instructions in a restricted Thumb subset",
                              "contexts": contexts, "unsupported_instructions_fail": True,
                              "limitations": "No instruction timing, asynchronous exceptions, privilege enforcement, MPU permissions, bus faults, DMA engine execution or peripheral behavior; ends at call to main. Passing does not validate the physical loader, clocks, I2C or USB."},
            "historical_context": historical, "device_actions": []}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--historical-review", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="Fresh JSON filename within the workspace")
    args = parser.parse_args(argv)
    try:
        output = args.output.resolve()
        require(output.is_relative_to(ROOT), "Review output leaves workspace")
        require("resources" not in {part.casefold() for part in output.parts}, "Review output cannot be inside Resources")
        require(not output.exists(), "Existing startup review must be preserved")
        report = review_build(args.build_dir, historical_review=args.historical_review)
        require(output not in {ROOT / name for name in report["source_identities"]}, "Review cannot replace an input")
        with output.open("x", encoding="utf-8", newline="\n") as handle:
            json.dump(report, handle, indent=2)
            handle.write("\n")
        print(f"Fresh startup model passed for build {report['prepared_build']['build_id']}; flash_ready=False; no device access")
        return 0
    except (OSError, ValueError, KeyError, TypeError, AssertionError, ImportError) as exc:
        print(f"Startup review failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
