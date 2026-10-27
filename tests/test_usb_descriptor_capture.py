"""Offline USB descriptor/identity/coverage checks; no Windows APIs."""
import ast
import contextlib
import io
from pathlib import Path
import struct
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import capture_usb_descriptors as module


def device(configurations=1, vendor=0x0333, revision=0x0104, product=0x0333):
    return struct.pack("<BBHBBBBHHHBBBB", 18,1,0x0200,0,0,0,64,vendor,product,revision,1,2,3,configurations)


def configuration(parts, interfaces=1, value=1):
    body = b"".join(parts)
    return struct.pack("<BBHBBBBB", 9,2,9+len(body),interfaces,value,0,0x80,50) + body


def interface(number=0, alternate=0, endpoints=0, cls=3, subclass=0, protocol=0):
    return bytes([9,4,number,alternate,endpoints,cls,subclass,protocol,0])


HID = bytes.fromhex("092111010001222300")
DFU = bytes.fromhex("092103e80340001001")
HID_CONFIG = configuration([interface(endpoints=2), HID,
    bytes.fromhex("07058103400005"), bytes.fromhex("07050103400005")])
DFU_CONFIG = configuration([interface(cls=0xfe, subclass=1, protocol=2), DFU])


class FakeHub:
    def __init__(self, configurations, fail_index=None, node_change_at=None, vid=0x0333, pid=0x0333):
        self.configurations = configurations
        self.vid, self.pid = vid, pid
        self.device = device(len(configurations), vendor=vid, product=pid)
        self.fail_index = fail_index
        self.node_change_at = node_change_at
        self.node_count = 0
        self.gets = []

    def node(self):
        self.node_count += 1
        raw = self.device
        if self.node_count == self.node_change_at:
            raw = device(len(self.configurations), vendor=self.vid, product=self.pid, revision=0x0105)
        module.require_target(raw, self.vid, self.pid)
        return raw, {"current_configuration_value": 1}

    def descriptor(self, kind, index, count):
        self.gets.append((kind,index,count))
        if kind == 1: return self.device
        if index == self.fail_index: raise OSError("Configuration query rejected")
        return self.configurations[index][:count]


class DescriptorTests(unittest.TestCase):
    def test_wdk_packed_layouts_and_any_access_ioctls(self):
        module.check_layouts()
        self.assertEqual((module.IOCTL_CONNECTION >> 14) & 3, 0)
        self.assertEqual((module.IOCTL_DESCRIPTOR >> 14) & 3, 0)

    def test_requests_are_standard_device_recipient_get_descriptor(self):
        self.assertEqual(struct.unpack("<IBBHHH", module.descriptor_request(3,1,0,18)),
                         (3,0x80,6,0x0100,0,18))
        self.assertEqual(struct.unpack("<IBBHHH", module.descriptor_request(3,2,1,41)),
                         (3,0x80,6,0x0201,0,41))

    def test_hid_report_class_vendor_and_invalid_requests_are_rejected(self):
        for request in ((3,0x22,0,65),(3,0x21,0,9),(3,3,1,8),(0,1,0,18),
                        (3,1,1,18),(3,2,8,9),(3,2,0,4097)):
            with self.assertRaises(ValueError): module.descriptor_request(*request)

    def test_physical_cached_id_filter_excludes_interface_children(self):
        self.assertTrue(module.PHYSICAL_ID.fullmatch(r"USB\VID_0333&PID_0333&REV_0104"))
        for identity in (r"HID\VID_0333&PID_0333", r"USB\VID_0333&PID_0333&MI_00",
                         r"USB\VID_0333&PID_0333&REV_0104&MI_00", r"USB\VID_0333&PID_572B"):
            self.assertFalse(module.PHYSICAL_ID.fullmatch(identity))

    def test_wrong_live_vid_stops_before_configuration_requests(self):
        hub = FakeHub([HID_CONFIG]); hub.device = device(vendor=0x1234)
        with self.assertRaises(ValueError): module.capture_from_hub(hub)
        self.assertEqual(hub.gets, [])

    def test_explicit_application_pairs_keep_exact_physical_node_filter(self):
        for vid, pid in ((0xcafe, 0x4009), (0x115c, 0x572b)):
            pattern = module.physical_id_pattern(vid, pid)
            self.assertTrue(pattern.fullmatch(f"USB\\VID_{vid:04x}&PID_{pid:04x}&REV_0001"))
            for identity in (f"USB\\VID_{vid:04x}&PID_{pid:04x}&MI_00",
                             f"HID\\VID_{vid:04x}&PID_{pid:04x}", r"USB\VID_0333&PID_0333"):
                self.assertFalse(pattern.fullmatch(identity))
            raw = device(vendor=vid, product=pid)
            module.require_target(raw, vid, pid)
            with self.assertRaises(ValueError): module.require_target(raw)
            with self.assertRaises(ValueError): module.require_target(raw, vid, pid ^ 1)
            result = module.capture_from_hub(FakeHub([HID_CONFIG], vid=vid, pid=pid), vid, pid)
            self.assertTrue(result["configuration_coverage_complete"])
            self.assertEqual(result["device"]["idVendor"], vid)
            self.assertEqual(result["device"]["idProduct"], pid)

    def test_explicit_identity_change_is_rejected_before_configuration_requests(self):
        hub = FakeHub([HID_CONFIG], vid=0x115c, pid=0x572b)
        with self.assertRaises(ValueError): module.capture_from_hub(hub, 0xcafe, 0x4009)
        self.assertEqual(hub.gets, [(1, 0, 18)])

    def test_usb_selector_rejects_out_of_range_and_malformed_values(self):
        for value, expected in (("cafe", 0xcafe), ("0X4009", 0x4009), ("0333", 0x0333)):
            self.assertEqual(module.usb_id_argument(value), expected)
        for value in ("-1", "10000", "0x10000", "cafe.*", "", " 0333 "):
            with self.assertRaises(module.argparse.ArgumentTypeError): module.usb_id_argument(value)
        for vid, pid in ((-1, 0x333), (0x333, 0x10000), (True, 0x333)):
            with self.assertRaises(ValueError): module.physical_id_pattern(vid, pid)

    def test_cli_requires_both_explicit_ids_even_in_offline_mode(self):
        for option in ("--vid", "--pid"):
            with mock.patch("sys.argv", ["capture_usb_descriptors.py", option, "0333", "--check-layouts"]), \
                 contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                module.main()
            self.assertEqual(raised.exception.code, 2)

    def test_cli_passes_selected_pair_without_loading_windows_during_mock_capture(self):
        record = {"configurations": [], "errors": [], "configuration_coverage_complete": True}
        for options, pair in (([], (0x0333, 0x0333)), (["--vid", "cafe", "--pid", "4009"], (0xcafe, 0x4009))):
            with mock.patch("sys.argv", ["capture_usb_descriptors.py", *options]), \
                 mock.patch.object(module, "capture", return_value=record) as capture, \
                 mock.patch.object(module, "write_capture", return_value="offline.json"), \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(module.main(), 0)
            capture.assert_called_once_with(*pair)

    def test_hid_descriptor_type21_does_not_create_dfu_evidence(self):
        parsed = module.parse_configuration(HID_CONFIG, 0)
        self.assertEqual(parsed["total_length"], 41)
        self.assertFalse(parsed["dfu_present_in_this_configuration"])
        self.assertEqual(parsed["interfaces"][0]["class_specific_descriptors"][0]["classification"], "HID descriptor")
        self.assertEqual([ep["address"] for ep in parsed["interfaces"][0]["endpoints"]], [0x81, 1])

    def test_dfu_functional_descriptors_keep_alternate_contexts_separate(self):
        raw = configuration([interface(cls=0xfe,subclass=1,protocol=2),DFU,
            interface(alternate=1,cls=0xfe,subclass=1,protocol=2),DFU])
        parsed = module.parse_configuration(raw, 0)
        self.assertEqual([item["alternate_setting"] for item in parsed["dfu_interfaces"]], [0,1])
        self.assertEqual([len(item["class_specific_descriptors"]) for item in parsed["interfaces"]], [1,1])
        self.assertTrue(parsed["dfu_present_in_this_configuration"])

    def test_unknown_type21_context_is_not_dfu(self):
        raw = configuration([interface(cls=0xff,subclass=1,protocol=2),DFU])
        self.assertFalse(module.parse_configuration(raw, 0)["dfu_present_in_this_configuration"])

    def test_truncation_bad_lengths_and_endpoint_counts_are_rejected(self):
        for raw in (HID_CONFIG[:-1],configuration([b"\0\x04"]),
                    configuration([interface(endpoints=1)]),configuration([interface()],interfaces=2)):
            with self.assertRaises(ValueError): module.parse_configuration(raw, 0)

    def test_failed_second_configuration_cannot_prove_dfu_absence(self):
        hub = FakeHub([HID_CONFIG,DFU_CONFIG], fail_index=1)
        result = module.capture_from_hub(hub)
        self.assertEqual(len(result["configurations"]), 1)
        self.assertFalse(result["configuration_coverage_complete"])
        self.assertIsNone(result["dfu_absent_from_all_advertised_configurations"])
        self.assertTrue(result["errors"])
        self.assertEqual(result["configuration_attempts"][0]["raw_hex"], HID_CONFIG.hex())

    def test_complete_multiple_configurations_include_the_unselected_dfu_interface(self):
        result = module.capture_from_hub(FakeHub([HID_CONFIG,DFU_CONFIG]))
        self.assertTrue(result["configuration_coverage_complete"])
        self.assertTrue(result["dfu_present_in_captured_configurations"])
        self.assertFalse(result["dfu_absent_from_all_advertised_configurations"])

    def test_complete_hid_only_capture_has_bounded_absence_claim(self):
        result = module.capture_from_hub(FakeHub([HID_CONFIG]))
        self.assertTrue(result["configuration_coverage_complete"])
        self.assertTrue(result["dfu_absent_from_all_advertised_configurations"])

    def test_identity_change_stops_before_config_get_and_final_get(self):
        hub = FakeHub([HID_CONFIG], node_change_at=2)
        result = module.capture_from_hub(hub)
        self.assertTrue(result["errors"])
        self.assertEqual(hub.gets, [(1,0,18)])
        self.assertIsNone(result["dfu_absent_from_all_advertised_configurations"])

    def test_only_hub_metadata_and_read_only_control_apis_are_bound(self):
        tree = ast.parse((ROOT / "tools/capture_usb_descriptors.py").read_text())
        names = {node.args[1].value for node in ast.walk(tree)
                 if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "_bind"}
        self.assertEqual(names, {"SetupDiGetClassDevsW", "SetupDiEnumDeviceInfo", "SetupDiGetDeviceRegistryPropertyW",
            "SetupDiGetDeviceInstanceIdW", "SetupDiDestroyDeviceInfoList", "SetupDiEnumDeviceInterfaces",
            "SetupDiGetDeviceInterfaceDetailW", "CM_Get_Parent", "CM_Get_Device_IDW",
            "CreateFileW", "CloseHandle", "DeviceIoControl"})


if __name__ == "__main__": unittest.main()
