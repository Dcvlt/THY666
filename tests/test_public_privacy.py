"""Keep local identities/configuration out of source and browser deliverables.

These checks do not open devices or transmit data. Private values are used only
as local needles; assertion messages identify files/classes, never their values.
"""
from pathlib import Path, PureWindowsPath
import base64
from contextlib import contextmanager
import json
import re
import shutil
import subprocess
import sys
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from local_env import read_env
from serve_workbench import WorkbenchHandler

EXCLUDED = {".git", ".tools", ".local-archive", ".venv", "analysis", "build",
            "Resources", "node_modules", "__pycache__"}
HOME_PATH = re.compile(r"(?i)(?:\b[A-Z]:[\\/]+Users[\\/]+[^\\/\s\"']+|(?<!:)/(?:Users|home)/[^/\s\"']+)")
SECRET_FORMATS = {
    "private key": re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    "GitHub token": re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9]{25,}|github_pat_[A-Za-z0-9_]{25,})\b"),
    "API token": re.compile(r"\bsk-(?:proj-)?[A-Za-z0-9_-]{30,}\b"),
    "AWS access key": re.compile(r"\bAKIA[A-Z0-9]{16}\b"),
}


def public_files():
    """Inspect working files proposed for Git, including new source files."""
    try:
        result = subprocess.run(["git", "ls-files", "--cached", "--others",
                                 "--exclude-standard", "-z"], cwd=ROOT,
                                check=True, capture_output=True)
        candidates = [ROOT / name for name in result.stdout.decode().split("\0") if name]
    except (OSError, subprocess.CalledProcessError):
        # Also works from an extracted source archive without Git metadata.
        candidates = ROOT.rglob("*")
    for path in candidates:
        relative = path.relative_to(ROOT)
        if any(part in EXCLUDED for part in relative.parts):
            continue
        if relative.parts[:2] == ("web", "generated"):
            continue
        if path.name.startswith(".env") and path.name != ".env.example":
            continue
        if path.is_file():
            yield path


def sensitive_local_values():
    """Read literal values only; a dotenv file is never evaluated as code."""
    values = []
    for key, value in read_env().items():
        if len(value) < 8:
            continue
        absolute = bool(re.match(r"(?i)(?:[A-Z]:[\\/]|/)", value))
        if (re.search(r"UID|SERIAL|HUB|KEY|TOKEN|SECRET|PASSWORD", key) or HOME_PATH.search(value)
                or (absolute and re.search(r"PATH|EXECUTABLE|DIR|FILE|EXE", key))):
            values.append((key, value))
    return values


def local_path_present(data):
    variants = {str(ROOT), ROOT.as_posix()}
    return any(value.encode() in data for value in variants) or bool(
        re.search(rb"(?i)\b[A-Z]:[\\/]+Users[\\/]+[^\\/\x00\r\n]+", data))


@contextmanager
def static_handler():
    temporary_root = (ROOT / ".tools/tmp").resolve()
    folder = temporary_root / ("privacy-static-" + uuid.uuid4().hex)
    web = folder / "web"
    web.mkdir(parents=True)
    public = web / "index.html"
    public.write_text("public", encoding="utf-8")
    (web / ".env").write_text("SYNTHETIC_PRIVATE=1\n", encoding="utf-8")
    outside = folder / "private.txt"
    outside.write_text("synthetic outside-root data", encoding="utf-8")
    handler = WorkbenchHandler.__new__(WorkbenchHandler)
    handler.directory = str(web)
    try:
        yield handler, public, outside
    finally:
        if not folder.resolve().is_relative_to(temporary_root):
            raise RuntimeError("Temporary privacy fixture escaped its test directory")
        shutil.rmtree(folder)


class PublicPrivacyTests(unittest.TestCase):
    def test_local_configuration_is_ignored_and_example_is_publishable(self):
        ignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
        self.assertRegex(ignore, r"(?m)^\.?/?\.env$")
        self.assertIn("!.env.example", ignore)
        self.assertTrue((ROOT / ".env.example").is_file())
        try:
            result = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT,
                                    check=True, capture_output=True)
        except (OSError, subprocess.CalledProcessError):
            return
        private = [name for name in result.stdout.decode().split("\0")
                   if Path(name).name.startswith(".env") and Path(name).name != ".env.example"]
        self.assertFalse(private, "Private dotenv configuration must not enter the Git index")

    def test_public_text_has_no_local_identity_or_recognizable_secret(self):
        needles = sensitive_local_values()
        findings = []
        for path in public_files():
            try:
                text = path.read_text(encoding="utf-8")
            except UnicodeError:
                continue
            relative = path.relative_to(ROOT).as_posix()
            if HOME_PATH.search(text):
                findings.append((relative, "absolute user-home path"))
            for label, pattern in SECRET_FORMATS.items():
                if pattern.search(text):
                    findings.append((relative, label))
            for key, value in needles:
                if value.casefold() in text.casefold():
                    findings.append((relative, "private configuration: " + key))
        self.assertFalse(findings, "Public files contain local/private data: " + repr(findings))

    def test_browser_source_does_not_link_outside_its_public_root(self):
        web = (ROOT / "web").resolve()
        escaped = [path.relative_to(web).as_posix() for path in web.rglob("*")
                   if path.is_symlink() and not path.resolve().is_relative_to(web)]
        self.assertFalse(escaped, "Browser source must not symlink private local files")

    def test_static_server_rejects_private_names_and_encoded_traversal(self):
        with static_handler() as (handler, public, _):
            self.assertTrue(handler.public_path("/index.html") == public.resolve())
            self.assertTrue(handler.public_path("/index.html?view=help") == public.resolve())
            denied = ("/.env", "/%2eenv", "/%2e%65nv", "/../private.txt",
                      "/%2e%2e/private.txt", "/..%5cprivate.txt", "/%2e%2e%5cprivate.txt")
            for request in denied:
                self.assertTrue(handler.public_path(request) is None,
                                "Static server admitted a private or traversal path")

    def test_static_server_blocks_resolved_outside_root_paths(self):
        with static_handler() as (handler, _, outside):
            # A symlink pointing outside web resolves to this same path. Mock
            # translation so this boundary test needs no symlink privileges.
            handler.translate_path = lambda _: str(outside)
            self.assertTrue(handler.public_path("/linked-secret") is None,
                            "Resolved static targets must remain inside web")

    def test_static_server_denies_private_requests_and_directory_listing(self):
        with static_handler() as (handler, public, _):
            errors = []
            handler.send_error = lambda status: errors.append(status)
            handler.path = "/.env"
            self.assertTrue(handler.send_head() is None)
            self.assertTrue(handler.list_directory(str(public.parent)) is None)
            self.assertEqual(errors, [404, 404])

    @unittest.skipUnless((ROOT / "web/generated/core.wasm").exists(), "Build WebAssembly first")
    def test_generated_webassembly_does_not_embed_local_paths(self):
        self.assertFalse(local_path_present((ROOT / "web/generated/core.wasm").read_bytes()),
                         "WebAssembly debug/source paths must be remapped before publication")

    @unittest.skipUnless((ROOT / "build/workbench/THY666-workbench.html").exists(),
                         "Build the standalone browser bundle first")
    def test_standalone_bundle_does_not_hide_local_paths_in_embedded_wasm(self):
        html = (ROOT / "build/workbench/THY666-workbench.html").read_text(encoding="utf-8")
        match = re.search(r'<script id="thy-core-bytes"[^>]*>([^<]*)</script>', html)
        self.assertIsNotNone(match, "Standalone bundle must identify its embedded WebAssembly")
        self.assertFalse(local_path_present(base64.b64decode(match.group(1))),
                         "Embedded WebAssembly must not publish local compiler paths")
        manifest = json.loads((ROOT / "build/workbench/manifest.json").read_text(encoding="utf-8"))
        self.assertFalse(Path(manifest["output"]).is_absolute() or PureWindowsPath(manifest["output"]).is_absolute(),
                         "Standalone manifest output must be repository-relative")


if __name__ == "__main__":
    unittest.main()
