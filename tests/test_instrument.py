"""Integrated portable pad/control runtime, using the real musical core."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import unittest
from tools.native_compile import native_target_flags
import uuid

ROOT = Path(__file__).resolve().parents[1]


class InstrumentTests(unittest.TestCase):
    def test_integrated_controls_acquisition_ownership_and_drift(self):
        toolchain = ROOT / ".tools/config/core-toolchain.json"
        if not toolchain.exists():
            self.skipTest("Pinned native compiler is not prepared")
        record = json.loads(toolchain.read_text(encoding="utf-8"))
        compiler = Path(record["executable"])
        self.assertEqual(hashlib.sha256(compiler.read_bytes()).hexdigest(),
                         record["executable_sha256"])
        output = ROOT / ".tools/tmp" / ("instrument-" + uuid.uuid4().hex)
        output.mkdir(parents=True, exist_ok=False)
        binary = output / ("test_instrument.exe" if os.name == "nt" else "test_instrument")
        env = dict(os.environ)
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / ".tools/zig-cache/global")
        env["ZIG_LOCAL_CACHE_DIR"] = str(output / "zig-cache")
        env["TEMP"] = env["TMP"] = str(ROOT / ".tools/tmp")
        command = [str(compiler), "cc", *native_target_flags(), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   str(ROOT / "core/thy_core.c"), str(ROOT / "core/thy_crc32.c"), str(ROOT / "core/thy_midi.c"),
                   str(ROOT / "core/thy_instrument.c"),
            *[str(ROOT / "core" / name) for name in ("thy_chord_layout.c", "thy_voicing.c", "thy_library.c", "thy_expression_profile.c")],
                   str(ROOT / "tests/core/test_instrument.c"), "-o", str(binary)]
        compiled = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                  text=True, check=False, timeout=90)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, capture_output=True,
                                text=True, check=False, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("42 instrument regression tests passed", result.stdout)
        print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
