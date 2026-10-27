"""Native actual console/detector/core tests; mocks never open hardware."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]


class InstrumentProtocolTests(unittest.TestCase):
    def test_actual_console_with_real_core_and_journal(self):
        record_path = ROOT / ".tools/config/core-toolchain.json"
        if not record_path.exists():
            self.skipTest("Portable compiler is not prepared")
        record = json.loads(record_path.read_text(encoding="utf-8"))
        compiler = Path(record["executable"])
        if not compiler.exists():
            self.skipTest("Portable compiler executable is absent")
        self.assertEqual(hashlib.sha256(compiler.read_bytes()).hexdigest(), record["executable_sha256"])
        output = ROOT / ".tools/tmp" / ("instrument-protocol-" + uuid.uuid4().hex)
        output.mkdir(parents=True, exist_ok=False)
        env = dict(os.environ)
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / ".tools/zig-cache/global")
        env["ZIG_LOCAL_CACHE_DIR"] = str(output / "zig-cache")
        env["TEMP"] = env["TMP"] = str(ROOT / ".tools/tmp")
        binary = output / "test_instrument_protocol.exe"
        sources = [ROOT / "tests/core/test_instrument_protocol.c", *sorted((ROOT / "core").glob("*.c")),
            ROOT / "firmware/instrument/protocol.c", ROOT / "firmware/instrument/transport.c",
            ROOT / "firmware/instrument/touch_config.c"]
        includes = [ROOT / "core", ROOT / "firmware/instrument", ROOT / "firmware/diagnostic"]
        flags = [flag for include in includes for flag in ("-I", str(include))]
        compiled = subprocess.run([str(compiler), "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            *flags, *map(str, sources), "-o", str(binary)], cwd=ROOT, env=env, check=False,
            capture_output=True, text=True, timeout=60)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, check=False,
            capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("instrument protocol:", result.stdout)
        self.assertIn("read-only ownership and panic passed", result.stdout)
        print(result.stdout.rstrip())
        fixtures = subprocess.run([str(binary), "--fixtures"], cwd=ROOT, env=env, check=False,
            capture_output=True, timeout=10)
        self.assertEqual(fixtures.returncode, 0, fixtures.stderr.decode("utf-8", errors="replace"))
        # Windows CRT converts explicit CRLF through a text-mode stdout into
        # CRCRLF, so preserve the protocol bytes by normalizing that CRT layer.
        wire = fixtures.stdout.replace(b"\r\r\n", b"\r\n")
        self.assertEqual(wire.count(b"\r\n"), 4)
        self.assertEqual([line.split(b" ", 1)[0] for line in wire.splitlines()],
            [b"OK", b"PAD", b"EDGES", b"IO"])
        fixture_path = output / "protocol-fixtures.txt"
        fixture_path.write_bytes(wire)
        print("Actual protocol parser fixtures: " + str(fixture_path))


if __name__ == "__main__":
    unittest.main()
