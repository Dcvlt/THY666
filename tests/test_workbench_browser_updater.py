"""Check the standalone's bounded in-memory updater transport, without USB."""
import base64
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from package_workbench import browser_updater_adapter


class EmbeddedUpdaterAdapterTests(unittest.TestCase):
    def test_only_two_embedded_assets_are_read_without_network_or_device_actions(self):
        node = shutil.which("node")
        self.assertIsNotNone(node)
        files = {"browser-updater.wasm": b"\0asm\x01\0\0\0",
                 "browser-updater.json": b'{"format":"synthetic"}'}
        script = """
import assert from 'node:assert/strict';
const location={protocol:'https:',href:'https://example.test/project/workbench.html'};
let created=0,network=0;
const createBrowserUpdaterClient=options=>{created++;return options;};
globalThis.fetch=()=>{network++;throw new Error('Network fallback is forbidden.');};
""" + browser_updater_adapter(files) + """
assert.equal(created,0);
const client=createBundledBrowserUpdaterClient({unused:true});
assert.equal(created,1);assert.equal(client.releasesURL,null);
const response=await client.fetch(client.wasmURL.href,{credentials:'omit',redirect:'error'});
assert.equal(response.status,200);assert.equal(response.headers.get('Content-Type'),'application/wasm');
const first=new Uint8Array(await response.arrayBuffer());
assert.deepEqual([...first],[0,97,115,109,1,0,0,0]);first.fill(9);
assert.deepEqual([...new Uint8Array(await (await client.fetch(client.wasmURL)).arrayBuffer())],[0,97,115,109,1,0,0,0]);
assert.equal(await (await client.fetch(client.coreManifestURL)).text(),'{"format":"synthetic"}');
for(const url of [client.wasmURL.href+'?other',client.wasmURL.href+'#other',
  'https://other.example.test/browser-updater.wasm',
  'https://user@example.test/project/__thy666_embedded_updater__/browser-updater.wasm',
  'https://example.test/project/private.thyb'])
  await assert.rejects(client.fetch(url),/only its two embedded core assets/);
await assert.rejects(client.fetch(client.wasmURL,{method:'POST'}),/only its two embedded core assets/);
await assert.rejects(client.fetch(client.wasmURL,{body:new Uint8Array([1])}),/only its two embedded core assets/);
await assert.rejects(client.fetch(client.wasmURL,{signal:AbortSignal.abort()}),/aborted/);
location.protocol='file:';location.href='file:///synthetic/workbench.html';
assert.throws(()=>createBundledBrowserUpdaterClient({}),/local Rust helper/);
assert.equal(created,1);assert.equal(network,0);
console.log('Embedded updater adapter verified; no network or USB.');
"""
        result = subprocess.run([node, "--input-type=module"], input=script, cwd=ROOT,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr[-2500:])


@unittest.skipUnless((ROOT / "build/workbench/THY666-workbench.html").exists(),
                     "Build the local standalone first")
class EmbeddedUpdaterBundleTests(unittest.TestCase):
    def test_exact_verified_core_and_dependency_notices_travel_with_the_bundle(self):
        output = ROOT / "build/workbench/THY666-workbench.html"
        html = output.read_text(encoding="utf-8")
        match = re.search(r"const encoded = (\{[^\n]+\});", html)
        self.assertIsNotNone(match, "The direct updater must have bounded embedded assets")
        encoded = json.loads(match[1])
        self.assertEqual(set(encoded), {"browser-updater.wasm", "browser-updater.json"})
        for name, value in encoded.items():
            self.assertEqual(base64.b64decode(value), (ROOT / "web/generated" / name).read_bytes())
        notice = re.search(r'<script id="thy-updater-notices" type="application/json">([^<]+)</script>', html)
        self.assertIsNotNone(notice)
        expected = (ROOT / "updater/THIRD_PARTY_LICENSES.txt").read_bytes()
        self.assertEqual(json.loads(notice[1]).encode(), expected)
        manifest = json.loads((output.parent / "manifest.json").read_text())
        record = manifest["browser_updater"]
        self.assertTrue(record["embedded"])
        self.assertFalse(record["firmware_included"])
        self.assertEqual(record["file_url_backend"], "explicit-local-helper")
        self.assertEqual((output.parent / record["notices"]["file"]).read_bytes(), expected)
        self.assertEqual(record["notices"]["sha256"], hashlib.sha256(expected).hexdigest())
        self.assertIn("&&['http:','https:'].includes(location.protocol)", html)


if __name__ == "__main__":
    unittest.main()
