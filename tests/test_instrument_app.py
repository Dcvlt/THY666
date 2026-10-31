"""Actual instrument main loop with mocked hardware and real musical layers."""
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


class InstrumentAppTests(unittest.TestCase):
    def test_actual_application_din_standalone_and_usb_isolation(self):
        self.run_fixture('test_instrument_app','instrument actual app tests passed: 9 scenarios',
                         separate_profile=True)

    def test_actual_application_profile_on_off_equivalence(self):
        self.run_fixture('test_instrument_profile_app','Instrument profiling app: 2 scenarios')

    def test_profile_commands_clocks_and_session_boundaries(self):
        self.run_fixture('test_instrument_profile','Instrument profiling: 7 scenarios')

    def run_fixture(self,name,summary,*,separate_profile=False):
        record_path = ROOT / ".tools/config/core-toolchain.json"
        if not record_path.exists():
            self.skipTest("Pinned native compiler is not prepared")
        record = json.loads(record_path.read_text(encoding="utf-8"))
        compiler = Path(record["executable"])
        self.assertEqual(hashlib.sha256(compiler.read_bytes()).hexdigest(),
                         record["executable_sha256"])
        output = ROOT / ".tools/tmp" / ("instrument-app-" + uuid.uuid4().hex)
        output.mkdir(parents=True, exist_ok=False)
        # Only the endpoint declaration is fake. app.c itself is included by
        # the C test, with its CPU-assembly seam disabled on the host.
        (output / "tusb.h").write_text(
            "#include <stdbool.h>\n#include <stdint.h>\n"
            "bool tud_midi_packet_write(const uint8_t packet[4]);\n",
            encoding="utf-8")
        binary = output / (name + '.exe')
        env = dict(os.environ)
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / ".tools/zig-cache/global")
        env["ZIG_LOCAL_CACHE_DIR"] = str(output / "zig-cache")
        env["TEMP"] = env["TMP"] = str(ROOT / ".tools/tmp")
        command = [str(compiler), "cc", *native_target_flags(), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-I", str(output), "-I", str(ROOT / "core"),
                   "-I", str(ROOT / "firmware/diagnostic"),
                   *map(str, sorted((ROOT / "core").glob("*.c"))),
                   str(ROOT / "firmware/instrument/transport.c"),
                   *([str(ROOT / "firmware/instrument/profile.c")] if separate_profile else []),
                   str(ROOT / 'tests/core' / (name + '.c')), "-o", str(binary)]
        compiled = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                  text=True, check=False, timeout=60)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, capture_output=True,
                                text=True, check=False, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(summary, result.stdout)
        print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
