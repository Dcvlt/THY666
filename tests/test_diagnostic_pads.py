"""Actual ADC/mux pad driver with fake MMIO and lighting-isolation checks."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]


class DiagnosticPadsTests(unittest.TestCase):
    def test_actual_mux_adc_dma_lifecycle_and_lighting_isolation(self):
        record_path = ROOT / ".tools/config/core-toolchain.json"
        if not record_path.exists():
            self.skipTest("Portable compiler is not prepared")
        record = json.loads(record_path.read_text(encoding="utf-8"))
        compiler = Path(record["executable"])
        if not compiler.exists():
            self.skipTest("Portable compiler executable is absent")
        self.assertEqual(hashlib.sha256(compiler.read_bytes()).hexdigest(),
                         record["executable_sha256"])
        output = ROOT / ".tools/tmp" / ("diagnostic-pads-" + uuid.uuid4().hex)
        output.mkdir(parents=True, exist_ok=False)
        binary = output / "test_diagnostic_pads.exe"
        env = dict(os.environ)
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / ".tools/zig-cache/global")
        env["ZIG_LOCAL_CACHE_DIR"] = str(output / "zig-cache")
        env["TEMP"] = env["TMP"] = str(ROOT / ".tools/tmp")
        compiled = subprocess.run([str(compiler), "cc", "-std=c11", "-O2",
                                   "-Wall", "-Wextra", "-Werror",
                                   str(ROOT / "tests/core/test_diagnostic_pads.c"),
                                   "-o", str(binary)], cwd=ROOT, env=env,
                                  check=False, capture_output=True, text=True, timeout=60)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result = subprocess.run([str(binary)], cwd=ROOT, env=env,
                                check=False, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("diagnostic pad mux, ADC-only DMA lifecycle and lighting isolation tests passed", result.stdout)
        # The native harness runs the actual driver with DMA5 enabled and its
        # old 0x440 fault flags seeded throughout normal/failure/cleanup paths.
        # It forbids TIM1/SPI2/DMA1/DMA5 MMIO and checks PE3..5 are unchanged.
        header = ROOT / "analysis/local/stm32-reference/stm32f405xx.h"
        self.assertTrue(header.is_file(), "Pinned official CMSIS device header is required")
        self.assertEqual(hashlib.sha256(header.read_bytes()).hexdigest(),
                         "10c3b2dfc2b358d62a55668a7085ef07c44fc01abca8f2a08c2640a538040c7b")
        text = header.read_text(encoding="utf-8")
        self.assertRegex(text, r"#define\s+ADC_CR2_DMA_Pos\s+\(8U\)")
        self.assertRegex(text, r"#define\s+ADC_CR2_SWSTART_Pos\s+\(30U\)")


if __name__ == "__main__":
    unittest.main()
