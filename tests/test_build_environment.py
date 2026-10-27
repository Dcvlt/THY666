"""Local build settings select inputs without weakening identity checks."""
import contextlib
import hashlib
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
import build_diagnostic
import chord_dump
from test_runtime_data import fixture


class BuildEnvironmentTests(unittest.TestCase):
    def setUp(self):
        folder = ROOT / "build/test-build-environment"
        folder.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=folder)
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.compiler = self.folder / "configured-compiler"
        self.compiler.write_bytes(b"synthetic compiler identity; never executed")
        self.digest = hashlib.sha256(self.compiler.read_bytes()).hexdigest()

    def identity(self, path=None, digest=None):
        with patch.object(build_diagnostic, "ROOT", self.folder), \
                patch.object(build_diagnostic, "env_path", return_value=path), \
                patch.object(build_diagnostic, "env_value", return_value=digest):
            return build_diagnostic.compiler_identity()

    def test_configured_compiler_requires_exact_pin(self):
        executable, record = self.identity(self.compiler, self.digest.upper())
        self.assertEqual(executable, self.compiler)
        self.assertEqual(record["executable_sha256"], self.digest)
        self.assertEqual(record["version"], "configured")
        self.compiler.write_bytes(b"changed identity")
        with self.assertRaisesRegex(ValueError, "identity changed"):
            self.identity(self.compiler, self.digest)

    def test_compiler_override_rejects_incomplete_or_invalid_pin(self):
        for path, digest in ((self.compiler, None), (None, self.digest),
                             (self.compiler, "not-a-sha256")):
            with self.subTest(has_path=bool(path), has_digest=bool(digest)):
                with self.assertRaises(ValueError):
                    self.identity(path, digest)

    def test_default_manifest_remains_pinned(self):
        path = self.folder / ".tools/config/core-toolchain.json"
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps({"version": "synthetic", "executable": str(self.compiler),
                                    "executable_sha256": self.digest}), encoding="utf-8")
        executable, record = self.identity()
        self.assertEqual(executable, self.compiler)
        self.assertEqual(record["version"], "synthetic")
        self.compiler.write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "identity changed"):
            self.identity()

    def test_chord_archive_process_setting_selects_default_input(self):
        script = (
            "import sys; from pathlib import Path; "
            "sys.path.insert(0, 'tools'); from build_runtime_data import DEFAULT_ARCHIVE; "
            "raise SystemExit(DEFAULT_ARCHIVE != Path(sys.argv[1]).resolve())"
        )
        import os
        expected = self.folder / "external.lossless.json"
        env = {**os.environ, "THY666_CHORD_ARCHIVE": str(expected)}
        result = subprocess.run([sys.executable, "-c", script, str(expected)], cwd=ROOT,
                                env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, "Configured archive was not selected")

    def test_importer_uses_configured_dump_but_explicit_argument_wins(self):
        first = self.folder / "configured-dump.bin"
        second = self.folder / "explicit-dump.bin"
        first.write_bytes(fixture())
        second.write_bytes(fixture()[:-1] + b"\x7e")
        for name, arguments, expected in (("configured", [], first),
                                          ("explicit", [str(second)], second)):
            output = self.folder / name
            with patch.object(chord_dump, "env_path", side_effect=lambda key, default=None: first if key == "THY666_CHORD_DUMP" else default), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(chord_dump.main(["import", *arguments, "--output-dir", str(output)]), 0)
            saved = json.loads((output / "chords.lossless.json").read_text(encoding="utf-8"))
            self.assertEqual(saved["source"]["sha256"], hashlib.sha256(expected.read_bytes()).hexdigest())

    def test_missing_dump_reports_configuration_requirement(self):
        with patch.object(chord_dump, "env_path", return_value=None), \
                contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                chord_dump.main(["import"])
        self.assertEqual(error.exception.code, 2)

    def test_importer_writes_configured_archive_without_requiring_fixed_filename(self):
        source = self.folder / "source.bin"
        source.write_bytes(fixture())
        destination = self.folder / "private-inputs/selected.lossless.json"
        settings = {"THY666_CHORD_DUMP": source, "THY666_CHORD_ARCHIVE": destination}
        with patch.object(chord_dump, "env_path", side_effect=lambda key, default=None: settings.get(key, default)), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(chord_dump.main(["import"]), 0)
        self.assertTrue(destination.is_file())
        self.assertTrue((destination.parent / "validation.json").is_file())
        self.assertEqual((destination.parent / "roundtrip.bin").read_bytes(), source.read_bytes())


if __name__ == "__main__":
    unittest.main()
