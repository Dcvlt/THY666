"""Synthetic release preparation checks; the native inspector is mocked.

No vendor package, USB access, executable launch or network request is required.
The Rust catalog tests separately exercise authoritative selection/revalidation.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("prepare_updater_releases", ROOT / "tools/prepare_updater_releases.py")
prepare = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = prepare
SPEC.loader.exec_module(prepare)
BUILD = "1234567890abcdef"


def synthetic_package(encrypted: bool) -> bytes:
    size = 112 if encrypted else 512
    fields = [(6, 1), (6, 0), (6, 1), (6, 0), (6, size),
              (8, 0xEBF3A7F7), (8, 0x429B51D6), (8, 0xAA017D8C),
              (6, 4), (6, int(encrypted)), (6, 1)]
    app = bytearray((i * 37 + 11) & 255 for i in range(size))
    if not encrypted:
        app[:] = bytes(size)
        app[:4] = (0x20020000).to_bytes(4, "little")
        app[4:8] = (0x08040000 + 393).to_bytes(4, "little")
        app[416:432] = BUILD.encode()
    return b"THY333" + "".join(f"{value:0{width}X}" for width, value in fields).encode() + app


class ReleasePreparationTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="updater-releases-", dir=ROOT / "build")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.thy666 = self.base / "THY666-instrument-alpha-12345678.333"
        self.stock = self.base / "original.333"
        self.thy666.write_bytes(synthetic_package(False))
        self.stock.write_bytes(synthetic_package(True))
        self.updater = self.base / "synthetic-updater.exe"
        self.updater.write_bytes(b"Mocked inspector; this file is never executed")
        self.output = self.base / "release"
        self.overrides = {}
        self.hook = None
        self.stdout = None
        self.calls = []
        # Only synthetic tests replace this constant. Production has no allowlist
        # override and the original image is never copied into a test fixture.
        patcher = mock.patch.object(prepare, "STOCK_V5_SHA256", hashlib.sha256(self.stock.read_bytes()).hexdigest())
        patcher.start(); self.addCleanup(patcher.stop)
        patcher = mock.patch.object(prepare.subprocess, "run", side_effect=self.fake_inspect)
        self.native = patcher.start(); self.addCleanup(patcher.stop)

    def fake_inspect(self, command, **options):
        self.assertEqual(command[0], str(self.updater))
        self.assertEqual(command[1], "inspect")
        self.assertTrue(len(command) == 3 or (len(command) == 5 and command[3] == "--expected-build"))
        self.assertEqual(options["timeout"], 30)
        self.assertTrue(options["check"])
        self.assertNotIn("shell", options)
        self.calls.append(command)
        path = Path(command[2]); raw = path.read_bytes()
        kind = "stock" if raw[66:72] == b"000001" else "thy666"
        info = {"kind": kind, "installable": True, "file_bytes": len(raw),
                "file_sha256": hashlib.sha256(raw).hexdigest(),
                "build_id": BUILD if kind == "thy666" else None,
                "build_identity_verified": kind == "thy666"}
        info.update(self.overrides.get(kind, {}))
        value = {"protocol": prepare.PROTOCOL, "simulation": False, "package": info}
        if self.hook:
            self.hook(path, kind, value)
        return subprocess.CompletedProcess(command, 0, self.stdout if self.stdout is not None else json.dumps(value), "")

    def run_prepare(self, **changes):
        options = dict(thy666=self.thy666, stock=self.stock, updater=self.updater, output_dir=self.output)
        options.update(changes)
        return prepare.prepare_releases(**options)

    def old_catalog(self, value=b"previous catalog"):
        self.output.mkdir(exist_ok=True)
        path = self.output / "releases.json"
        path.write_bytes(value)
        return path

    def test_exact_catalog_schema_and_sibling_hashes_preserve_inputs(self):
        before = [p.read_bytes() for p in [self.thy666, self.stock]]
        path = self.run_prepare()
        catalog = json.loads(path.read_text())
        self.assertEqual(set(catalog), {"format", "version", "releases"})
        self.assertEqual((catalog["format"], catalog["version"]), ("thy666.release-catalog", 1))
        self.assertEqual([e["id"] for e in catalog["releases"]], ["latest-thy666", "original-thy333"])
        for entry, data in zip(catalog["releases"], before):
            self.assertEqual(set(entry), {"id", "label", "kind", "file", "build_id", "sha256"})
            self.assertEqual(Path(entry["file"]).name, entry["file"])
            self.assertTrue(entry["file"].endswith(".333"))
            self.assertEqual((path.parent / entry["file"]).read_bytes(), data)
            self.assertEqual(entry["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(catalog["releases"][0]["build_id"], BUILD)
        self.assertIsNone(catalog["releases"][1]["build_id"])
        self.assertEqual([p.read_bytes() for p in [self.thy666, self.stock]], before)
        self.assertNotIn(str(self.base), path.read_text())
        self.assertEqual(len(self.calls), 4)  # Both explicit inputs and both copied files.

    def test_identical_repeated_preparation_reuses_files_without_replacing_catalog(self):
        path = self.run_prepare()
        before = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in self.output.iterdir()}
        with mock.patch.object(prepare.os, "replace", wraps=os.replace) as replace:
            self.assertEqual(self.run_prepare(), path)
            replace.assert_not_called()
        self.assertEqual({p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in self.output.iterdir()}, before)

    def test_conflicting_curated_bytes_are_not_overwritten_or_republished(self):
        path = self.run_prepare(); previous = path.read_bytes()
        entry = json.loads(previous)["releases"][0]
        target = path.parent / entry["file"]
        target.write_bytes(b"different bytes")
        with self.assertRaisesRegex(ValueError, "different bytes"):
            self.run_prepare()
        self.assertEqual(target.read_bytes(), b"different bytes")
        self.assertEqual(path.read_bytes(), previous)

    def test_wrong_kind_digest_length_and_unapproved_build_never_replace_catalog(self):
        path = self.old_catalog()
        changes = [{"kind": "stock"}, {"installable": False}, {"file_sha256": "0" * 64},
                   {"file_bytes": 1}, {"build_id": None}, {"build_identity_verified": False},
                   {"build_id": "ABCDEF0123456789"}]
        for change in changes:
            with self.subTest(change=change):
                self.overrides = {"thy666": change}
                with self.assertRaises(ValueError):
                    self.run_prepare()
                self.assertEqual(path.read_bytes(), b"previous catalog")
                self.assertEqual([p.name for p in self.output.iterdir()], ["releases.json"])

    def test_stock_allowlist_and_absence_of_thy666_identity_are_required(self):
        for change in [{"build_id": BUILD}, {"build_identity_verified": True}]:
            with self.subTest(change=change):
                self.overrides = {"stock": change}
                with self.assertRaises(ValueError): self.run_prepare()
                self.assertFalse(self.output.exists())
        self.overrides = {}
        changed = bytearray(self.stock.read_bytes()); changed[-1] ^= 1; self.stock.write_bytes(changed)
        with self.assertRaisesRegex(ValueError, "exact allowlisted"):
            self.run_prepare()
        self.assertFalse(self.output.exists())

    def test_source_change_during_inspection_preserves_existing_catalog(self):
        path = self.old_catalog()
        def change(source, kind, _):
            if source == self.thy666:
                source.write_bytes(source.read_bytes()[:-1] + b"x")
        self.hook = change
        with self.assertRaisesRegex(ValueError, "source changed"):
            self.run_prepare()
        self.assertEqual(path.read_bytes(), b"previous catalog")

    def test_copied_file_corruption_is_caught_before_catalog_publication(self):
        path = self.old_catalog()
        def corrupt(source, kind, _):
            if source.parent == self.output and kind == "stock":
                source.write_bytes(source.read_bytes()[:-1] + b"x")
        self.hook = corrupt
        with self.assertRaisesRegex(ValueError, "source changed"):
            self.run_prepare()
        self.assertEqual(path.read_bytes(), b"previous catalog")

    def test_failed_atomic_replacement_preserves_previous_catalog_and_removes_only_staging_manifest(self):
        path = self.old_catalog()
        with mock.patch.object(prepare.os, "replace", side_effect=OSError("synthetic replace failure")):
            with self.assertRaises(OSError): self.run_prepare()
        self.assertEqual(path.read_bytes(), b"previous catalog")
        self.assertFalse(list(self.output.glob(".releases-*.tmp")))
        self.assertEqual(len(list(self.output.glob("*.333"))), 2)

    def test_concurrent_catalog_change_is_preserved(self):
        path = self.old_catalog()
        def concurrent(source, kind, _):
            if source.parent == self.output and kind == "stock": path.write_bytes(b"concurrent catalog")
        self.hook = concurrent
        with self.assertRaisesRegex(ValueError, "catalog changed"):
            self.run_prepare()
        self.assertEqual(path.read_bytes(), b"concurrent catalog")

    def test_explicit_build_is_sent_only_to_thy666_inspections(self):
        self.run_prepare(expected_build=BUILD)
        for command in self.calls:
            data = Path(command[2]).read_bytes()
            if data[66:72] == b"000001": self.assertEqual(len(command), 3)
            else: self.assertEqual(command[3:], ["--expected-build", BUILD])
        self.calls.clear()
        with self.assertRaisesRegex(ValueError, "16 lowercase"):
            self.run_prepare(expected_build="NOT-A-BUILD")
        self.assertEqual(self.calls, [])

    def test_inspector_error_malformed_or_wrong_protocol_responses_stop_without_outputs(self):
        for text in ["not JSON", "[]", json.dumps({"protocol": "wrong", "package": {}})]:
            with self.subTest(text=text):
                self.stdout = text
                with self.assertRaises(ValueError): self.run_prepare()
                self.assertFalse(self.output.exists())
        self.stdout = None
        self.native.side_effect = subprocess.TimeoutExpired(["inspect"], 30)
        with self.assertRaisesRegex(ValueError, "Offline updater inspection failed"):
            self.run_prepare()
        self.assertFalse(self.output.exists())

    def test_oversized_input_and_links_are_rejected_before_inspection(self):
        self.thy666.write_bytes(bytes(prepare.PACKAGE_BYTES_MAX + 1))
        with self.assertRaisesRegex(ValueError, "format limit"):
            self.run_prepare()
        self.native.assert_not_called()
        self.thy666.write_bytes(synthetic_package(False))
        original = Path.lstat
        def linked(path):
            return SimpleNamespace(st_mode=stat.S_IFLNK) if path == self.thy666 else original(path)
        with mock.patch.object(Path, "lstat", autospec=True, side_effect=linked):
            with self.assertRaisesRegex(ValueError, "not a link"):
                self.run_prepare()
        self.native.assert_not_called()


if __name__ == "__main__":
    unittest.main()
