"""Actual registered-pad feedback and device-tested DMA driver, offline."""
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


class InstrumentLedTests(unittest.TestCase):
    def test_actual_bitmap_dma_and_registered_feedback(self):
        record_path = ROOT / ".tools/config/core-toolchain.json"
        if not record_path.exists():
            self.skipTest("Portable compiler is not prepared")
        record = json.loads(record_path.read_text(encoding="utf-8"))
        compiler = Path(record["executable"])
        if not compiler.exists():
            self.skipTest("Portable compiler executable is absent")
        self.assertEqual(hashlib.sha256(compiler.read_bytes()).hexdigest(), record["executable_sha256"])
        output = ROOT / ".tools/tmp" / ("instrument-led-" + uuid.uuid4().hex)
        output.mkdir(parents=True, exist_ok=False)
        env = dict(os.environ)
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / ".tools/zig-cache/global")
        env["ZIG_LOCAL_CACHE_DIR"] = str(output / "zig-cache")
        env["TEMP"] = env["TMP"] = str(ROOT / ".tools/tmp")
        binary = output / "test_instrument_led.exe"
        compiled = subprocess.run([str(compiler), "cc", *native_target_flags(), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            str(ROOT / "core/thy_core.c"), str(ROOT / "core/thy_midi.c"),
            str(ROOT / "core/thy_instrument.c"),
            *[str(ROOT / "core" / name) for name in ("thy_chord_layout.c", "thy_voicing.c", "thy_library.c", "thy_expression_profile.c")], str(ROOT / "core/thy_lighting.c"),
            str(ROOT / "tests/core/test_instrument_led.c"), "-o", str(binary)],
            cwd=ROOT, env=env, check=False, capture_output=True, text=True, timeout=60)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, check=False,
            capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("instrument bitmap LED encoding, DMA coalescing, registered feedback, OFF, lease, fault and wrap tests passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
