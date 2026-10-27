"""Compile actual center encoder/CPU driver against fake MMIO, offline."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]


class DiagnosticCenterTests(unittest.TestCase):
    def test_actual_encoder_mappings_and_gpio_lifecycle(self):
        record_path = ROOT / ".tools/config/core-toolchain.json"
        if not record_path.exists():
            self.skipTest("Portable compiler is not prepared")
        record = json.loads(record_path.read_text(encoding="utf-8"))
        compiler = Path(record["executable"])
        if not compiler.exists():
            self.skipTest("Portable compiler executable is absent")
        self.assertEqual(hashlib.sha256(compiler.read_bytes()).hexdigest(),
                         record["executable_sha256"])
        output = ROOT / ".tools/tmp" / ("diagnostic-center-" + uuid.uuid4().hex)
        output.mkdir(parents=True, exist_ok=False)
        binary = output / "test_diagnostic_center.exe"
        env = dict(os.environ)
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / ".tools/zig-cache/global")
        env["ZIG_LOCAL_CACHE_DIR"] = str(output / "zig-cache")
        env["TEMP"] = env["TMP"] = str(ROOT / ".tools/tmp")
        compiled = subprocess.run([str(compiler), "cc", "-std=c11", "-O2",
                                   "-Wall", "-Wextra", "-Werror",
                                   str(ROOT / "tests/core/test_diagnostic_center.c"),
                                   "-o", str(binary)], cwd=ROOT, env=env,
                                  check=False, capture_output=True, text=True, timeout=60)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, check=False,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("diagnostic center encoder, mappings and CPU GPIO lifecycle tests passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
