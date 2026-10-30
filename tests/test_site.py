"""Exercise the public artifact boundary and its deployable links, without USB."""
from __future__ import annotations

import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_site import build, site_assets
from browser_updater_assets import browser_core_assets, checked_bytes


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []
        self.titles = 0

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if tag == "title":
            self.titles += 1
        for key in ("href", "src"):
            if values.get(key):
                self.links.append(values[key])


class PublicSiteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = site_assets()

    def test_guide_remains_read_only_and_installer_is_bounded(self):
        self.assertIn("index.html", self.files)
        self.assertIn("guide/help-topics.js", self.files)
        self.assertIn("LICENSE.txt", self.files)
        self.assertIn("installer/THIRD_PARTY_LICENSES.txt", self.files)
        self.assertIn("client/backup-migration.js", self.files)
        forbidden = (".env", "chords.runtime.json", "Resources/", "analysis/", "before.thyb")
        for name in self.files:
            self.assertFalse(any(value in name for value in forbidden), name)
            self.assertNotIn(Path(name).suffix, (".333", ".bin", ".thyb", ".exe"))
            if name.endswith(".wasm"):
                self.assertIn(name, ("installer/browser-updater.wasm", "client/generated/browser-updater.wasm", "client/generated/core.wasm"))
        # The guide is exactly the Client's authored guide, not a stale copy.
        for name in ("help.js", "help.css", "help-topics.js", "studio-menu.js"):
            self.assertEqual(self.files["guide/" + name], (ROOT / "web" / name).read_bytes())
        for name, data in self.files.items():
            if name.endswith(".js") and not name.startswith(("installer/", "client/")):
                source = data.decode()
                self.assertNotRegex(source, r"navigator\.(?:hid|serial|requestMIDIAccess)")
                self.assertNotRegex(source, r"\bfetch\s*\(")

    def test_html_module_css_and_font_links_resolve_under_a_project_subpath(self):
        for name, data in self.files.items():
            urls = []
            if name.endswith(".html"):
                parser = Links()
                parser.feed(data.decode())
                self.assertEqual(parser.titles, 1, name)
                urls += parser.links
            elif name.endswith(".css"):
                urls += re.findall(r"url\(['\"]?([^)'\"]+)", data.decode())
            elif name.endswith(".js"):
                urls += re.findall(r"\bfrom\s+['\"]([^'\"]+)['\"]", data.decode())
            for url in urls:
                parsed = urlsplit(url)
                if parsed.scheme or not parsed.path:
                    continue
                self.assertFalse(parsed.path.startswith("/"), (name, url))
                parts = list(PurePosixPath(name).parent.parts)
                for part in unquote(parsed.path).split("/"):
                    if part == "..":
                        self.assertTrue(parts, (name, url))
                        parts.pop()
                    elif part not in ("", "."):
                        parts.append(part)
                resolved = "/".join(parts) + ("/index.html" if parsed.path.endswith("/") else "")
                self.assertIn(resolved, self.files.keys(), (name, url))

    def test_client_navigation_and_public_runtime(self):
        for page in ("index.html", "install.html", "manual.html"):
            source = self.files[page].decode()
            self.assertLess(source.index('>Install<'), source.index('>Client<'))
            self.assertLess(source.index('>Client<'), source.index('>User guide<'))
        self.assertIn(b'data-project-site="true"', self.files['client/index.html'])
        self.assertIn(b"client/index.html", self.files['install.js'])
        self.assertIn('client/generated/menu-schema.js', self.files)
        self.assertEqual({name for name in self.files if name.startswith('client/generated/')}, {
            'client/generated/' + name for name in ('core.wasm', 'menu-schema.js', 'oled-profile.json',
            'build-info.json', 'browser-updater.wasm', 'browser-updater.json', 'THIRD_PARTY_LICENSES.txt')})

    def test_font_licenses_and_exact_hashes_travel_with_site(self):
        manifest = json.loads(self.files["assets/fonts/manifest.json"])
        for face in manifest["fonts"]:
            self.assertIn("assets/fonts/" + face["license_path"], self.files)
            for asset in face["files"]:
                actual = self.files["assets/fonts/" + asset["path"]]
                self.assertEqual(hashlib.sha256(actual).hexdigest(), asset["sha256"])

    def test_shared_interface_and_reference_are_staged_from_the_same_sources(self):
        for name in ("typography.css", "ui-system.css", "ui-system.js",
                     "design-system.html", "design-system.js", "design-system.css"):
            with self.subTest(asset=name):
                self.assertEqual(self.files[name], (ROOT / "web" / name).read_bytes())
        for name in ("typography.css", "ui-system.css", "ui-system.js"):
            self.assertEqual(self.files[name], self.files["client/" + name])
        self.assertEqual(self.files["assets/thy666-web-logo.svg"], self.files["assets/logo.svg"])
        self.assertEqual(self.files["assets/thy666-web-logo.svg"],
                         self.files["client/assets/thy666-web-logo.svg"])

    def test_component_reference_has_no_device_or_musical_runtime_dependencies(self):
        # Traverse actual imports so a future helper cannot silently bring a
        # connection or the musical runtime into the read-only reference.
        pending = ["design-system.js"]
        visited = set()
        forbidden = {"app.js", "core.js", "device-client.js", "desktop-runtime.js",
                     "browser-hid.js", "browser-updater-client.js", "controller.js"}
        while pending:
            name = pending.pop()
            if name in visited:
                continue
            visited.add(name)
            self.assertNotIn(PurePosixPath(name).name, forbidden, name)
            source = self.files[name].decode()
            self.assertNotRegex(source, r"navigator\.(?:hid|serial|requestMIDIAccess)")
            self.assertNotRegex(source, r"\b(?:fetch|WebSocket|Worker)\s*\(")
            imports = re.findall(r"\bfrom\s+['\"]([^'\"]+)['\"]|\bimport\s*(?:\(\s*)?['\"]([^'\"]+)['\"]", source)
            for static, direct in imports:
                target = static or direct
                self.assertTrue(target.startswith("./"), (name, target))
                resolved = str(PurePosixPath(name).parent / target)
                self.assertIn(resolved, self.files, (name, target))
                pending.append(resolved)

    def test_browser_policy_cannot_be_packaged_with_tampered_bytes_or_stale_sources(self):
        original = browser_core_assets()
        wasm = original["browser-updater.wasm"]
        for problem in ("wasm", "source", "source-list"):
            def read(path, limit):
                data = checked_bytes(path, limit)
                if problem == "wasm" and path.name == "browser-updater.wasm":
                    return data[:-1] + bytes([data[-1] ^ 1])
                if problem == "source" and path.as_posix().endswith("updater/src/package.rs"):
                    return data + b"\n"
                if problem == "source-list" and path.name == "browser-updater.json":
                    value = json.loads(data)
                    value["source_sha256"] = {}
                    return json.dumps(value).encode()
                return data
            with self.subTest(problem=problem), patch("browser_updater_assets.checked_bytes", side_effect=read):
                with self.assertRaises(ValueError):
                    browser_core_assets()
        self.assertGreater(len(wasm), 8)

    def test_build_replaces_only_checked_generated_directory_and_records_bytes(self):
        (ROOT / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="site-test-", dir=ROOT / "build") as temp:
            destination = Path(temp) / "site"
            build(destination)
            (destination / "old.txt").write_text("stale")
            records = build(destination)
            self.assertFalse((destination / "old.txt").exists())
            manifest = json.loads((destination / "site-manifest.json").read_text())
            self.assertTrue(manifest["device_access"])
            self.assertFalse(manifest["automatic_device_access"])
            self.assertFalse(manifest["guide_device_access"])
            self.assertFalse(manifest["firmware_included"])
            for row in records:
                data = (destination / row["file"]).read_bytes()
                self.assertEqual(len(data), row["bytes"])
                self.assertEqual(hashlib.sha256(data).hexdigest(), row["sha256"])
        for invalid in (ROOT, ROOT / "build", ROOT / "site"):
            with self.assertRaises(ValueError):
                build(invalid)

    def test_release_page_does_not_promise_unpublished_or_qualified_paths(self):
        page = self.files["install.html"].decode()
        text = " ".join(page.split())
        for fact in ("reset settings", "not a full-device image", "remain unverified"):
            self.assertIn(fact, text)
        self.assertEqual(json.loads(self.files["releases.json"])["releases"], [])



if __name__ == "__main__":
    unittest.main()
