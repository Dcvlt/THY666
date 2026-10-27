"""Capture target HID capabilities through read-only Windows metadata APIs.

No HID reports, feature requests, vendor commands, firmware operations or
configuration changes exist in this tool. Cached SetupAPI hardware IDs must
match VID0333/PID0333 before an interface is opened with desiredAccess=0.
The result records HID capabilities, NOT the raw HID report descriptor.

Primary API references:
https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/obtaining-hid-reports
https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdigetclassdevsw
https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdienumdeviceinterfaces
https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdigetdeviceinterfacedetailw
https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdigetdeviceregistrypropertyw
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidsdi/nf-hidsdi-hidd_getattributes
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidsdi/nf-hidsdi-hidd_getpreparseddata
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidpi/nf-hidpi-hidp_getcaps
"""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import uuid

ROOT = Path(__file__).resolve().parents[1]
OUTPUT_DIRECTORY = ROOT / "analysis/local/usb"
TARGET_VID = 0x0333
TARGET_PID = 0x0333
ERROR_INSUFFICIENT_BUFFER = 122
ERROR_NO_MORE_ITEMS = 259
REG_MULTI_SZ = 7
HIDP_STATUS_SUCCESS = 0x00110000
MAX_BUFFER = 65536
MAX_INTERFACES = 4096
HARDWARE_ID = re.compile(r"^(?:HID|USB)\\VID_0333&PID_0333(?:&|\\|$)", re.IGNORECASE)


class GUID(ctypes.Structure):
    _fields_ = [("Data1", ctypes.c_uint32), ("Data2", ctypes.c_uint16),
                ("Data3", ctypes.c_uint16), ("Data4", ctypes.c_uint8 * 8)]


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [("cbSize", ctypes.c_uint32), ("InterfaceClassGuid", GUID),
                ("Flags", ctypes.c_uint32), ("Reserved", ctypes.c_void_p)]


class SP_DEVINFO_DATA(ctypes.Structure):
    _fields_ = [("cbSize", ctypes.c_uint32), ("ClassGuid", GUID),
                ("DevInst", ctypes.c_uint32), ("Reserved", ctypes.c_void_p)]


class HIDD_ATTRIBUTES(ctypes.Structure):
    _fields_ = [("Size", ctypes.c_uint32), ("VendorID", ctypes.c_uint16),
                ("ProductID", ctypes.c_uint16), ("VersionNumber", ctypes.c_uint16)]


class HIDP_CAPS(ctypes.Structure):
    _fields_ = [("Usage", ctypes.c_uint16), ("UsagePage", ctypes.c_uint16),
                ("InputReportByteLength", ctypes.c_uint16),
                ("OutputReportByteLength", ctypes.c_uint16),
                ("FeatureReportByteLength", ctypes.c_uint16),
                ("Reserved", ctypes.c_uint16 * 17),
                ("NumberLinkCollectionNodes", ctypes.c_uint16),
                ("NumberInputButtonCaps", ctypes.c_uint16),
                ("NumberInputValueCaps", ctypes.c_uint16),
                ("NumberInputDataIndices", ctypes.c_uint16),
                ("NumberOutputButtonCaps", ctypes.c_uint16),
                ("NumberOutputValueCaps", ctypes.c_uint16),
                ("NumberOutputDataIndices", ctypes.c_uint16),
                ("NumberFeatureButtonCaps", ctypes.c_uint16),
                ("NumberFeatureValueCaps", ctypes.c_uint16),
                ("NumberFeatureDataIndices", ctypes.c_uint16)]


def matches_target(hardware_ids: list[str]) -> bool:
    """Match cached hardware IDs only; never infer identity from DevicePath."""
    return any(HARDWARE_ID.match(value) is not None for value in hardware_ids)


def decode_multi_sz(raw: bytes) -> list[str]:
    if len(raw) % 2 or len(raw) < 4:
        raise ValueError("Invalid cached REG_MULTI_SZ size")
    value = raw.decode("utf-16-le", errors="strict")
    if not value.endswith("\0\0"):
        raise ValueError("Cached hardware IDs lack the double-NUL terminator")
    if value == "\0\0":
        return []
    items = value[:-2].split("\0")
    if any(not item for item in items):
        raise ValueError("Cached hardware IDs contain an embedded list terminator")
    return items


def validate_layouts() -> None:
    expected_pointer_struct = 32 if ctypes.sizeof(ctypes.c_void_p) == 8 else 28
    if ctypes.sizeof(GUID) != 16 or ctypes.sizeof(HIDP_CAPS) != 64:
        raise ValueError("HID/GUID ctypes layout mismatch")
    if ctypes.sizeof(HIDD_ATTRIBUTES) != 12:
        raise ValueError("HID attributes ctypes layout mismatch")
    if ctypes.sizeof(SP_DEVICE_INTERFACE_DATA) != expected_pointer_struct or \
            ctypes.sizeof(SP_DEVINFO_DATA) != expected_pointer_struct:
        raise ValueError("SetupAPI ctypes layout mismatch")


def _bind(library, name, args, returns):
    function = getattr(library, name)
    function.argtypes = args
    function.restype = returns
    return function


def capture_inventory() -> dict:
    if os.name != "nt":
        raise RuntimeError("HID inventory requires Windows; no device API was opened")
    validate_layouts()
    # DLL loading and API binding happen only when an actual capture is requested.
    setup = ctypes.WinDLL("setupapi", use_last_error=True)
    hid = ctypes.WinDLL("hid", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    P_GUID = ctypes.POINTER(GUID)
    P_INTERFACE = ctypes.POINTER(SP_DEVICE_INTERFACE_DATA)
    P_DEVICE = ctypes.POINTER(SP_DEVINFO_DATA)
    P_DWORD = ctypes.POINTER(wintypes.DWORD)
    get_guid = _bind(hid, "HidD_GetHidGuid", [P_GUID], None)
    class_devs = _bind(setup, "SetupDiGetClassDevsW",
                       [P_GUID, wintypes.LPCWSTR, wintypes.HWND, wintypes.DWORD], wintypes.HANDLE)
    enum_interfaces = _bind(setup, "SetupDiEnumDeviceInterfaces",
                            [wintypes.HANDLE, P_DEVICE, P_GUID, wintypes.DWORD, P_INTERFACE], wintypes.BOOL)
    get_detail = _bind(setup, "SetupDiGetDeviceInterfaceDetailW",
                      [wintypes.HANDLE, P_INTERFACE, ctypes.c_void_p, wintypes.DWORD, P_DWORD, P_DEVICE], wintypes.BOOL)
    get_property = _bind(setup, "SetupDiGetDeviceRegistryPropertyW",
                        [wintypes.HANDLE, P_DEVICE, wintypes.DWORD, P_DWORD,
                         ctypes.c_void_p, wintypes.DWORD, P_DWORD], wintypes.BOOL)
    destroy_list = _bind(setup, "SetupDiDestroyDeviceInfoList", [wintypes.HANDLE], wintypes.BOOL)
    create_file = _bind(kernel, "CreateFileW",
                       [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE], wintypes.HANDLE)
    close_handle = _bind(kernel, "CloseHandle", [wintypes.HANDLE], wintypes.BOOL)
    get_attributes = _bind(hid, "HidD_GetAttributes",
                          [wintypes.HANDLE, ctypes.POINTER(HIDD_ATTRIBUTES)], ctypes.c_ubyte)
    get_preparsed = _bind(hid, "HidD_GetPreparsedData",
                         [wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p)], ctypes.c_ubyte)
    free_preparsed = _bind(hid, "HidD_FreePreparsedData", [ctypes.c_void_p], ctypes.c_ubyte)
    get_caps = _bind(hid, "HidP_GetCaps",
                    [ctypes.c_void_p, ctypes.POINTER(HIDP_CAPS)], ctypes.c_int32)
    invalid_handle = ctypes.c_void_p(-1).value

    guid = GUID()
    get_guid(ctypes.byref(guid))
    devices = class_devs(ctypes.byref(guid), None, None, 0x12)  # PRESENT|DEVICEINTERFACE.
    if devices == invalid_handle:
        raise ctypes.WinError(ctypes.get_last_error())

    record = {
        "format": "thy333.windows-hid-capabilities", "version": 1,
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "target": {"vid": "0333", "pid": "0333"},
        "access": "CreateFileW desiredAccess=0; share READ|WRITE; metadata queries only",
        "scope": "HID capabilities only; not raw report descriptors, bootloader independence or backup evidence",
        "reports_read_or_written": False, "feature_requests": False,
        "vendor_commands": False, "configuration_changes": False,
        "enumerated_hid_interfaces": 0, "matched_interfaces": [], "errors": [],
    }

    def error(index, operation, code=None):
        record["errors"].append({"interface_index": index, "operation": operation,
                                  "winerror": ctypes.get_last_error() if code is None else code})

    try:
        for index in range(MAX_INTERFACES):
            interface = SP_DEVICE_INTERFACE_DATA()
            interface.cbSize = ctypes.sizeof(interface)
            if not enum_interfaces(devices, None, ctypes.byref(guid), index, ctypes.byref(interface)):
                code = ctypes.get_last_error()
                if code == ERROR_NO_MORE_ITEMS:
                    break
                raise ctypes.WinError(code)
            record["enumerated_hid_interfaces"] += 1
            required = wintypes.DWORD()
            first = get_detail(devices, ctypes.byref(interface), None, 0, ctypes.byref(required), None)
            if first or ctypes.get_last_error() != ERROR_INSUFFICIENT_BUFFER or not 6 <= required.value <= MAX_BUFFER:
                error(index, "SetupDiGetDeviceInterfaceDetailW.size")
                continue
            detail = ctypes.create_string_buffer(required.value)
            ctypes.c_uint32.from_buffer(detail).value = 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6
            device = SP_DEVINFO_DATA()
            device.cbSize = ctypes.sizeof(device)
            if not get_detail(devices, ctypes.byref(interface), detail, required.value,
                              ctypes.byref(required), ctypes.byref(device)):
                error(index, "SetupDiGetDeviceInterfaceDetailW")
                continue
            raw_path = detail.raw[4:required.value]
            if len(raw_path) % 2:
                error(index, "DevicePath.invalid-size", 0)
                continue
            try:
                path_value = raw_path.decode("utf-16-le", errors="strict")
                if "\0" not in path_value:
                    raise ValueError("Unterminated DevicePath")
                device_path = path_value.split("\0", 1)[0]
            except (UnicodeError, ValueError):
                error(index, "DevicePath.invalid-UTF16", 0)
                continue

            property_type = wintypes.DWORD()
            required_ids = wintypes.DWORD()
            first = get_property(devices, ctypes.byref(device), 1, ctypes.byref(property_type),
                                 None, 0, ctypes.byref(required_ids))  # SPDRP_HARDWAREID.
            if first or ctypes.get_last_error() != ERROR_INSUFFICIENT_BUFFER or not 4 <= required_ids.value <= MAX_BUFFER:
                error(index, "SetupDiGetDeviceRegistryPropertyW.size")
                continue
            ids_buffer = ctypes.create_string_buffer(required_ids.value)
            if not get_property(devices, ctypes.byref(device), 1, ctypes.byref(property_type),
                                ids_buffer, required_ids.value, ctypes.byref(required_ids)):
                error(index, "SetupDiGetDeviceRegistryPropertyW")
                continue
            try:
                if property_type.value != REG_MULTI_SZ:
                    raise ValueError("Hardware IDs are not REG_MULTI_SZ")
                hardware_ids = decode_multi_sz(ids_buffer.raw[:required_ids.value])
            except (UnicodeError, ValueError):
                error(index, "HardwareIDs.invalid-REG_MULTI_SZ", 0)
                continue
            if not matches_target(hardware_ids):
                continue

            item = {"interface_index": index, "device_path": device_path,
                    "hardware_ids": hardware_ids, "attributes": None, "capabilities": None}
            record["matched_interfaces"].append(item)
            handle = create_file(device_path, 0, 3, None, 3, 0, None)
            if handle == invalid_handle:
                error(index, "CreateFileW.metadata-access")
                continue
            preparsed = ctypes.c_void_p()
            try:
                attributes = HIDD_ATTRIBUTES()
                attributes.Size = ctypes.sizeof(attributes)
                if not get_attributes(handle, ctypes.byref(attributes)):
                    error(index, "HidD_GetAttributes")
                    continue
                item["attributes"] = {"vid": f"{attributes.VendorID:04x}",
                                       "pid": f"{attributes.ProductID:04x}",
                                       "version_number": f"{attributes.VersionNumber:04x}"}
                if (attributes.VendorID, attributes.ProductID) != (TARGET_VID, TARGET_PID):
                    error(index, "Attributes.changed-identity", 0)
                    continue
                if not get_preparsed(handle, ctypes.byref(preparsed)):
                    error(index, "HidD_GetPreparsedData")
                    continue
                caps = HIDP_CAPS()
                status = get_caps(preparsed, ctypes.byref(caps))
                if status != HIDP_STATUS_SUCCESS:
                    item["hidp_status"] = f"0x{status & 0xffffffff:08x}"
                    error(index, "HidP_GetCaps", 0)
                    continue
                item["capabilities"] = {name: int(getattr(caps, name))
                                        for name, _ in HIDP_CAPS._fields_ if name != "Reserved"}
            finally:
                if preparsed.value:
                    free_preparsed(preparsed)
                close_handle(handle)
        else:
            raise ValueError("HID interface enumeration exceeded its finite bound")
    finally:
        destroy_list(devices)
    return record


def write_capture(record: dict) -> Path:
    directory = OUTPUT_DIRECTORY.resolve()
    if not directory.is_relative_to(ROOT):
        raise ValueError("Inventory output directory escapes the project")
    directory.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output = directory / f"hid-capabilities-{timestamp}-{uuid.uuid4().hex[:8]}.json"
    with output.open("x", encoding="utf-8") as stream:
        json.dump(record, stream, indent=2)
        stream.write("\n")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-layouts", action="store_true",
                        help="Check ctypes structures only; load no Windows DLL or device API")
    args = parser.parse_args()
    if args.check_layouts:
        validate_layouts()
        print("ctypes layouts passed; no Windows API or device operation performed")
        return 0
    record = capture_inventory()
    output = write_capture(record)
    matched = len(record["matched_interfaces"])
    complete = sum(item["capabilities"] is not None for item in record["matched_interfaces"])
    errors = len(record["errors"])
    print(f"Matched {matched} target HID interfaces; capability records {complete}; query errors {errors}.")
    print(f"Capture: {output}")
    print("Metadata only; this does not prove updater entry, report contents or backup/recovery.")
    return 2 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
