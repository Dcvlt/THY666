"""Loopback/CLI integration against the built executable, always --simulate.

Build with tools/build_updater.py first. These tests never enumerate real HID.
"""
from __future__ import annotations

import hashlib
import http.client
import json
from pathlib import Path
import queue
import socket
import subprocess
import sys
import threading
import time
import tempfile
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build/updater-target/release" / ("thy666-updater.exe" if sys.platform == "win32" else "thy666-updater")
ORIGIN = "http://127.0.0.1:3330"
BUILD = "1234567890abcdef"


def synthetic_package() -> bytes:
    fields = [(6, 1), (6, 0), (6, 1), (6, 0), (6, 512),
              (8, 0xEBF3A7F7), (8, 0x429B51D6), (8, 0xAA017D8C),
              (6, 4), (6, 0), (6, 1)]
    app = bytearray(512)
    app[:4] = (0x20020000).to_bytes(4, "little")
    app[4:8] = (0x08040000 + 393).to_bytes(4, "little")
    app[416:432] = BUILD.encode()
    return b"THY333" + "".join(f"{value:0{width}X}" for width, value in fields).encode() + app


@unittest.skipUnless(EXE.is_file(), "Build the Rust updater before loopback integration tests")
class UpdaterHelperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / "build").mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="updater-helper-", dir=ROOT / "build")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        cls.package_path = cls.directory / "synthetic.333"
        cls.package_path.write_bytes(synthetic_package())
        # An encrypted synthetic lookalike must remain unavailable. The exact
        # original allowlist is exercised in Rust without vendor test fixtures.
        encoded = bytearray(synthetic_package())
        encoded[66:72] = b"000001"
        (cls.directory / "unapproved-stock.333").write_bytes(encoded)
        cls.catalog = cls.directory / "releases.json"
        cls.catalog.write_text(json.dumps({"format": "thy666.release-catalog", "version": 1,
            "releases": [{"id": "current", "label": "Synthetic THY666", "kind": "thy666",
                "file": "synthetic.333", "build_id": BUILD, "sha256": hashlib.sha256(synthetic_package()).hexdigest()},
                {"id": "unapproved-stock", "label": "Synthetic unapproved encrypted image", "kind": "stock",
                "file": "unapproved-stock.333", "build_id": None, "sha256": hashlib.sha256(encoded).hexdigest()}]}), encoding="utf-8")
        with socket.socket() as reserve:
            reserve.bind(("127.0.0.1", 0))
            cls.port = reserve.getsockname()[1]
        cls.process = subprocess.Popen(
            [str(EXE), "serve", "--simulate", "--port", str(cls.port), "--origin", ORIGIN,
             "--releases", str(cls.catalog)],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
        lines = queue.Queue()
        def collect():
            for line in cls.process.stdout:
                lines.put(line.strip())
        threading.Thread(target=collect, daemon=True).start()
        cls.token = None
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                line = lines.get(timeout=max(0.01, deadline - time.monotonic()))
                if line.startswith("Session token (local use only): "):
                    cls.token = line.split(": ", 1)[1]
                    break
            if not cls.token:
                raise AssertionError("Simulation helper did not start")
            status, _, body = cls.post("status", {})
            if status != 200 or body.get("simulation") is not True:
                raise AssertionError("Test helper is not in simulation mode")
        except BaseException:
            cls.process.terminate()
            cls.process.wait(timeout=10)
            raise

    @classmethod
    def tearDownClass(cls):
        cls.process.terminate()
        cls.process.wait(timeout=10)
        cls.process.stdout.close()

    @classmethod
    def post(cls, route, value, *, headers=None, method="POST"):
        fields = {"Origin": ORIGIN, "Authorization": f"Bearer {cls.token}", "Content-Type": "application/json"}
        fields.update(headers or {})
        body = value if isinstance(value, bytes) else json.dumps(value).encode()
        conn = http.client.HTTPConnection("127.0.0.1", cls.port, timeout=8)
        try:
            conn.request(method, f"/v1/{route}", body=body, headers=fields)
            result = conn.getresponse()
            payload = result.read()
            return result.status, dict(result.getheaders()), json.loads(payload) if payload else None
        finally:
            conn.close()

    def prepare(self):
        raw = synthetic_package()
        status, _, response = self.post("inspect", raw, headers={
            "Content-Type": "application/octet-stream",
            "X-THY666-Filename": "THY666-instrument-alpha-12345678.333"})
        self.assertEqual(status, 200)
        self.assertEqual(response["package"]["file_sha256"], hashlib.sha256(raw).hexdigest())
        self.assertEqual(response["package"]["build_id"], BUILD)
        self.assertTrue(response["package"]["build_identity_verified"])
        self.assertEqual(response["package"]["kind"], "thy666")
        self.assertTrue(response["package"]["installable"])
        status, _, found = self.post("devices", {})
        self.assertEqual(status, 200)
        self.assertEqual(found["devices"][0]["id"], "simulation")
        self.assertTrue(found["devices"][0]["simulation"])
        return {"operation_id": uuid.uuid4().hex, "device_id": "simulation",
                "package_sha256": response["package"]["file_sha256"],
                "backup_disposition": "unavailable", "confirmed": True,
                "preparation_acknowledged": True, "physical_target_confirmed": True}

    def finish(self, operation_id):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            status, _, response = self.post("progress", {"operation_id": operation_id, "after_sequence": 0})
            self.assertEqual(status, 200)
            if response["operation"]["state"] != "running":
                return response["operation"]
            time.sleep(0.01)
        self.fail("Synthetic operation did not finish")

    def test_exact_origin_token_and_preflight(self):
        for fields in [{"Origin": "http://127.0.0.1:9999"}, {"Origin": "null"},
                       {"Authorization": "Bearer wrong"}, {"Host": "example.invalid"}]:
            status, headers, _ = self.post("status", {}, headers=fields)
            self.assertEqual(status, 403)
            self.assertNotIn("Access-Control-Allow-Origin", headers)
        status, headers, _ = self.post("status", b"", method="OPTIONS", headers={"Access-Control-Request-Method": "POST"})
        self.assertEqual(status, 204)
        self.assertEqual(headers["Access-Control-Allow-Origin"], ORIGIN)

    def test_inspection_rejects_corrupt_replacement_and_paths(self):
        request = self.prepare()
        status, _, _ = self.post("inspect", b"invalid", headers={"Content-Type": "application/octet-stream", "X-THY666-Filename": "bad.333"})
        self.assertEqual(status, 400)
        self.assertEqual(self.post("install", request)[0], 400)
        status, _, _ = self.post("inspect", synthetic_package(), headers={"Content-Type": "application/octet-stream", "X-THY666-Filename": "..%2Fimage.333"})
        self.assertEqual(status, 400)

    def test_simulated_transfer_is_idempotent_and_never_reports_physical_success(self):
        request = self.prepare()
        self.assertEqual(self.post("install", request)[0], 200)
        operation = self.finish(request["operation_id"])
        self.assertEqual(operation["state"], "awaiting-verification")
        self.assertTrue(operation["simulation"])
        self.assertEqual(operation["firmware_kind"], "thy666")
        self.assertEqual(operation["expected_build"], BUILD)
        self.assertIn("SIMULATION", operation["message"])
        status, _, repeated = self.post("install", request)
        self.assertEqual(status, 200)
        self.assertEqual(repeated["operation"]["sequence"], operation["sequence"])
        self.assertEqual(self.post("install", {**request, "physical_target_confirmed": False})[0], 400)
        # A new operation cannot reuse consumed inspection/discovery.
        self.assertEqual(self.post("install", {**request, "operation_id": uuid.uuid4().hex})[0], 400)

    def test_wrong_uid_stops_after_preparation_and_reports_uncertainty(self):
        request = {**self.prepare(), "expected_uid": "fedcba9876543210fedcba98", "backup_disposition": "downloaded"}
        self.assertEqual(self.post("install", request)[0], 200)
        operation = self.finish(request["operation_id"])
        self.assertEqual(operation["state"], "failed")
        self.assertTrue(operation["mutation_started"])
        self.assertTrue(operation["indeterminate"])
        self.assertEqual(operation["completed"], 0)

    def test_invalid_uid_type_and_missing_consent_do_not_create_operation(self):
        request = self.prepare()
        for field in ["confirmed", "preparation_acknowledged", "physical_target_confirmed"]:
            self.assertEqual(self.post("install", {**request, field: False})[0], 400)
        self.assertEqual(self.post("install", {**request, "expected_uid": 123})[0], 400)
        self.assertEqual(self.post("progress", {"operation_id": request["operation_id"]})[0], 400)

    def test_curated_catalog_lists_only_validated_entries_and_stages_only_explicit_selection(self):
        status, _, response = self.post("releases", {})
        self.assertEqual(status, 200)
        self.assertEqual(response["protocol"], "THY666-UPDATER/1")
        self.assertTrue(response["configured"])
        self.assertTrue(response["simulation"])
        releases = {entry["id"]: entry for entry in response["releases"]}
        self.assertEqual(set(releases), {"current", "unapproved-stock"})
        self.assertTrue(releases["current"]["available"])
        self.assertEqual(releases["current"]["build_id"], BUILD)
        self.assertFalse(releases["unapproved-stock"]["available"])
        self.assertTrue(releases["unapproved-stock"]["disabled_reason"])
        status, _, selected = self.post("release-select", {"id": "current"})
        self.assertEqual(status, 200)
        self.assertEqual(selected["release_id"], "current")
        self.assertEqual(selected["package"]["kind"], "thy666")
        self.assertTrue(selected["package"]["installable"])
        self.assertEqual(self.post("release-select", {"id": "unapproved-stock"})[0], 400)

    def test_failed_or_changed_curated_selection_never_reuses_an_earlier_staged_image(self):
        for identifier in [True, None, "unknown", "../synthetic.333"]:
            request = self.prepare()
            self.assertEqual(self.post("release-select", {"id": identifier})[0], 400)
            self.assertEqual(self.post("install", request)[0], 400)
        request = self.prepare()
        original = self.package_path.read_bytes()
        try:
            self.package_path.write_bytes(b"changed after listing")
            self.assertEqual(self.post("release-select", {"id": "current"})[0], 400)
            self.assertEqual(self.post("install", request)[0], 400)
            status, _, response = self.post("releases", {})
            self.assertEqual(status, 200)
            self.assertFalse(response["releases"][0]["available"])
        finally:
            self.package_path.write_bytes(original)


if __name__ == "__main__":
    unittest.main()
