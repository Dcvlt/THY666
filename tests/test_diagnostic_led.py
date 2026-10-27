"""Compile actual opt-in LED encoder/DMA lifecycle against fake MMIO, offline."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
import sys
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]


class DiagnosticLedTests(unittest.TestCase):
    def test_actual_encoder_and_dma_lifecycle(self):
        record_path = ROOT / ".tools/config/core-toolchain.json"
        if not record_path.exists():
            self.skipTest("Portable compiler is not prepared")
        record = json.loads(record_path.read_text(encoding="utf-8"))
        compiler = Path(record["executable"])
        if not compiler.exists():
            self.skipTest("Portable compiler executable is absent")
        self.assertEqual(hashlib.sha256(compiler.read_bytes()).hexdigest(),
                         record["executable_sha256"])
        output = ROOT / ".tools/tmp" / ("diagnostic-led-" + uuid.uuid4().hex)
        output.mkdir(parents=True, exist_ok=False)
        binary = output / "test_diagnostic_led.exe"
        fixtures = output / "encoded-frames.bin"
        env = dict(os.environ)
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / ".tools/zig-cache/global")
        env["ZIG_LOCAL_CACHE_DIR"] = str(output / "zig-cache")
        env["TEMP"] = env["TMP"] = str(ROOT / ".tools/tmp")
        compiled = subprocess.run([str(compiler), "cc", "-std=c11", "-O2",
                                   "-Wall", "-Wextra", "-Werror",
                                   str(ROOT / "tests/core/test_diagnostic_led.c"),
                                   "-o", str(binary)], cwd=ROOT, env=env,
                                  check=False, capture_output=True, text=True, timeout=60)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result = subprocess.run([str(binary), str(fixtures)], cwd=ROOT, env=env, check=False,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("diagnostic LED encoder and DMA lifecycle tests passed", result.stdout)
        # Independent wire-contract reference: each component is MSB-first,
        # with distinct 16-bit symbols and a reset interval on both sides.
        def led_words(triplets):
            return [0xffc0 if component & (1 << bit) else 0xf800
                    for triplet in triplets for component in triplet
                    for bit in range(7, -1, -1)]

        def decode_led_words(words):
            components = []
            for offset in range(0, len(words), 8):
                value = 0
                for symbol in words[offset:offset + 8]:
                    self.assertIn(symbol, (0xf800, 0xffc0))
                    value = (value << 1) | (symbol == 0xffc0)
                components.append(value)
            return [components[i:i + 3] for i in range(0, len(components), 3)]

        raw = fixtures.read_bytes()
        words_per_frame, reset_words = 2816, 256
        self.assertEqual(len(raw), 4 * words_per_frame * 2)
        for i, (mode, pixel, component) in enumerate(
                (("pixel", 0, 0), ("pixel", 95, 2), ("all", 0, 1), ("off", 0, 0))):
            words = list(struct.unpack_from("<" + "H" * words_per_frame,
                                            raw, i * words_per_frame * 2))
            triplets = [[0, 0, 0] for _ in range(96)]
            if mode == "all":
                for triplet in triplets:
                    triplet[component] = 8
            elif mode == "pixel":
                triplets[pixel][component] = 8
            encoded = led_words(triplets)
            self.assertEqual(words, [0] * reset_words + encoded + [0] * reset_words)
            self.assertEqual(decode_led_words(words[reset_words:-reset_words]),
                             triplets)


if __name__ == "__main__":
    unittest.main()
