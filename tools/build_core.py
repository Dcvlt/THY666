"""Build the same portable C engine for native regression tests and WebAssembly."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import subprocess
import sys
import uuid
from build_diagnostic import compiler_identity, source_prefix_flags
from local_env import env_path

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_test(command, env):
    """Keep machine-readable passing output and visible failure diagnostics."""
    result = subprocess.run(command, cwd=ROOT, env=env, text=True, capture_output=True)
    if result.returncode:
        print(result.stdout, end="", flush=True)
        print(result.stderr, file=sys.stderr, end="", flush=True)
        result.check_returncode()
    return result


def undefined_flags():
    # Explicit traps are portable across the supported Clang/Zig hosts. The
    # pinned Windows Zig distribution has no standalone UBSan report runtime.
    return ["-g", "-UNDEBUG", "-fsanitize=undefined", "-fsanitize-trap=undefined",
            "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]


def verify_undefined_checks(compiler, common, native_target, flags, build, env):
    """Fail closed if this compiler ignores instrumentation or assertions."""
    source = build / "undefined-probe.c"
    source.write_text(
        '#include <assert.h>\n#include <limits.h>\n#include <stdio.h>\n'
        '#ifdef NDEBUG\n#error Assertions must remain enabled\n#endif\n'
        'int main(int argc,char **argv) {\n'
        '  if(argc==1) { puts("sanitizer-clean"); return 0; }\n'
        '  if(argv[1][0]==\'o\') {\n'
        '    puts("sanitizer-overflow"); fflush(stdout);\n'
        '    volatile int value=INT_MAX; volatile int result=value+1;\n'
        '    (void)result; return 0;\n'
        '  }\n'
        '  puts("sanitizer-assert"); fflush(stdout); assert(0); return 0;\n'
        '}\n', encoding="utf-8")
    binary = build / ("undefined-probe.exe" if os.name == "nt" else "undefined-probe")
    subprocess.run(compiler + common + native_target + ["-O2"] + flags +
                   [str(source), "-o", str(binary)], cwd=ROOT, env=env, check=True)
    results = {}
    for name, extra, marker in (("clean", [], "sanitizer-clean"),
                                ("overflow", ["overflow"], "sanitizer-overflow"),
                                ("assert", ["assert"], "sanitizer-assert")):
        result = subprocess.run([str(binary), *extra], cwd=ROOT, env=env,
                                text=True, capture_output=True, timeout=30)
        if marker not in result.stdout or (result.returncode != 0 if name == "clean"
                                           else result.returncode == 0):
            print(result.stdout, end="", flush=True)
            print(result.stderr, file=sys.stderr, end="", flush=True)
            raise RuntimeError("Undefined-behavior " + name + " probe failed; "
                               "sanitizer/assertion support is not verified")
        results[name] = {"returncode": result.returncode, "passed": True}
    return {"path": str(binary.relative_to(ROOT)), "sha256": sha(binary),
            "source_sha256": sha(source), "checks": results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="Installed clang executable instead of portable Zig")
    parser.add_argument("--wasm-only", action="store_true")
    parser.add_argument("--test", action="store_true", help="Run the native regression executable after building")
    parser.add_argument("--core-only", action="store_true", help="Skip local data/display projection and preview packaging")
    parser.add_argument("--sanitize", choices=["undefined"],
                        help="Build and run native suites with fatal undefined-behavior traps; "
                             "uses fresh build/core-ubsan output and leaves WebAssembly unchanged")
    args = parser.parse_args()
    if args.sanitize and args.wasm_only:
        parser.error("--sanitize is native-only and cannot be used with --wasm-only")
    if args.wasm_only and args.test:
        parser.error("--test requires the native build")
    if args.sanitize:
        args.test = True
    env = os.environ.copy()
    for name, directory in (("ZIG_GLOBAL_CACHE_DIR", ROOT / ".tools/zig-cache/global"),
                            ("ZIG_LOCAL_CACHE_DIR", ROOT / ".tools/zig-cache/local"),
                            ("TEMP", ROOT / ".tools/tmp"), ("TMP", ROOT / ".tools/tmp")):
        directory.mkdir(parents=True, exist_ok=True)
        env[name] = str(directory)
    if args.cc:
        compiler = [args.cc]
    else:
        executable, record = compiler_identity()
        compiler = [str(executable), "cc"]
    if not args.sanitize:
        subprocess.run([sys.executable, str(ROOT / "tools/build_studio_schema.py")], cwd=ROOT, check=True)
    sources = [ROOT / "core" / name for name in
               ("thy_core.c", "thy_crc32.c", "thy_profile.c", "thy_chord_layout.c", "thy_voicing.c", "thy_harmony.c", "thy_harmony_workspace.c", "thy_library.c", "thy_library_ui.c", "thy_library_engine.c", "thy_mpe.c", "thy_channel_expression.c", "thy_expression_profile.c", "thy_expression_engine.c", "thy_midi.c", "thy_clock.c", "thy_transport.c", "thy_arp.c", "thy_rhythm.c", "thy_calibration.c", "thy_calibration_engine.c", "thy_sequencer.c", "thy_sequencer_ui.c", "thy_sequence_engine.c", "thy_song_transfer.c", "thy_instrument.c", "thy_lighting.c", "thy_ui.c",
                "thy_menu_data.c", "thy_state.c", "thy_engine.c", "thy_studio_ui.c", "thy_display.c", "thy_settings.c", "thy_settings_transfer.c", "thy_companion.c")]
    bridge = ROOT / "host/wasm_bridge.c"
    output = (ROOT / "build/core-ubsan" /
              (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8])
              if args.sanitize else ROOT / "web/generated")
    output.mkdir(parents=True, exist_ok=True)
    common = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "core")]
    common.extend(source_prefix_flags())
    wasm = output / "core.wasm"
    byte_runtime=ROOT / 'firmware/diagnostic/runtime.c'
    byte_headers=ROOT / 'firmware/diagnostic/freestanding'
    input_paths=set(sources+list((ROOT/'core').glob('*.h'))+
        [bridge,byte_runtime,byte_headers/'string.h',Path(__file__).resolve()])
    input_paths.update(p for p in (ROOT/'tests/core').rglob('*') if p.suffix in ('.c','.h'))
    frozen={path:sha(path) for path in input_paths}
    built = {}
    if not args.sanitize:
        exports = re.findall(r"^(?:void|int|uintptr_t|uint32_t) (bridge_\w+)\(", bridge.read_text(), re.M)
        wasm_args = ["-target", "wasm32-freestanding", "-O2", "-ffreestanding", "-fno-builtin", "-nostdlib","-I",str(byte_headers),
                     "-Wl,--no-entry", "-Wl,--export-memory", "-Wl,-z,stack-size=65536",
                     "-Wl,--initial-memory=524288", "-Wl,--max-memory=524288"]
        if args.cc:
            wasm_args[1] = "wasm32-unknown-unknown"
        print("Building shared musical/controller/sensitivity/lighting/OLED WebAssembly", flush=True)
        subprocess.run(compiler + common + wasm_args + [f"-Wl,--export={name}" for name in exports] +
                       [str(path) for path in sources] + [str(byte_runtime),str(bridge), "-o", str(wasm)], cwd=ROOT, env=env, check=True)
        built["webassembly"] = {"path": "web/generated/core.wasm", "sha256": sha(wasm), "bytes": wasm.stat().st_size}
    native_target = []
    if sys.platform == "darwin" and not args.cc:
        # Zig 0.14.1 cannot resolve libc for newer automatically detected macOS
        # versions. Use its supported deployment target, retaining the pinned
        # compiler and its bundled Darwin SDK stubs for native regressions.
        arch = {"arm64": "aarch64", "x86_64": "x86_64"}[platform.machine()]
        native_target = ["-target", f"{arch}-macos.13.0"]
    if not args.wasm_only:
        build = output if args.sanitize else ROOT / "build/core"
        build.mkdir(parents=True, exist_ok=True)
        native_checks = undefined_flags() if args.sanitize else []
        if args.sanitize:
            env["ZIG_LOCAL_CACHE_DIR"] = str(build / "zig-cache")
            print("Native undefined-behavior checks: fatal trap mode, assertions enabled; "
                  "no symbolic UBSan runtime diagnostics. WebAssembly is unchanged.", flush=True)
            built["sanitizer_probe"] = verify_undefined_checks(
                compiler, common, native_target, native_checks, build, env)
        native = build / ("core-tests.exe" if os.name == "nt" else "core-tests")
        tests = [ROOT / "tests/core/test_core.c", ROOT / "tests/core/test_midi.c"]
        print("Building native C regression tests", flush=True)
        subprocess.run(compiler + common + native_target + ["-O2"] + native_checks + [str(path) for path in sources] +
                       [str(test) for test in tests] + ["-o", str(native)], cwd=ROOT, env=env, check=True)
        built["native"] = {"path": str(native.relative_to(ROOT)), "sha256": sha(native)}
        if args.test:
            result = run_test([str(native)], env)
            print(result.stdout, end="", flush=True)
            if result.stderr:
                print(result.stderr, file=sys.stderr, end="")
            built["native"]["tests_passed"] = True
            summary = re.search(r"(\d+) C regression tests passed \((\d+) assertions\)", result.stdout)
            if summary:
                built["native"]["regression_tests"] = int(summary[1])
                built["native"]["assertions"] = int(summary[2])
        suites = []
        for name, extra in (("test_crc32", ["test_crc32.c"]),
                            ("test_profile", ["test_profile.c"]),
                            ("test_settings_observer", ["test_settings_observer.c"]),
                            ("test_assigned_voices", ["test_assigned_voices.c"]),
                            ("test_chords", ["test_chords.c"]),
                            ("test_chord_layout", ["test_chord_layout.c"]),
                            ("test_voicing", ["test_voicing.c"]),
                            ("test_harmony", ["test_harmony.c"]),
                            ("test_harmony_workspace", ["test_harmony_workspace.c"]),
                            ("test_cluster_swap", ["test_cluster_swap.c"]),
                            ("test_menu_reset", ["test_menu_reset.c"]),
                            ("test_library", ["test_library.c"]),
                            ("test_library_ui", ["test_library_ui.c"]),
                            ("test_library_engine", ["test_library_engine.c"]),
                            ("test_mpe", ["test_mpe.c"]),
                            ("test_channel_expression", ["test_channel_expression.c"]),
                            ("test_generated_expression_gate", ["test_generated_expression_gate.c"]),
                            ("test_expression_profile", ["test_expression_profile.c"]),
                            ("test_expression_engine", ["test_expression_engine.c"]),
                            ("test_calibration", ["test_calibration.c"]),
                            ("test_calibration_engine", ["test_calibration_engine.c"]),
                            ("test_clock_transport", ["test_clock_transport.c"]),
                            ("test_arp", ["test_arp.c"]),
                            ("test_arp_engine", ["test_arp_engine.c"]),
                            ("test_arp_ui", ["test_arp_ui.c"]),
                            ("test_views", ["test_views.c"]),
                            ("test_display", ["test_display.c"]),
                            ("test_sequencer", ["test_sequencer.c"]),
                            ("test_sequencer_ui", ["test_sequencer_ui.c"]),
                            ("test_sequencer_engine", ["test_sequencer_engine.c"]),
                            ("test_song_transfer", ["test_song_transfer.c"]),
                            ("test_settings_transfer", ["test_settings_transfer.c"]),
                            ("test_companion", ["test_companion.c"]),
                            ("test_generated_owners", ["test_generated_owners.c"]),
                            ("test_rhythm_engine", ["test_rhythm_engine.c"]),
                            ("test_state", ["test_state.c"]),
                            ("test_lower_controls", ["test_lower_controls.c"]),
                            ("test_studio_ui", ["test_studio_ui.c"]),
                            ("test_engine", ["test_engine.c"]),
                            ("test_midi_routes", ["test_midi_routes.c"]),
                            ("test_alpha_polish", ["test_alpha_polish.c"]),
                            ("test_settings", ["test_settings.c"])):
            target = build / (name + (".exe" if os.name == "nt" else ""))
            suite_sources = [ROOT / "tests/core" / filename for filename in extra]
            subprocess.run(compiler + common + native_target + ["-O2"] + native_checks + [str(path) for path in sources + suite_sources] +
                           ["-o", str(target)], cwd=ROOT, env=env, check=True)
            entry = {"name": name, "path": str(target.relative_to(ROOT)), "sha256": sha(target)}
            if args.test:
                suite = run_test([str(target)], env)
                print(suite.stdout, end="", flush=True)
                entry["tests_passed"] = True
                entry["output"] = suite.stdout.strip()
            suites.append(entry)
            tests.extend(suite_sources)
        built["state_suites"] = suites
    version = subprocess.run(compiler[:-1] + ["version"] if len(compiler) == 2 else compiler + ["--version"],
                             cwd=ROOT, env=env, text=True, capture_output=True, check=True).stdout.strip()
    for path,expected in frozen.items():
        if sha(path)!=expected:raise ValueError('Build input changed during compilation: '+str(path.relative_to(ROOT)))
    record = {"format": "thy666.core-build", "version": 1, "compiler": version, "artifacts": built,
              "source_sha256": {str(path.relative_to(ROOT)).replace('\\', '/'): frozen[path]
                                 for path in sources + list((ROOT / 'core').glob('*.h')) + [bridge,byte_runtime,byte_headers/'string.h']},
              "build_inputs_unchanged": True,
              "scope": "Host/WebAssembly build only; no MCU startup, drivers, bootloader or flash image"}
    if args.sanitize:
        record.update({"format": "thy666.core-sanitizer-build",
                       "sanitizer": {"kind": args.sanitize, "mode": "trap", "native_only": True,
                                     "symbolic_runtime_diagnostics": False,
                                     "assertions_enabled": True, "flags": native_checks},
                       "scope": "Native undefined-behavior trap checks only; "
                                "no WebAssembly, MCU build or device access"})
    if not args.wasm_only:
        tests.extend(sorted((ROOT / 'tests/core/fixtures').glob('*.h')))
        record["test_source_sha256"] = {str(path.relative_to(ROOT)).replace('\\', '/'): frozen[path] for path in tests}
    (output / "build-info.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    if args.sanitize:
        print(f"Native sanitizer results: {output / 'build-info.json'}", flush=True)
        return
    subprocess.run([sys.executable, str(ROOT / "tools/build_oled_profile.py")], cwd=ROOT, check=True)
    archive = env_path("THY666_CHORD_ARCHIVE", ROOT / ".tools/inputs/chords.lossless.json")
    if archive.exists() and not args.core_only:
        subprocess.run([sys.executable, str(ROOT / "tools/build_runtime_data.py")], cwd=ROOT, check=True)
        subprocess.run([sys.executable, str(ROOT / "tools/package_workbench.py")], cwd=ROOT, check=True)
    elif args.core_only:
        print("Core built; local preview packaging skipped.")
    else:
        print("Client ready. Archival workbench packaging requires a separately supplied lossless chord archive.")
    print(f"WebAssembly: {wasm} ({wasm.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()
