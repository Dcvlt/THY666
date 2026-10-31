"""Sanitizer wiring uses synthetic compiler results and never opens hardware."""
from __future__ import annotations

import ast
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_core


class CoreSanitizerTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "build/test-core-sanitizer"
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        tree = ast.parse((ROOT / "tools/build_core.py").read_text(encoding="utf-8"))
        sources = next(node.value for node in ast.walk(tree) if isinstance(node, ast.Assign)
                       and any(isinstance(t, ast.Name) and t.id == "sources" for t in node.targets))
        for name in ast.literal_eval(sources.generators[0].iter):
            self.write("core/" + name, b"synthetic C input\n")
        self.write("core/fixture.h", b"synthetic header\n")
        for source in (ROOT / "tests/core").glob("*.c"):
            self.write("tests/core/" + source.name, b"synthetic test input\n")
        for name in ("host/wasm_bridge.c", "firmware/diagnostic/runtime.c",
                     "firmware/diagnostic/freestanding/string.h"):
            self.write(name, b"synthetic input\n")
        self.original = {}
        for name in ("web/generated/core.wasm", "web/generated/build-info.json",
                     "web/generated/menu-schema.js", "web/generated/oled-profile.json"):
            data = ("original " + name).encode()
            self.write(name, data)
            self.original[name] = data
        self.calls = []
        self.probe_failure = None

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def compiler_run(self, command, **kwargs):
        command = list(map(str, command))
        self.calls.append(command)
        if "-o" in command:
            target = Path(command[command.index("-o") + 1])
            if self.probe_failure == "compile" and target.stem == "undefined-probe":
                raise subprocess.CalledProcessError(1, command, stderr="unsupported sanitizer")
            target.write_bytes(b"synthetic executable")
            return subprocess.CompletedProcess(command, 0, "", "")
        if command[-1] in ("version", "--version"):
            return subprocess.CompletedProcess(command, 0, "0.14.1\n", "")
        if Path(command[0]).stem == "undefined-probe":
            name = command[1] if len(command) > 1 else "clean"
            code = 0 if name == "clean" else 3
            if self.probe_failure == name:
                code = 1 if name == "clean" else 0
            return subprocess.CompletedProcess(command, code, "sanitizer-" + name + "\n", "")
        return subprocess.CompletedProcess(command, 0, "synthetic native suite passed\n", "")

    def run_main(self, args):
        with patch.object(build_core, "ROOT", self.root), \
                patch.object(build_core, "compiler_identity", return_value=(Path("pinned-zig"), {})) as identity, \
                patch.object(build_core, "source_prefix_flags", return_value=[]), \
                patch.object(build_core, "env_path", return_value=self.root / "absent-archive"), \
                patch.object(build_core.subprocess, "run", side_effect=self.compiler_run), \
                patch.object(sys, "argv", ["build_core.py", *args]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            build_core.main()
        return identity

    def test_sanitizer_runs_all_native_suites_and_preserves_standard_outputs(self):
        identity = self.run_main(["--sanitize=undefined"])
        identity.assert_called_once_with()
        for name, data in self.original.items():
            self.assertEqual((self.root / name).read_bytes(), data)
        self.assertFalse((self.root / "build/core").exists())
        reports = list((self.root / "build/core-ubsan").glob("*/build-info.json"))
        self.assertEqual(len(reports), 1)
        report = json.loads(reports[0].read_text())
        self.assertEqual(report["format"], "thy666.core-sanitizer-build")
        self.assertEqual(report["sanitizer"]["mode"], "trap")
        self.assertTrue(report["sanitizer"]["native_only"])
        self.assertTrue(report["sanitizer"]["assertions_enabled"])
        self.assertFalse(report["sanitizer"]["symbolic_runtime_diagnostics"])
        self.assertNotIn("webassembly", report["artifacts"])
        self.assertEqual(set(report["artifacts"]["sanitizer_probe"]["checks"]),
                         {"clean", "overflow", "assert"})
        suites = {row["name"] for row in report["artifacts"]["state_suites"]}
        for name in ("test_settings", "test_settings_transfer", "test_library", "test_sequencer",
                     "test_clock_transport", "test_generated_owners", "test_mpe"):
            self.assertIn(name, suites)
        self.assertGreater(len(suites), 35)
        compiles = [command for command in self.calls if "-o" in command]
        self.assertEqual(len(compiles), len(suites) + 2)  # Probe and combined core/MIDI suite.
        for command in compiles:
            for flag in build_core.undefined_flags():
                self.assertIn(flag, command)
            self.assertNotIn("-DNDEBUG", command)
            self.assertNotIn("host/wasm_bridge.c", " ".join(command).replace("\\", "/"))
            self.assertFalse(any("wasm32" in part for part in command))
        self.assertFalse(any(command[0] == sys.executable for command in self.calls))

    def test_each_probe_failure_stops_before_suites_or_passing_manifest(self):
        for failure in ("clean", "overflow", "assert", "compile"):
            with self.subTest(failure=failure):
                self.calls.clear()
                self.probe_failure = failure
                expected = subprocess.CalledProcessError if failure == "compile" else RuntimeError
                with self.assertRaises(expected):
                    self.run_main(["--sanitize=undefined"])
                self.assertEqual(sum("-o" in command for command in self.calls), 1)
                self.assertEqual(list((self.root / "build/core-ubsan").glob("*/build-info.json")), [])

    def test_sanitizer_and_wasm_only_are_rejected_before_any_work(self):
        with self.assertRaises(SystemExit) as rejected:
            self.run_main(["--sanitize=undefined", "--wasm-only"])
        self.assertEqual(rejected.exception.code, 2)
        self.assertEqual(self.calls, [])
        self.assertFalse((self.root / "build/core-ubsan").exists())

    def test_normal_build_keeps_its_flags_outputs_and_manifest(self):
        self.run_main(["--test", "--core-only"])
        self.assertFalse((self.root / "build/core-ubsan").exists())
        report = json.loads((self.root / "web/generated/build-info.json").read_text())
        self.assertEqual(report["format"], "thy666.core-build")
        self.assertNotIn("sanitizer", report)
        self.assertIn("webassembly", report["artifacts"])
        self.assertTrue((self.root / "build/core").is_dir())
        for command in self.calls:
            self.assertFalse(any(part.startswith("-fsanitize") or part.startswith("-fno-sanitize")
                                 for part in command))
        self.assertTrue(any("build_studio_schema.py" in " ".join(command) for command in self.calls))
        self.assertTrue(any("build_oled_profile.py" in " ".join(command) for command in self.calls))

    def test_explicit_clang_override_still_requires_probes(self):
        identity = self.run_main(["--sanitize=undefined", "--cc", "clang"])
        identity.assert_not_called()
        compiles = [command for command in self.calls if "-o" in command]
        self.assertTrue(all(command[0] == "clang" for command in compiles))
        self.assertEqual(sum(Path(command[0]).stem == "undefined-probe" for command in self.calls), 3)


if __name__ == "__main__":
    unittest.main()
