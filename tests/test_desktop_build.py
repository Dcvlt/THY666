"""Synthetic desktop staging/packaging checks. No native app or USB is opened."""
from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import prepare_desktop as stage
import build_desktop as build


class DesktopBuildTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="desktop-build-test-", dir=ROOT / "build")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.web = self.root / "web"
        for folder in ("generated/updater", "assets/fonts/face"):
            (self.web / folder).mkdir(parents=True)
        files = {"index.html": b"<html>synthetic</html>", "app.js": b"// synthetic", "styles.css": b"body{}",
                 "generated/core.wasm": b"synthetic wasm", "generated/chords.runtime.json": b"{}",
                 "generated/menu-schema.js": b"// schema", "generated/oled-profile.json": b"{}",
                 "assets/brand.svg": b"<svg/>", "assets/fonts/face/font.ttf": b"synthetic font",
                 "assets/fonts/face/OFL.txt": b"Synthetic font license"}
        files["generated/build-info.json"] = json.dumps({"artifacts": {"webassembly": {"sha256": stage.digest(files["generated/core.wasm"])}}, "source_sha256": {}}).encode()
        files["assets/fonts/manifest.json"] = json.dumps({"fonts": [{"family": "Synthetic face", "files": [{"path": "face/font.ttf", "sha256": stage.digest(files["assets/fonts/face/font.ttf"])}], "license_path": "face/OFL.txt"}]}).encode()
        for name, data in files.items():
            (self.web / name).write_bytes(data)
        for name in ("generated/chords.runtime.json", "generated/updater/private-download.zip", "generated/old-preview.html", "private.log", ".env"):
            (self.web / name).write_text("must not be included")
        for module, name, value in [(stage, "ROOT", self.root), (stage, "WEB", self.web),
                                    (build, "ROOT", self.root), (build, "TARGET", self.root / "build/desktop-target"),
                                    (build, "CARGO_CACHE", self.root / ".tools/cargo")]:
            patcher = mock.patch.object(module, name, value)
            patcher.start(); self.addCleanup(patcher.stop)

    def test_staging_is_curated_and_does_not_include_helper_zip_old_previews_or_dotenv(self):
        files = stage.web_assets()
        self.assertIn("generated/core.wasm", files)
        self.assertIn("assets/fonts/face/OFL.txt", files)
        self.assertNotIn("generated/updater/private-download.zip", files)
        self.assertNotIn("generated/old-preview.html", files)
        self.assertNotIn("private.log", files)
        self.assertNotIn("generated/chords.runtime.json", files)
        self.assertNotIn(".env", files)

    def test_macos_development_bundle_contains_exact_executable_and_scoped_resources(self):
        import plistlib
        (self.root / "desktop").mkdir()
        (self.root / "desktop/Cargo.toml").write_text('[package]\nversion = "0.1.0"\n')
        (self.root / "LICENSE").write_text("Synthetic license")
        assets = stage.prepare(with_releases=False)
        executable = self.root / "build/release/thy666-desktop"
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b"synthetic executable")
        app = build.macos_development_bundle(executable, assets, b"synthetic notices")
        self.assertEqual((app / "Contents/MacOS/thy666-desktop").read_bytes(), executable.read_bytes())
        self.assertTrue((app / "Contents/MacOS/thy666-desktop").stat().st_mode & 0o100)
        info = plistlib.loads((app / "Contents/Info.plist").read_bytes())
        self.assertEqual(info["CFBundleExecutable"], "thy666-desktop")
        self.assertTrue((app / "Contents/Resources/resources/README.txt").is_file())
        self.assertFalse((app / "Contents/Resources/resources/releases.json").exists())
        with self.assertRaisesRegex(ValueError, "compile-check"):
            build.macos_development_bundle(executable, {**assets, "compile_check_only": True}, b"")

    def test_core_and_font_identities_are_checked_before_staging(self):
        (self.web / "generated/core.wasm").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "WebAssembly"):
            stage.web_assets()
        (self.web / "generated/core.wasm").write_bytes(b"synthetic wasm")
        (self.web / "assets/fonts/face/font.ttf").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "font"):
            stage.web_assets()

    def test_compile_check_is_labelled_and_needs_no_private_musical_archive(self):
        with mock.patch.object(stage, "catalog_files", side_effect=AssertionError("A compile check must not inspect releases")):
            result = stage.prepare(compile_check=True)
        self.assertTrue(result["compile_check_only"])
        self.assertEqual([row["file"] for row in result["web"]], ["index.html"])
        self.assertIn(b"not the Client release", (self.root / "build/desktop-web/index.html").read_bytes())
        self.assertFalse(result["hardware_access_performed"])

    def test_stage_replacement_discards_stale_output_and_rejects_escaping_targets(self):
        destination = self.root / "build/desktop-web"
        destination.mkdir(parents=True)
        (destination / "stale.txt").write_text("stale")
        records = stage.replace_stage(destination, {"folder/current.txt": b"current"})
        self.assertEqual([row["file"] for row in records], ["folder/current.txt"])
        self.assertFalse((destination / "stale.txt").exists())
        for target in (self.root, self.root / "build", self.root / "outside"):
            with self.assertRaises(ValueError):
                stage.replace_stage(target, {})
        for name in ("../outside.txt", "a\\b.txt", "/outside.txt", "C:outside"):
            with self.assertRaises(ValueError):
                stage.replace_stage(destination, {name: b"bad"})
        self.assertEqual((destination / "folder/current.txt").read_bytes(), b"current")

    def test_bounded_sources_reject_outside_files_and_oversized_inputs(self):
        outside = self.root / "outside.txt"
        outside.write_bytes(b"outside")
        with self.assertRaises(ValueError):
            stage.bounded_file(outside, parent=self.web)
        with self.assertRaises(ValueError):
            stage.bounded_file(self.web / "index.html", parent=self.web, limit=2)

    def test_build_environment_uses_local_paths_and_preserves_flags_without_forcing_crt(self):
        with mock.patch.dict(os.environ, {"RUSTFLAGS": "-C opt-level=2", "USERPROFILE": str(self.root), "HOME": str(self.root)}, clear=True):
            env = build.build_environment("x86_64-pc-windows-msvc")
        self.assertEqual(env["CARGO_TARGET_DIR"], str(self.root / "build/desktop-target"))
        self.assertEqual(env["CARGO_INCREMENTAL"], "0")
        self.assertIn("-C\x1fopt-level=2", env["CARGO_ENCODED_RUSTFLAGS"])
        self.assertIn("--remap-path-prefix=", env["CARGO_ENCODED_RUSTFLAGS"])
        self.assertNotIn("crt-static", env["CARGO_ENCODED_RUSTFLAGS"])
        with mock.patch.dict(os.environ, {"CARGO_BUILD_TARGET": "different-target"}):
            with self.assertRaises(ValueError):
                build.build_environment("x86_64-pc-windows-msvc")

    def test_notices_use_exact_locked_package_and_license_content_checksums(self):
        cache = self.root / ".tools/cargo"
        crate = cache / "registry/src/synthetic/example-1.2.3"
        crate.mkdir(parents=True)
        lock = self.root / "Cargo.lock"
        lock.write_text('version = 4\n[[package]]\nname = "example"\nversion = "1.2.3"\nsource = "registry+https://github.com/rust-lang/crates.io-index"\nchecksum = "abc"\n')
        (crate / "Cargo.toml").write_text('[package]\nname = "example"\nversion = "1.2.3"\nlicense = "MIT"\n')
        license_bytes = b"Synthetic original license text"
        (crate / "LICENSE").write_bytes(license_bytes)
        (crate / ".cargo-checksum.json").write_text(json.dumps({"files": {"LICENSE": stage.digest(license_bytes)}}))
        notices = build.dependency_notices(lock, cache)
        self.assertIn(license_bytes, notices)
        self.assertIn(b"Package: example 1.2.3", notices)
        self.assertIn(stage.digest(lock.read_bytes()).encode(), notices)
        (crate / "LICENSE").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "checksum"):
            build.dependency_notices(lock, cache)

    def test_compile_only_assets_cannot_be_packaged_as_a_release(self):
        with self.assertRaisesRegex(ValueError, "compile-check"):
            build.package_desktop(self.root / "unused.exe", "x86_64-pc-windows-msvc", {"compile_check_only": True}, b"unused")

    def test_zip_is_deterministic_contains_notices_and_preserves_previous_release_on_stale_asset(self):
        assets = stage.prepare(with_releases=False)
        (self.root / "desktop").mkdir()
        (self.root / "desktop/Cargo.toml").write_text('[package]\nversion = "0.1.0"\n')
        (self.root / "LICENSE").write_bytes(b"Synthetic project license")
        executable = self.root / "build/desktop-target/release/thy666-desktop.exe"
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b"Synthetic executable, never launched")
        checked = {"bytes": executable.stat().st_size, "sha256": stage.digest(executable.read_bytes()), "windows_runtime_imports": []}
        with mock.patch.object(build, "verify_artifact", return_value=checked):
            first = build.package_desktop(executable, "x86_64-pc-windows-msvc", assets, b"Synthetic dependency notices")
            previous = (self.root / first["file"]).read_bytes()
            second = build.package_desktop(executable, "x86_64-pc-windows-msvc", assets, b"Synthetic dependency notices")
            self.assertEqual(first["sha256"], second["sha256"])
            with zipfile.ZipFile(self.root / first["file"]) as archive:
                self.assertEqual(archive.read("FONT_LICENSES.txt").count(b"Synthetic font license"), 1)
                self.assertEqual(archive.read("thy666-desktop.exe"), executable.read_bytes())
                self.assertNotIn("private.log", archive.namelist())
                self.assertNotIn("generated/updater/private-download.zip", archive.namelist())
            (self.root / "build/desktop-web/app.js").write_bytes(b"changed after build")
            with self.assertRaisesRegex(ValueError, "changed after compilation"):
                build.package_desktop(executable, "x86_64-pc-windows-msvc", assets, b"Synthetic dependency notices")
            self.assertEqual((self.root / first["file"]).read_bytes(), previous)


if __name__ == "__main__":
    unittest.main()
