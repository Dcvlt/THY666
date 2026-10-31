"""Create a local single-file preview with the actual WASM engine and exact data."""
import base64
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from browser_updater_assets import browser_core_assets

ROOT = Path(__file__).resolve().parents[1]


def inline_named_module(raw, relative, allowed_imports=()):
    """Isolate private helpers while exposing explicit named module exports."""
    source = raw.decode("utf-8")
    import_pattern = r"^import\s+\{[^}]+\}\s+from\s+['\"]([^'\"]+)['\"];?[ \t]*\r?\n"
    imports = re.findall(import_pattern, source, re.M)
    if any(value not in allowed_imports for value in imports):
        raise ValueError(f"Unexpected dependency in {relative}: {imports}")
    source = re.sub(import_pattern, "", source, flags=re.M)
    names = re.findall(r"^export\s+(?:async\s+)?(?:function|class|const|let|var)\s+(\w+)", source, re.M)
    # Re-exports already exist in the enclosing bundle scope. Do not redeclare
    # them alongside a module's own public functions.
    source = re.sub(r"^export\s+\{[\w\s,]+\};?[ \t]*\r?\n", "", source, flags=re.M)
    if not names or len(names) != len(set(names)):
        raise ValueError(f"Expected unique named exports in {relative}")
    source = re.sub(r"^export\s+", "", source, flags=re.M)
    if re.search(r"^\s*(?:import\s|export\s)", source, re.M):
        raise ValueError(f"Unsupported module declaration in {relative}")
    names = ", ".join(names)
    return "const { " + names + " } = (() => {\n" + source + "\nreturn { " + names + " };\n})();"


def browser_updater_adapter(files):
    """Use the shared client with two exact in-memory assets, never a network fallback.

    This does not create a client, request a device or instantiate WebAssembly.
    The caller supplies verified bytes from browser_core_assets().
    """
    encoded = {name: base64.b64encode(files[name]).decode("ascii")
               for name in ("browser-updater.wasm", "browser-updater.json")}
    return """const createBundledBrowserUpdaterClient = options => {
  if (!['http:', 'https:'].includes(location.protocol))
    throw new Error('Direct browser installation needs a served secure page. Use the local Rust helper for this file.');
  const base = new URL('./__thy666_embedded_updater__/', location.href);
  const encoded = """ + json.dumps(encoded) + """;
  const resources = new Map();
  for (const [name, value] of Object.entries(encoded))
    resources.set(new URL(name, base).href, Uint8Array.from(atob(value), char => char.charCodeAt(0)));
  const embeddedFetch = async (input, settings = {}) => {
    const url = new URL(String(input), location.href);
    if ((settings.method || 'GET') !== 'GET' || settings.body != null ||
        url.origin !== base.origin || url.username || url.password || url.search || url.hash || !resources.has(url.href))
      throw new Error('This standalone updater can read only its two embedded core assets.');
    if (settings.signal?.aborted) throw new DOMException('The updater read was aborted.', 'AbortError');
    const bytes = resources.get(url.href);
    return new Response(bytes.slice(), {status:200, headers:{'Content-Type':url.pathname.endsWith('.wasm')?'application/wasm':'application/json',
      'Content-Length':String(bytes.length), 'Cache-Control':'no-store'}});
  };
  return createBrowserUpdaterClient({...options, baseURL:location.href, fetch:embeddedFetch, releasesURL:null,
    wasmURL:new URL('browser-updater.wasm',base), coreManifestURL:new URL('browser-updater.json',base)});
};"""


def package():
    subprocess.run([sys.executable, str(ROOT / "tools/build_studio_schema.py"), "--check"], cwd=ROOT, check=True)
    web = ROOT / "web"
    paths = [web / name for name in ("index.html", "styles.css", "companion-shell.css", "help.css", "companion-theme.css", "assets/thy666-web-logo.svg", "core.js", "oled.js", "controller.js", "firmware.js", "studio-menu.js", "studio-model.js", "engine-studio.js", "preview-settings.js", "rhythm-lab.js", "arp-lab.js", "mpe-lab.js", "library-lab.js", "harmony-guide.js", "sequencer-midi.js", "device-song.js", "sequencer-lab.js", "device-client.js", "companion-protocol.js", "help-topics.js", "help.js", "app-settings.js", "backup-panel.js", "companion-shell.js", "app.js",
                                    "generated/core.wasm", "generated/chords.runtime.json", "generated/oled-profile.json",
                                    "generated/build-info.json", "generated/menu-schema.js")]
    paths += [web / "cockpit.js", web / "cockpit.css", web / "typography.css",
              web / "companion-display.js", web / "companion-display.css"]
    paths += [web / "ui-system.js", web / "ui-system.css"]
    paths += [web / "profile-panel.js", web / "profile-panel.css"]
    paths += [web / "startup.js", web / "startup.css", web / "updater-panel.js", web / "updater-client.js", web / "updater-art.js", web / "updater-checks.js", web / "updater-layout.js", web / "updater-wizard.css", web / "desktop-runtime.js", web / "desktop-chrome.js", web / "desktop-chrome.css"]
    paths += [web / "browser-hid.js", web / "browser-updater-client.js"]
    paths += [web / "backup-migration.js"]
    paths += sorted(path for path in (web / "assets/fonts").rglob("*") if path.is_file())
    assets = {str(path.relative_to(web)).replace('\\', '/'): path.read_bytes() for path in paths}
    browser_assets = browser_core_assets()
    assets.update({"generated/" + name: browser_assets[name]
                   for name in ("browser-updater.wasm", "browser-updater.json")})
    data = assets["generated/chords.runtime.json"].decode("utf-8")
    runtime = json.loads(data)
    build = json.loads(assets["generated/build-info.json"])
    wasm_sha = hashlib.sha256(assets["generated/core.wasm"]).hexdigest()
    if build["artifacts"]["webassembly"]["sha256"] != wasm_sha:
        raise ValueError("WebAssembly differs from the verified build")
    for relative, expected in build["source_sha256"].items():
        if hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() != expected:
            raise ValueError(f"Core source changed since build: {relative}")
    font_manifest = json.loads(assets["assets/fonts/manifest.json"])
    font_notices = {}
    for face in font_manifest["fonts"]:
        for item in face["files"]:
            relative = "assets/fonts/" + item["path"]
            if hashlib.sha256(assets[relative]).hexdigest() != item["sha256"]:
                raise ValueError(f"Font asset differs from its recorded upstream identity: {relative}")
        font_notices[face["family"]] = assets["assets/fonts/" + face["license_path"]].decode("utf-8")
    html = assets["index.html"].decode("utf-8")
    style_tag = '<link rel="stylesheet" href="./styles.css">'
    script_tag = '<script type="module" src="./app.js"></script>'
    if html.count(style_tag) != 1 or html.count(script_tag) != 1:
        raise ValueError("Unexpected entry HTML; stylesheet/module tags must be explicit")
    core = assets["core.js"].decode("utf-8").replace("export async function createCore", "async function createCore", 1)
    oled = re.sub(r"^export\s+", "", assets["oled.js"].decode("utf-8"), flags=re.M)
    app = assets["app.js"].decode("utf-8")
    app = app.replace("import { SCALE_CATALOG } from './generated/menu-schema.js';", "", 1)
    startup_import = "import { initializeStartup } from './startup.js';"
    if app.count(startup_import) != 1:
        raise ValueError("Unexpected startup presentation import")
    app = app.replace(startup_import, "", 1)
    interface_import = "import { initializeUIComponents } from './ui-system.js';"
    if app.count(interface_import) != 1:
        raise ValueError("Unexpected shared interface import")
    app = app.replace(interface_import, "", 1)
    import_line = "import { createCore } from './core.js';"
    if app.count(import_line) != 1:
        raise ValueError("Unexpected core import in browser adapter")
    app = app.replace(import_line, "", 1)
    oled_import = "import { OLEDPreview } from './oled.js';"
    if app.count(oled_import) != 1:
        raise ValueError("Unexpected OLED module import in browser adapter")
    app = app.replace(oled_import, "", 1)
    controller_import = "import { ROOT_KEYS, MODE_KEYS } from './controller.js';"
    if app.count(controller_import) != 1:
        raise ValueError("Unexpected controller module import in browser adapter")
    app = app.replace(controller_import, "", 1)
    firmware_import = "import { initializeFirmwareStudio } from './firmware.js';"
    if app.count(firmware_import) != 1:
        raise ValueError("Unexpected firmware studio import")
    app = app.replace(firmware_import, "", 1)
    engine_import = "import { initializeEngineStudio } from './engine-studio.js';"
    if app.count(engine_import) != 1:
        raise ValueError("Unexpected engine studio import")
    app = app.replace(engine_import, "", 1)
    companion_import = "import { initializeCompanionShell } from './companion-shell.js';"
    if app.count(companion_import) != 1:
        raise ValueError("Unexpected companion import")
    app = app.replace(companion_import, "", 1)
    desktop_import = "import { connectDesktopRuntime } from './desktop-runtime.js';"
    if app.count(desktop_import) != 1:
        raise ValueError("Unexpected desktop runtime import")
    app = app.replace(desktop_import, "", 1)
    chrome_import = "import { initializeDesktopChrome } from './desktop-chrome.js';"
    if app.count(chrome_import) != 1:
        raise ValueError("Unexpected desktop chrome import")
    app = app.replace(chrome_import, "", 1)
    settings_import = "import { PreviewSettings } from './preview-settings.js';"
    if app.count(settings_import) != 1:
        raise ValueError("Unexpected preview settings import")
    app = app.replace(settings_import, "", 1)
    rhythm_import = "import { initializeRhythmLab } from './rhythm-lab.js';"
    if app.count(rhythm_import) != 1:
        raise ValueError("Unexpected rhythm foundation import")
    app = app.replace(rhythm_import, "", 1)
    rhythm = re.sub(r"^export\s+", "", assets["rhythm-lab.js"].decode("utf-8"), flags=re.M)
    rhythm = "const initializeRhythmLab = (() => {\n" + rhythm + "\nreturn initializeRhythmLab;\n})();"
    arp_import = "import { initializeArpLab } from './arp-lab.js';"
    if app.count(arp_import) != 1:
        raise ValueError("Unexpected Arp preview import")
    app = app.replace(arp_import, "", 1)
    arp = re.sub(r"^export\s+", "", assets["arp-lab.js"].decode("utf-8"), flags=re.M)
    arp = "const initializeArpLab = (() => {\n" + arp + "\nreturn initializeArpLab;\n})();"
    mpe_import = "import { initializeMpeLab } from './mpe-lab.js';"
    if app.count(mpe_import) != 1:
        raise ValueError("Unexpected MPE preview import")
    app = app.replace(mpe_import, "", 1)
    mpe = re.sub(r"^export\s+", "", assets["mpe-lab.js"].decode("utf-8"), flags=re.M)
    mpe = "const initializeMpeLab = (() => {\n" + mpe + "\nreturn initializeMpeLab;\n})();"
    library_import = "import { initializeLibraryLab } from './library-lab.js';"
    if app.count(library_import) != 1:
        raise ValueError("Unexpected library preview import")
    app = app.replace(library_import, "", 1)
    library = re.sub(r"^export\s+", "", assets["library-lab.js"].decode("utf-8"), flags=re.M)
    library = "const initializeLibraryLab = (() => {\n" + library + "\nreturn initializeLibraryLab;\n})();"
    sequencer_import = "import { initializeSequencerLab } from './sequencer-lab.js';"
    if app.count(sequencer_import) != 1:
        raise ValueError("Unexpected Sequencer preview import")
    app = app.replace(sequencer_import, "", 1)
    sequencer_midi = inline_named_module(assets["sequencer-midi.js"], "sequencer-midi.js")
    device_song = inline_named_module(assets["device-song.js"], "device-song.js", ("./sequencer-midi.js",))
    sequencer = inline_named_module(assets["sequencer-lab.js"], "sequencer-lab.js",
        ("./sequencer-midi.js", "./device-song.js"))
    settings = re.sub(r"^export\s+", "", assets["preview-settings.js"].decode("utf-8"), flags=re.M)
    menu = inline_named_module(assets["studio-menu.js"], "studio-menu.js")
    model = assets["studio-model.js"].decode("utf-8").replace("import { STUDIO_MENU } from './studio-menu.js';", "", 1)
    model = model.replace("import { MENU_SCHEMA_SHA256 } from './generated/menu-schema.js';", "", 1)
    schema = re.sub(r"^export\s+", "", assets["generated/menu-schema.js"].decode("utf-8"), flags=re.M)
    model = re.sub(r"^export\s+", "", model, flags=re.M)
    model = "const StudioModel = (() => {\n" + model + "\nreturn StudioModel;\n})();"
    engine_studio = assets["engine-studio.js"].decode("utf-8")
    engine_studio = engine_studio.replace("import { StudioModel } from './studio-model.js';", "", 1).replace("import { OLEDFramebuffer } from './oled.js';", "", 1)
    icon_import = "import { createUIIcon } from './ui-system.js';"
    if engine_studio.count(icon_import) != 1:
        raise ValueError("Unexpected parameter editor icon import")
    engine_studio = engine_studio.replace(icon_import, "", 1)
    engine_studio = re.sub(r"^export\s+", "", engine_studio, flags=re.M)
    engine_studio = "const initializeEngineStudio = (() => {\n" + engine_studio + "\nreturn initializeEngineStudio;\n})();"
    firmware = inline_named_module(assets["firmware.js"], "firmware.js", ("./studio-menu.js",))
    controller = re.sub(r"^export\s+", "", assets["controller.js"].decode("utf-8"), flags=re.M)
    device_client = inline_named_module(assets["device-client.js"], "device-client.js")
    backup_migration = inline_named_module(assets["backup-migration.js"], "backup-migration.js", ("./device-client.js",))
    companion_protocol = inline_named_module(assets["companion-protocol.js"], "companion-protocol.js")
    help_topics = inline_named_module(assets["help-topics.js"], "help-topics.js", ("./studio-menu.js",))
    help_ui = inline_named_module(assets["help.js"], "help.js", ("./help-topics.js",))
    ui_system = inline_named_module(assets["ui-system.js"], "ui-system.js")
    app_settings = inline_named_module(assets["app-settings.js"], "app-settings.js", ("./ui-system.js",))
    backup_panel = inline_named_module(assets["backup-panel.js"], "backup-panel.js", ("./device-client.js", "./ui-system.js", "./updater-art.js"))
    updater_client = inline_named_module(assets["updater-client.js"], "updater-client.js")
    browser_hid = inline_named_module(assets["browser-hid.js"], "browser-hid.js")
    browser_updater = inline_named_module(assets["browser-updater-client.js"], "browser-updater-client.js",
        ("./browser-hid.js", "./updater-client.js"))
    browser_adapter = browser_updater_adapter(browser_assets)
    desktop_runtime = inline_named_module(assets["desktop-runtime.js"], "desktop-runtime.js", ("./device-client.js", "./updater-client.js"))
    desktop_chrome = inline_named_module(assets["desktop-chrome.js"], "desktop-chrome.js")
    updater_checks = inline_named_module(assets["updater-checks.js"], "updater-checks.js", ("./device-client.js",))
    updater_art = inline_named_module(assets["updater-art.js"], "updater-art.js")
    updater_layout = inline_named_module(assets["updater-layout.js"], "updater-layout.js", ("./updater-art.js", "./updater-checks.js"))
    updater_panel = inline_named_module(assets["updater-panel.js"], "updater-panel.js", ("./device-client.js", "./updater-client.js", "./updater-layout.js", "./updater-checks.js", "./updater-art.js"))
    cockpit = inline_named_module(assets["cockpit.js"], "cockpit.js", ("./companion-protocol.js",))
    companion_display = inline_named_module(assets["companion-display.js"], "companion-display.js")
    startup = inline_named_module(assets["startup.js"], "startup.js")
    harmony_guide = inline_named_module(assets["harmony-guide.js"], "harmony-guide.js")
    profile_panel = inline_named_module(assets["profile-panel.js"], "profile-panel.js")
    companion = inline_named_module(assets["companion-shell.js"], "companion-shell.js",
        ("./studio-menu.js", "./device-client.js", "./core.js", "./backup-migration.js", "./companion-protocol.js", "./companion-display.js", "./help.js", "./backup-panel.js", "./updater-panel.js", "./browser-updater-client.js", "./app-settings.js", "./cockpit.js", "./harmony-guide.js", "./ui-system.js", "./profile-panel.js"))
    factory = "createBrowserUpdaterClient({"
    browser_guard = "&&globalThis.isSecureContext===true;"
    if companion.count(factory) != 1 or companion.count(browser_guard) != 1:
        raise ValueError("Unexpected direct-browser Client integration")
    companion = companion.replace(factory, "createBundledBrowserUpdaterClient({", 1)
    # Chromium treats some local files as secure contexts, but they have no
    # same-origin WebHID installation route. Keep the explicit helper there.
    companion = companion.replace(browser_guard,
        "&&globalThis.isSecureContext===true&&['http:','https:'].includes(location.protocol);", 1)
    javascript = "\n".join((core, oled, controller, menu, firmware, schema, model, ui_system, engine_studio, settings,
        rhythm, arp, mpe, library, sequencer_midi, device_song, sequencer, device_client, backup_migration,
        companion_protocol, help_topics, help_ui, app_settings, cockpit, companion_display, backup_panel, updater_client,
        browser_hid, browser_updater, browser_adapter, desktop_runtime, desktop_chrome, updater_checks, updater_art, updater_layout, updater_panel, harmony_guide, profile_panel, companion, startup, app)).replace("</script", "<\\/script")
    embedded = ('<script id="thy-core-bytes" type="application/octet-stream">' +
                base64.b64encode(assets["generated/core.wasm"]).decode("ascii") + '</script>\n' +
                '<script id="thy-runtime-data" type="application/json">' + data.replace('<', '\\u003c') + '</script>\n' +
                '<script id="thy-oled-profile" type="application/json">' + assets["generated/oled-profile.json"].decode("utf-8").replace('<', '\\u003c') + '</script>\n' +
                '<script id="thy-font-notices" type="application/json">' + json.dumps(font_notices).replace('<', '\\u003c') + '</script>\n' +
                '<script id="thy-updater-notices" type="application/json">' + json.dumps(browser_assets["THIRD_PARTY_LICENSES.txt"].decode("utf-8")).replace('<', '\\u003c') + '</script>\n' +
                '<script type="module">\n' + javascript + '\n</script>')
    html = html.replace(style_tag, '<style>\n' + assets["styles.css"].decode("utf-8") + '\n</style>')
    for name in ("startup.css", "typography.css", "companion-shell.css", "help.css", "companion-theme.css", "ui-system.css", "updater-wizard.css", "cockpit.css", "profile-panel.css", "companion-display.css", "desktop-chrome.css"):
        tag = f'<link rel="stylesheet" href="./{name}">'
        if html.count(tag) != 1:
            raise ValueError(f"Missing explicit stylesheet: {name}")
        css = assets[name].decode("utf-8")
        if name == "typography.css":
            for face in font_manifest["fonts"]:
                relative = "assets/fonts/" + face["font_path"]
                if css.count(relative) != 1:
                    raise ValueError(f"Expected one explicit local font reference: {relative}")
                css = css.replace(relative, "data:font/ttf;base64," + base64.b64encode(assets[relative]).decode("ascii"))
        html = html.replace(tag, '<style>\n' + css + '\n</style>')
    html = html.replace('src="./assets/thy666-web-logo.svg"',
        'src="data:image/svg+xml;base64,' + base64.b64encode(assets['assets/thy666-web-logo.svg']).decode('ascii') + '"')
    html = html.replace('href="./assets/thy666-web-logo.svg"',
        'href="data:image/svg+xml;base64,' + base64.b64encode(assets['assets/thy666-web-logo.svg']).decode('ascii') + '"')
    html = html.replace(script_tag, embedded).replace('href="./"', 'href="#client/board"')
    output = ROOT / "build/workbench/THY666-workbench.html"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(html, encoding="utf-8", newline="\n")
    updater_notice = output.parent / "THY666-updater-THIRD_PARTY_LICENSES.txt"
    updater_notice.write_bytes(browser_assets["THIRD_PARTY_LICENSES.txt"])
    manifest = {"format": "thy666.workbench-bundle", "version": 1,
                "output": output.relative_to(ROOT).as_posix(), "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                "source_sha256": runtime["provenance"]["source"]["sha256"],
                "assets_sha256": {name: hashlib.sha256(raw).hexdigest() for name, raw in assets.items()},
                "browser_updater": {"wasm_sha256": hashlib.sha256(browser_assets["browser-updater.wasm"]).hexdigest(),
                    "embedded": True, "firmware_included": False, "file_url_backend": "explicit-local-helper",
                    "notices": {"file": updater_notice.name, "sha256": hashlib.sha256(updater_notice.read_bytes()).hexdigest()}},
                "scope": "Self-contained Client and Documentation; explicit WebSerial control and backup/restore. Served secure pages can use the embedded shared Rust/WebHID updater; file URLs retain the explicit local Rust helper. No automatic device connection, installation or restore."}
    (output.parent / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Standalone workbench: {output} ({output.stat().st_size:,} bytes)")
    return output


if __name__ == "__main__":
    package()
