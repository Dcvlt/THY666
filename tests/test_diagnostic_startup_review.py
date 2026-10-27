"""Startup review provenance/guard tests; reset execution has its own ELF tests."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import review_diagnostic_startup as review
from inspect_diagnostic_elf import inspect_elf
from test_diagnostic_elf import synthetic_elf


def record(path):
    raw = path.read_bytes()
    return {"bytes": len(raw), "sha256": review.digest(raw)}


class StartupReviewBindingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / "build")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.source = self.directory / "source.c"
        self.source.write_text("independent fixture source", encoding="utf-8")
        self.native_source = self.directory / "native.c"
        self.native_source.write_text("fixture source, never compiled here", encoding="utf-8")
        self.native = self.directory / ("test_diagnostic.exe" if sys.platform == "win32" else "test_diagnostic")
        self.native.write_bytes(b"Only hashed, never executed")
        self.elf = self.directory / "THY666-diagnostic.elf"
        self.elf.write_bytes(synthetic_elf())
        self.inspection, image = inspect_elf(self.elf.read_bytes())
        self.binary = self.directory / "THY666-diagnostic.bin"
        self.binary.write_bytes(image)
        self.saved = self.directory / "THY666-diagnostic.inspection.json"
        self.saved.write_text(json.dumps(self.inspection), encoding="utf-8")
        sources = {self.source.relative_to(ROOT).as_posix(): record(self.source)["sha256"],
                   "firmware/diagnostic/startup.S": record(ROOT / "firmware/diagnostic/startup.S")["sha256"]}
        self.manifest = {
            "format": "thy666.stm32-diagnostic-build", "usb_enabled": True,
            "flash_ready": False, "device_execution_ready": False,
            "build_id": review.digest(json.dumps(sources, sort_keys=True).encode())[:16],
            "build_script_sha256": record(ROOT / "tools/build_diagnostic.py")["sha256"],
            "source_sha256": sources,
            "artifacts": {path.name: record(path) for path in (self.elf, self.binary, self.saved)},
            "inspection": {key: self.inspection[key] for key in
                           ("entry", "execution_mode", "image_start", "image_end", "initial_stack", "memory_bytes")},
            "native_validation": {"passed": True, "output": "Synthetic recorded pass",
                                  "source": self.native_source.relative_to(ROOT).as_posix(),
                                  "source_sha256": record(self.native_source)["sha256"],
                                  "executable_sha256": record(self.native)["sha256"]},
        }
        self.manifest_path = self.directory / "THY666-diagnostic.manifest.json"
        self.save_manifest()
        # These tests isolate manifest/byte binding and file guards. Actual
        # ResetModel execution is tested against compiled ELF in
        # test_diagnostic_startup.py and run without mocks by the review CLI.
        self.models = mock.patch.object(review, "reset_contexts", return_value=[{"fixture": "model boundary"}])
        self.calls = mock.patch.object(review, "direct_call_review", return_value={"fixture": "call audit boundary"})
        self.models.start()
        self.calls.start()
        self.addCleanup(self.models.stop)
        self.addCleanup(self.calls.stop)

    def save_manifest(self):
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")

    def add_additional_native(self):
        source = self.directory / "extra.c"
        executable = self.directory / "extra.exe"
        source.write_bytes(b"additional native source")
        executable.write_bytes(b"Only hash this fixture; never execute")
        self.manifest["additional_native_validation"] = [{
            "passed": True, "output": "Synthetic additional pass",
            "source": source.relative_to(ROOT).as_posix(),
            "source_sha256": record(source)["sha256"],
            "executable_sha256": record(executable)["sha256"]}]
        self.save_manifest()
        return source, executable

    def test_additional_native_source_and_executable_are_bound_in_review(self):
        source, executable = self.add_additional_native()
        result = review.review_build(self.directory)
        self.assertEqual(result["checks"]["additional_native_validation_records_checked"], 1)
        for path in (source, executable):
            self.assertEqual(result["source_identities"][path.relative_to(ROOT).as_posix()], record(path))
        self.assertIn("tools/build_validation.py", result["source_identities"])
        self.assertFalse(result["checks"]["native_validation_rerun"])

    def test_additional_native_source_and_executable_drift_are_rejected(self):
        source, executable = self.add_additional_native()
        for path in (source, executable):
            original = path.read_bytes()
            path.write_bytes(original + b"drift")
            try:
                with self.subTest(path=path), self.assertRaisesRegex(ValueError, "Additional native validation"):
                    review.review_build(self.directory)
            finally:
                path.write_bytes(original)

    def test_additional_native_invalid_and_duplicate_records_are_rejected(self):
        self.add_additional_native()
        good = self.manifest["additional_native_validation"][0]
        for invalid in (None, {}, [0], [{**good, "passed": None}], [{**good, "output": ""}],
                        [{**good, "source": "../outside.c"}], [good, dict(good)],
                        [{**good, "source": self.manifest["native_validation"]["source"]}],
                        [{**good, "executable_sha256": False}]):
            self.manifest["additional_native_validation"] = invalid
            self.save_manifest()
            with self.subTest(value=invalid), self.assertRaisesRegex(ValueError, "Additional native validation"):
                review.review_build(self.directory)

    def test_fresh_report_binds_exact_build_and_keeps_offline_scope(self):
        result = review.review_build(self.directory)
        self.assertEqual(result["prepared_build"]["inspection"], self.inspection)
        self.assertEqual(result["prepared_build"]["build_id"], self.manifest["build_id"])
        self.assertEqual(result["source_identities"][self.elf.relative_to(ROOT).as_posix()], record(self.elf))
        self.assertIn("tests/test_diagnostic_startup.py", result["source_identities"])
        self.assertFalse(result["flash_ready"])
        self.assertFalse(result["hardware_checked"])
        self.assertFalse(result["device_execution_performed"])
        self.assertFalse(result["checks"]["native_validation_rerun"])
        self.assertEqual(result["device_actions"], [])
        self.assertEqual(result["checks"]["additional_native_validation_records_checked"], 0)

    def test_source_and_native_drift_are_rejected(self):
        for path, message in ((self.source, "Build source"), (self.native_source, "Native test source"),
                              (self.native, "Native executable")):
            original = path.read_bytes()
            path.write_bytes(original + b"drift")
            try:
                with self.subTest(path=path), self.assertRaisesRegex(ValueError, message):
                    review.review_build(self.directory)
            finally:
                path.write_bytes(original)

    def test_changed_raw_binary_is_rejected_even_after_manifest_hash_update(self):
        original = bytearray(self.binary.read_bytes())
        original[-1] ^= 1
        self.binary.write_bytes(original)
        self.manifest["artifacts"][self.binary.name] = record(self.binary)
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "reconstructed image differs"):
            review.review_build(self.directory)

    def test_unsafe_artifact_name_and_missing_native_pass_are_rejected(self):
        self.manifest["artifacts"]["../escape"] = {"sha256": "0" * 64}
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "Artifact name leaves"):
            review.review_build(self.directory)
        self.manifest["artifacts"].pop("../escape")
        self.manifest["native_validation"]["passed"] = False
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "no recorded native test pass"):
            review.review_build(self.directory)

    def test_historical_review_is_context_without_replaying_old_hardware_claims(self):
        baseline = self.directory / "historical.json"
        baseline.write_text(json.dumps({"format": "thy666.first-test-startup-review",
            "source_identities": {"firmware/diagnostic/startup.S": record(ROOT / "firmware/diagnostic/startup.S")},
            "prepared_build": {"build_id": "earlier-build"},
            "remaining_unknowns": ["stale observation that must not be copied"]}), encoding="utf-8")
        result = review.review_build(self.directory, historical_review=baseline)
        self.assertTrue(result["historical_context"]["startup_source_unchanged"])
        self.assertEqual(result["historical_context"]["build_id"], "earlier-build")
        self.assertNotIn("stale observation", json.dumps(result))
        self.assertEqual(result["source_identities"][baseline.relative_to(ROOT).as_posix()], record(baseline))

    def test_model_failure_does_not_emit_report_and_output_is_exclusive(self):
        output = self.directory / "review.json"
        argv = ["--build-dir", str(self.directory), "--output", str(output)]
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            with mock.patch.object(review, "reset_contexts", side_effect=AssertionError("unsupported opcode")):
                self.assertEqual(review.main(argv), 1)
                self.assertFalse(output.exists())
            self.assertEqual(review.main(argv), 0)
            original = output.read_bytes()
            self.assertEqual(review.main(argv), 1)
            self.assertEqual(output.read_bytes(), original)
            self.assertEqual(review.main(["--build-dir", str(self.directory), "--output",
                                          str(ROOT / "Resources/forbidden-startup-review.json")]), 1)


if __name__ == "__main__":
    unittest.main()
