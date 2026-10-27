"""Offline identity/parser/ABI checks; never load a Windows DLL or device."""
import ast
import importlib.util
import contextlib
import io
from pathlib import Path
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "tools/capture_hid_inventory.py"
SPEC = importlib.util.spec_from_file_location("capture_hid_inventory", PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class InventoryTests(unittest.TestCase):
    def test_fixed_width_structure_layouts(self):
        MODULE.validate_layouts()

    def test_cached_target_ids_accept_composite_and_collection(self):
        for value in (r"HID\VID_0333&PID_0333", r"hid\vid_0333&pid_0333&mi_01&col01",
                      r"USB\VID_0333&PID_0333&REV_0200"):
            self.assertTrue(MODULE.matches_target([value]))

    def test_paths_substrings_partial_ids_and_wrong_devices_do_not_match(self):
        for value in (r"\\?\hid#vid_0333&pid_0333#serial", r"HID\VID_0333&PID_03330",
                      r"HID\VID_03330&PID_0333", r"HID\VID_0333&PID_572b",
                      r"other-HID\VID_0333&PID_0333", "VID_0333&PID_0333"):
            self.assertFalse(MODULE.matches_target([value]))

    def test_multisz_preserves_all_ids(self):
        values = [r"HID\VID_0333&PID_0333&REV_0100", r"HID\VID_0333&PID_0333"]
        raw = ("\0".join(values) + "\0\0").encode("utf-16-le")
        self.assertEqual(MODULE.decode_multi_sz(raw), values)

    def test_multisz_rejects_truncation_invalid_unicode_and_early_terminator(self):
        for raw in (b"x", b"x\0\0\0", b"\x00\xd8\0\0\0\0",
                    "a\0\0b\0\0".encode("utf-16-le")):
            with self.assertRaises((ValueError, UnicodeError)):
                MODULE.decode_multi_sz(raw)

    def test_only_metadata_api_bindings_exist(self):
        tree = ast.parse(PATH.read_text(encoding="utf-8"))
        actual = {node.args[1].value for node in ast.walk(tree)
                  if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                  and node.func.id == "_bind"}
        self.assertEqual(actual, {"HidD_GetHidGuid", "SetupDiGetClassDevsW",
            "SetupDiEnumDeviceInterfaces", "SetupDiGetDeviceInterfaceDetailW",
            "SetupDiGetDeviceRegistryPropertyW", "SetupDiDestroyDeviceInfoList",
            "CreateFileW", "CloseHandle", "HidD_GetAttributes",
            "HidD_GetPreparsedData", "HidD_FreePreparsedData", "HidP_GetCaps"})

    def test_matched_but_access_denied_is_reported_and_retained_as_failure(self):
        # The observed held-pad inventory matched the device but could not open
        # its metadata handle. It must not be reported as zero matching devices.
        capture = {"matched_interfaces": [{"capabilities": None}],
                   "errors": [{"operation": "CreateFileW.metadata-access", "winerror": 5}]}
        output = io.StringIO()
        with mock.patch.object(MODULE, "capture_inventory", return_value=capture), \
             mock.patch.object(MODULE, "write_capture", return_value=Path("capture.json")) as save, \
             mock.patch("sys.argv", ["capture_hid_inventory.py"]), contextlib.redirect_stdout(output):
            result = MODULE.main()
        self.assertEqual(result, 2)
        save.assert_called_once_with(capture)
        self.assertIn("Matched 1 target HID interfaces; capability records 0; query errors 1.", output.getvalue())

    def test_successful_inventory_distinguishes_complete_records(self):
        capture = {"matched_interfaces": [{"capabilities": {"InputReportByteLength": 65}}],
                   "errors": []}
        output = io.StringIO()
        with mock.patch.object(MODULE, "capture_inventory", return_value=capture), \
             mock.patch.object(MODULE, "write_capture", return_value=Path("capture.json")), \
             mock.patch("sys.argv", ["capture_hid_inventory.py"]), contextlib.redirect_stdout(output):
            result = MODULE.main()
        self.assertEqual(result, 0)
        self.assertIn("Matched 1 target HID interfaces; capability records 1; query errors 0.", output.getvalue())


if __name__ == "__main__":
    unittest.main()
