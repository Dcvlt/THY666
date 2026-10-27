"""Deterministic updater ZIPs with synthetic artifacts and a mocked inspector.

No executable is launched, no vendor fixture is included and no device is used.
"""
from __future__ import annotations

import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import prepare_updater_releases as release_tools

SPEC = importlib.util.spec_from_file_location("package_updater", ROOT / "tools/package_updater.py")
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)
BUILD = "1234567890abcdef"


def sha(data):
    return hashlib.sha256(data).hexdigest()


class UpdaterPackageTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="updater-package-", dir=ROOT / "build")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.release = self.root / "build/updater-target/release"
        self.release.mkdir(parents=True)
        (self.root / "updater").mkdir()
        (self.root / "updater/Cargo.toml").write_text('[package]\nversion = "0.1.0"\n')
        self.exe = self.release / "thy666-updater.exe"
        self.exe.write_bytes(b"Synthetic executable; never launched")
        (self.root / "LICENSE").write_bytes(b"Synthetic project license")
        (self.root / "updater/THIRD_PARTY_LICENSES.txt").write_bytes(b"Synthetic dependency notices")
        for name, data in [("LICENSE", b"Synthetic project license"), ("THIRD_PARTY_LICENSES.txt", b"Synthetic dependency notices")]:
            (self.release / name).write_bytes(data)
        self.custom = b"THY333" + bytes(72) + b"synthetic custom data"
        self.stock = b"THY333" + bytes(72) + b"synthetic stock data"
        self.entries = [{"id": "current", "label": "Synthetic THY666", "kind": "thy666", "file": "current.333", "build_id": BUILD, "sha256": sha(self.custom)},
                        {"id": "stock", "label": "Synthetic original", "kind": "stock", "file": "stock.333", "build_id": None, "sha256": sha(self.stock)}]
        (self.release / "current.333").write_bytes(self.custom)
        (self.release / "stock.333").write_bytes(self.stock)
        self.catalog = self.release / "releases.json"
        self.write_catalog()
        self.manifest_path = self.root / "build/updater-target/updater-build.json"
        self.manifest = {"format": "thy666.updater-build", "version": 1, "host": "x86_64-pc-windows-msvc", "device_access_performed": False,
                         "artifact": {**self.record(self.exe), "private_path_check": "passed", "static_msvc_runtime_checked": True},
                         "license_files": [self.record(self.release / name) for name in ("LICENSE", "THIRD_PARTY_LICENSES.txt")]}
        self.write_manifest()
        self.calls = []
        self.version = "0.1.0"
        self.hook = None
        for target, name, value in [(package, "ROOT", self.root), (release_tools, "STOCK_V5_SHA256", sha(self.stock))]:
            patcher = mock.patch.object(target, name, value); patcher.start(); self.addCleanup(patcher.stop)
        patcher = mock.patch.object(package, "verify_artifact", side_effect=self.verify)
        patcher.start(); self.addCleanup(patcher.stop)
        patcher = mock.patch.object(package.subprocess, "run", side_effect=self.inspect)
        patcher.start(); self.addCleanup(patcher.stop)

    def record(self, path):
        data = path.read_bytes()
        return {"output": path.relative_to(self.root).as_posix(), "bytes": len(data), "sha256": sha(data)}

    def write_manifest(self):
        self.manifest_path.write_text(json.dumps(self.manifest))

    def write_catalog(self):
        self.catalog.write_text(json.dumps({"format": "thy666.release-catalog", "version": 1, "releases": self.entries}))

    def verify(self, path, host):
        self.assertEqual(path, self.exe)
        self.assertEqual(host, "x86_64-pc-windows-msvc")
        return {**self.record(path), "private_path_check": "passed", "static_msvc_runtime_checked": True}

    def inspect(self, command, **options):
        self.calls.append(command)
        self.assertEqual(command[0], str(self.exe))
        self.assertNotIn("shell", options)
        if command[1] == "--version":
            self.assertEqual(command, [str(self.exe), "--version"])
            return subprocess.CompletedProcess(command, 0, self.version + "\n", "")
        self.assertEqual(command[1], "inspect")
        self.assertIn(len(command), (3, 5))
        raw = Path(command[2]).read_bytes()
        kind = "stock" if raw == self.stock else "thy666"
        info = {"kind": kind, "installable": True, "file_bytes": len(raw), "file_sha256": sha(raw),
                "build_id": BUILD if kind == "thy666" else None, "build_identity_verified": kind == "thy666"}
        if kind == "thy666":
            self.assertEqual(command[3:], ["--expected-build", BUILD])
        else:
            self.assertEqual(len(command), 3)
        if self.hook:
            self.hook(command, info)
        return subprocess.CompletedProcess(command, 0, json.dumps({"protocol": "THY666-UPDATER/1", "simulation": False, "package": info}), "")

    def run_package(self):
        return package.package_updater(self.manifest_path)

    def archive_bytes(self):
        return (self.root / "build/releases/THY666-updater-0.1.0-windows-x64.zip").read_bytes()

    def test_exact_flat_contents_hashes_and_reproducible_download_bytes(self):
        first = self.run_package(); data = self.archive_bytes()
        self.assertEqual(first["sha256"], sha(data)); self.assertFalse(first["device_access_performed"])
        self.assertEqual((self.root / first["download"]).read_bytes(), data)
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            self.assertEqual(set(archive.namelist()), {"thy666-updater.exe", "LICENSE", "THIRD_PARTY_LICENSES.txt", "QUICKSTART.txt", "releases.json", "current.333", "stock.333"})
            self.assertEqual(archive.read("current.333"), self.custom)
            self.assertEqual(archive.read("stock.333"), self.stock)
            self.assertIn(b"http://127.0.0.1:3330/", archive.read("QUICKSTART.txt"))
            self.assertNotIn(str(self.root).encode(), data)
            self.assertTrue(all(info.date_time == (1980, 1, 1, 0, 0, 0) for info in archive.infolist()))
        self.assertEqual(self.run_package()["sha256"], first["sha256"])
        self.assertEqual(self.archive_bytes(), data)
        self.assertTrue(all(command[1] in ("--version", "inspect") for command in self.calls))

    def test_unlisted_files_and_private_configuration_are_never_included(self):
        (self.release / "extra.333").write_bytes(b"not listed")
        (self.release / ".env").write_bytes(b"SYNTHETIC_SECRET=never include")
        self.run_package()
        with zipfile.ZipFile(io.BytesIO(self.archive_bytes())) as archive:
            self.assertNotIn("extra.333", archive.namelist()); self.assertNotIn(".env", archive.namelist())

    def test_without_catalog_packages_only_helper_and_required_notices(self):
        self.catalog.unlink(); self.run_package()
        with zipfile.ZipFile(io.BytesIO(self.archive_bytes())) as archive:
            self.assertEqual(len(archive.namelist()), 4)
            self.assertIn(b"No firmware catalog", archive.read("QUICKSTART.txt"))

    def test_invalid_binary_identity_runtime_path_or_version_preserves_old_download(self):
        self.run_package(); before = self.archive_bytes()
        original = copy.deepcopy(self.manifest)
        for field, value in [("sha256", "0" * 64), ("bytes", 1), ("private_path_check", "failed"), ("static_msvc_runtime_checked", False), ("output", "../outside.exe")]:
            self.manifest = copy.deepcopy(original); self.manifest["artifact"][field] = value; self.write_manifest()
            with self.assertRaises(ValueError): self.run_package()
            self.assertEqual(self.archive_bytes(), before)
        self.manifest = original; self.write_manifest(); self.version = "9.9.9"
        with self.assertRaises(ValueError): self.run_package()

    def test_missing_changed_duplicate_or_unlisted_license_records_are_rejected(self):
        original = copy.deepcopy(self.manifest)
        for notices in [[], [original["license_files"][0]], [original["license_files"][0]] * 2]:
            self.manifest = copy.deepcopy(original); self.manifest["license_files"] = notices; self.write_manifest()
            with self.assertRaises(ValueError): self.run_package()
        self.manifest = original; self.write_manifest()
        (self.release / "LICENSE").write_bytes(b"changed")
        with self.assertRaises(ValueError): self.run_package()

    def test_catalog_paths_digests_kinds_builds_duplicates_and_counts_are_bounded(self):
        original = copy.deepcopy(self.entries)
        for key, value in [("file", "../current.333"), ("file", "other/current.333"), ("file", "other\\current.333"), ("file", "C:current.333"),
                           ("file", "CON.333"), ("sha256", "0" * 64), ("kind", "other"), ("build_id", "12345678")]:
            self.entries = copy.deepcopy(original); self.entries[0][key] = value; self.write_catalog()
            with self.assertRaises(ValueError, msg=key): self.run_package()
        for entries in [[original[0], original[0]], original * 33]:
            self.entries = entries; self.write_catalog()
            with self.assertRaises(ValueError): self.run_package()

    def test_authoritative_inspection_failure_and_source_change_prevent_publication(self):
        def wrong_kind(command, info): info["kind"] = "stock"
        self.hook = wrong_kind
        with self.assertRaises(ValueError): self.run_package()
        self.assertFalse((self.root / "build/releases").exists())
        self.hook = lambda command, info: (self.release / "LICENSE").write_bytes(b"changed while inspecting")
        with self.assertRaises(ValueError): self.run_package()
        self.assertFalse((self.root / "build/releases").exists())

    def test_private_local_paths_in_packaged_notices_are_rejected(self):
        data = str(self.root).encode()
        (self.root / "LICENSE").write_bytes(data); (self.release / "LICENSE").write_bytes(data)
        self.manifest["license_files"][0] = self.record(self.release / "LICENSE"); self.write_manifest()
        with self.assertRaises(ValueError): self.run_package()

    def test_atomic_failure_keeps_previous_archive_and_cleans_only_staging_file(self):
        result = self.run_package(); previous = self.archive_bytes()
        with mock.patch.object(package.os, "replace", side_effect=OSError("synthetic replace failure")):
            with self.assertRaises(OSError): self.run_package()
        self.assertEqual(self.archive_bytes(), previous)
        self.assertEqual((self.root / result["download"]).read_bytes(), previous)
        self.assertEqual(list((self.root / "build/releases").glob(".updater-*.tmp")), [])

    def test_resolved_outside_catalog_file_is_rejected_before_it_is_inspected(self):
        original = Path.resolve
        escaped = self.root / "outside.333"
        escaped.write_bytes(self.stock)
        def resolve(path, *args, **kwargs):
            # Equivalent resolution to a sibling symlink pointing outside the
            # catalog directory, without requiring Windows symlink privilege.
            return escaped if path == self.release / "stock.333" else original(path, *args, **kwargs)
        with mock.patch.object(Path, "resolve", resolve):
            with self.assertRaises(ValueError): self.run_package()
        self.assertFalse(any(len(command) > 2 and command[2] == str(self.release / "stock.333") for command in self.calls))
        self.assertFalse((self.root / "build/releases").exists())

    def test_manifest_duplicates_and_total_bundle_budget_fail_before_publication(self):
        self.manifest_path.write_text('{"format":"thy666.updater-build","version":1,"version":1}')
        with self.assertRaises(ValueError): self.run_package()
        self.assertEqual(self.calls, [])
        self.write_manifest()
        with mock.patch.object(package, "MAX_TOTAL", 1):
            with self.assertRaises(ValueError): self.run_package()
        self.assertFalse((self.root / "build/releases").exists())


if __name__ == "__main__":
    unittest.main()
