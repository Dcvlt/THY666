import base64
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUNDLE = ROOT / "build/workbench/THY666-workbench.html"


class Scripts(HTMLParser):
    def __init__(self):
        super().__init__()
        self.scripts = {}
        self.external = []
        self.current = None
        self.modules = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "script":
            if "src" in attrs:
                self.external.append(attrs["src"])
            self.current = attrs.get("id", "module" if attrs.get("type") == "module" else "other")
            self.scripts.setdefault(self.current, "")
        if tag == "link" and attrs.get("rel") == "stylesheet":
            self.external.append(attrs.get("href"))

    def handle_endtag(self, tag):
        if tag == "script":
            self.current = None

    def handle_data(self, data):
        if self.current:
            self.scripts[self.current] += data


@unittest.skipUnless(BUNDLE.exists(), "Build the local standalone preview first")
class WorkbenchBundleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parsed = Scripts()
        cls.parsed.feed(BUNDLE.read_text(encoding="utf-8"))
        cls.manifest = json.loads((BUNDLE.parent / "manifest.json").read_text(encoding="utf-8"))

    def test_standalone_contains_exact_wasm_and_both_data_profiles(self):
        self.assertEqual(self.parsed.external, [])
        self.assertEqual(base64.b64decode(self.parsed.scripts["thy-core-bytes"]),
                         (ROOT / "web/generated/core.wasm").read_bytes())
        for embedded, filename in (("thy-runtime-data", "chords.runtime.json"),
                                    ("thy-oled-profile", "oled-profile.json")):
            self.assertEqual(json.loads(self.parsed.scripts[embedded]),
                             json.loads((ROOT / "web/generated" / filename).read_text(encoding="utf-8")))
        module = self.parsed.scripts["module"]
        self.assertNotIn("import {", module)
        self.assertIn("class OLEDTransferModel", module)
        self.assertIn("async function createCore", module)
        self.assertIn("const initializeLibraryLab = (() =>", module)
        self.assertIn('id="library-lab"', BUNDLE.read_text(encoding="utf-8"))
        self.assertIn("library-lab.js", self.manifest["assets_sha256"])
        for name in ("companion-shell.js", "companion-display.js", "companion-display.css", "device-client.js", "backup-panel.js", "updater-panel.js", "help-topics.js", "app-settings.js", "companion-theme.css", "cockpit.js", "cockpit.css", "typography.css", "assets/fonts/manifest.json"):
            self.assertIn(name, self.manifest["assets_sha256"])
        self.assertIn("function initializeCompanionShell", module)
        self.assertIn("function initializeAppSettings", module)
        self.assertIn("function inspectDeviceBackup", module)
        self.assertIn("function createBackupPayloadMigrator", module)
        self.assertIn("backup-migration.js", self.manifest["assets_sha256"])
        self.assertIn("function initializeUpdaterPanel", module)
        self.assertIn("function createUpdaterClient", module)
        self.assertIn("THY666-UPDATER/1", module)
        logo = base64.b64encode((ROOT / "web/assets/thy666-web-logo.svg").read_bytes()).decode("ascii")
        self.assertIn('src="data:image/svg+xml;base64,' + logo + '"', BUNDLE.read_text(encoding="utf-8"))
        font_manifest = json.loads((ROOT / "web/assets/fonts/manifest.json").read_text(encoding="utf-8"))
        notices = json.loads(self.parsed.scripts["thy-font-notices"])
        html = BUNDLE.read_text(encoding="utf-8")
        for face in font_manifest["fonts"]:
            relative = "assets/fonts/" + face["font_path"]
            raw = (ROOT / "web" / relative).read_bytes()
            self.assertIn("data:font/ttf;base64," + base64.b64encode(raw).decode("ascii"), html)
            self.assertNotIn("url('" + relative, html)
            self.assertEqual(notices[face["family"]], (ROOT / "web/assets/fonts" / face["license_path"]).read_text(encoding="utf-8"))
        node = shutil.which("node")
        self.assertIsNotNone(node, "Node.js is required to validate the standalone module")
        temporary_root = ROOT / ".tools" / "tmp"
        temporary_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="thy666-bundle-", dir=temporary_root) as temporary:
            source = Path(temporary) / "workbench.mjs"
            source.write_text(module, encoding="utf-8")
            checked = subprocess.run([node, "--check", str(source)], capture_output=True, text=True)
        self.assertEqual(checked.returncode, 0, checked.stderr[-1500:])

    def test_bundle_and_inputs_match_recorded_identities(self):
        self.assertEqual(hashlib.sha256(BUNDLE.read_bytes()).hexdigest(), self.manifest["sha256"])
        for relative, expected in self.manifest["assets_sha256"].items():
            self.assertEqual(hashlib.sha256((ROOT / "web" / relative).read_bytes()).hexdigest(), expected, relative)
        runtime = json.loads((ROOT / "web/generated/chords.runtime.json").read_text(encoding="utf-8"))
        self.assertEqual(self.manifest["source_sha256"], runtime["provenance"]["source"]["sha256"])


if __name__ == "__main__":
    unittest.main()
