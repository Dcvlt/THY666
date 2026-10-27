"""Build the bounded public site, guide and explicit browser installer.

No firmware, private musical corpus, device state, environment or network is used.
The Client reuses the verified public asset boundary and generated allowlist.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from prepare_desktop import bounded_file, replace_stage, web_assets
from browser_updater_assets import browser_core_assets

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"


def site_assets() -> dict[str, bytes]:
    files: dict[str, bytes] = {}

    def add(source: Path, destination: str, *, parent: Path = ROOT) -> None:
        if destination in files:
            raise ValueError("Duplicate public site path")
        files[destination] = bounded_file(source, parent=parent)

    for name in ("index.html", "manual.html", "install.html", "site.css", "site.js", "screen-carousel.js", "guide.js", "install.js", "releases.json"):
        add(SITE / name, name, parent=SITE)
    for name in ("help.js", "help.css", "help-topics.js", "studio-menu.js"):
        add(ROOT / "web" / name, "guide/" + name, parent=ROOT / "web")
    for name in ("browser-updater-client.js", "browser-hid.js", "device-client.js",
                 "updater-panel.js", "updater-layout.js", "updater-client.js",
                 "updater-checks.js", "updater-art.js", "updater-wizard.css"):
        add(ROOT / "web" / name, "installer/" + name, parent=ROOT / "web")
    for name, data in browser_core_assets().items():
        files["installer/" + name] = data
    for source, name in (("thy666-web-logo.svg", "logo.svg"),):
        add(ROOT / "web/assets" / source, "assets/" + name, parent=ROOT / "web/assets")
    add(ROOT / "web/assets/thy333-logo.svg", "installer/assets/thy333-logo.svg", parent=ROOT / "web/assets")
    add(ROOT / "docs/assets/thy666-icon.svg", "assets/icon.svg", parent=ROOT / "docs/assets")
    add(SITE / "assets/client-preview.png", "assets/client-preview.png", parent=SITE)
    add(ROOT / "LICENSE", "LICENSE.txt")
    fonts = ROOT / "web/assets/fonts"
    manifest = json.loads(bounded_file(fonts / "manifest.json", parent=fonts))
    add(fonts / "manifest.json", "assets/fonts/manifest.json", parent=fonts)
    for face in manifest["fonts"]:
        for asset in face["files"]:
            path = asset["path"]
            data = bounded_file(fonts / path, parent=fonts)
            if hashlib.sha256(data).hexdigest() != asset["sha256"]:
                raise ValueError("A public font differs from its license manifest")
            if "assets/fonts/" + path not in files:
                files["assets/fonts/" + path] = data
    # Reuse the verified public Client asset boundary, never generated/ wholesale.
    for name, data in web_assets().items():
        if name == "index.html":
            data = data.replace(b'<html lang="en"', b'<html data-project-site="true" lang="en"', 1)
        files["client/" + name] = data
    files[".nojekyll"] = b""
    return files


def build(destination: Path | None = None) -> list[dict]:
    destination = destination or ROOT / "build/site"
    files = site_assets()
    records = [{"file": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
               for name, data in sorted(files.items())]
    files["site-manifest.json"] = (json.dumps({
        "format": "thy666.public-site", "version": 1,
        "device_access": True, "automatic_device_access": False,
        "guide_device_access": False, "firmware_included": False, "files": records,
    }, indent=2) + "\n").encode()
    return replace_stage(destination, files)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/site")
    args = parser.parse_args()
    records = build(args.output)
    print(f"Built public site: {len(records)} files, {sum(item['bytes'] for item in records):,} bytes")
    print("Browser Client, read-only guide and explicit browser installer; no firmware, device bindings or private corpus included.")


if __name__ == "__main__":
    main()
