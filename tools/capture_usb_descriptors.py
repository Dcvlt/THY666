"""Read TheoryBoard USB descriptors through its Windows parent hub.

Only documented GET_NODE_CONNECTION_INFORMATION_EX and GET_DESCRIPTOR_FROM_
NODE_CONNECTION IOCTLs exist. No vendor requests, firmware reads/writes,
set-configuration, port reset, driver changes or HID OUT reports.
Windows DLLs are loaded only for an explicit capture, not offline checks.
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
import struct
import uuid

from capture_hid_inventory import (GUID, SP_DEVINFO_DATA, SP_DEVICE_INTERFACE_DATA,
    _bind, decode_multi_sz, ROOT, MAX_BUFFER, MAX_INTERFACES,
    ERROR_INSUFFICIENT_BUFFER, ERROR_NO_MORE_ITEMS, validate_layouts)

# Microsoft usbioctl.h/usbiodef.h: FILE_DEVICE_USB=UNKNOWN=0x22,
# function260/274, METHOD_BUFFERED=0, FILE_ANY_ACCESS=0.
IOCTL_CONNECTION = (0x22 << 16) | (274 << 2)
IOCTL_DESCRIPTOR = (0x22 << 16) | (260 << 2)
MAX_CONFIGS = 8
MAX_CONFIGURATION_BYTES = 4096
DEFAULT_VID, DEFAULT_PID = 0x0333, 0x0333


def physical_id_pattern(vid=DEFAULT_VID, pid=DEFAULT_PID):
    if any(type(value) is not int or not 0 <= value <= 0xffff for value in (vid, pid)):
        raise ValueError("USB VID and PID must be unsigned 16-bit integers")
    return re.compile(rf"^USB\\VID_{vid:04X}&PID_{pid:04X}(?:&REV_[0-9A-F]{{4}})?$", re.IGNORECASE)


# Backward-compatible stock/updater selection for importers and existing records.
PHYSICAL_ID = physical_id_pattern()


class USB_DEVICE_DESCRIPTOR(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [("bLength", ctypes.c_uint8), ("bDescriptorType", ctypes.c_uint8),
        ("bcdUSB", ctypes.c_uint16), ("bDeviceClass", ctypes.c_uint8),
        ("bDeviceSubClass", ctypes.c_uint8), ("bDeviceProtocol", ctypes.c_uint8),
        ("bMaxPacketSize0", ctypes.c_uint8), ("idVendor", ctypes.c_uint16),
        ("idProduct", ctypes.c_uint16), ("bcdDevice", ctypes.c_uint16),
        ("iManufacturer", ctypes.c_uint8), ("iProduct", ctypes.c_uint8),
        ("iSerialNumber", ctypes.c_uint8), ("bNumConfigurations", ctypes.c_uint8)]


class USB_NODE_CONNECTION_INFORMATION_EX(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [("ConnectionIndex", ctypes.c_uint32), ("DeviceDescriptor", USB_DEVICE_DESCRIPTOR),
        ("CurrentConfigurationValue", ctypes.c_uint8), ("Speed", ctypes.c_uint8),
        ("DeviceIsHub", ctypes.c_uint8), ("DeviceAddress", ctypes.c_uint16),
        ("NumberOfOpenPipes", ctypes.c_uint32), ("ConnectionStatus", ctypes.c_uint32)]


class USB_DESCRIPTOR_REQUEST(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [("ConnectionIndex", ctypes.c_uint32), ("bmRequest", ctypes.c_uint8),
        ("bRequest", ctypes.c_uint8), ("wValue", ctypes.c_uint16),
        ("wIndex", ctypes.c_uint16), ("wLength", ctypes.c_uint16)]


def check_layouts():
    validate_layouts()
    if (ctypes.sizeof(USB_DEVICE_DESCRIPTOR), ctypes.sizeof(USB_NODE_CONNECTION_INFORMATION_EX),
        ctypes.sizeof(USB_DESCRIPTOR_REQUEST)) != (18, 35, 12):
        raise ValueError("Packed WDK USB structure sizes differ")
    if (USB_NODE_CONNECTION_INFORMATION_EX.DeviceAddress.offset,
        USB_NODE_CONNECTION_INFORMATION_EX.ConnectionStatus.offset) != (25, 31):
        raise ValueError("Packed WDK USB field offsets differ")
    if (IOCTL_CONNECTION, IOCTL_DESCRIPTOR) != (0x220448, 0x220410):
        raise ValueError("Read-only USB IOCTL encoding differs")


def parse_device(raw):
    if len(raw) != 18 or raw[:2] != b"\x12\x01":
        raise ValueError("Incomplete/invalid USB device descriptor")
    descriptor = USB_DEVICE_DESCRIPTOR.from_buffer_copy(raw)
    return {name: int(getattr(descriptor, name)) for name, _ in descriptor._fields_}


def require_target(raw, vid=DEFAULT_VID, pid=DEFAULT_PID):
    physical_id_pattern(vid, pid)  # Validate explicit selectors before using them.
    result = parse_device(raw)
    if (result["idVendor"], result["idProduct"]) != (vid, pid):
        raise ValueError(f"Live USB identity differs from target {vid:04x}:{pid:04x}")
    return result


def parse_configuration(raw, index):
    if len(raw) < 9 or raw[:2] != b"\x09\x02":
        raise ValueError("Invalid configuration header")
    total = int.from_bytes(raw[2:4], "little")
    if total != len(raw):
        raise ValueError("Configuration capture is not exactly wTotalLength")
    result = {"descriptor_index": index, "configuration_value": raw[5],
        "total_length": total, "bNumInterfaces": raw[4], "attributes": raw[7],
        "max_power_units_2mA": raw[8], "raw_hex": raw.hex(), "interfaces": [],
        "other_descriptors": [], "dfu_interfaces": []}
    current = None
    seen = set()
    offset = 9
    while offset < len(raw):
        if offset + 2 > len(raw): raise ValueError("Truncated descriptor header")
        length, kind = raw[offset:offset + 2]
        if length < 2 or offset + length > len(raw): raise ValueError("Descriptor length exceeds configuration")
        entry = raw[offset:offset + length]
        if kind == 4:
            if length < 9: raise ValueError("Truncated interface descriptor")
            key = (entry[2], entry[3])
            if key in seen: raise ValueError("Duplicate interface/alternate-setting pair")
            seen.add(key)
            current = {"number": entry[2], "alternate_setting": entry[3],
                "bNumEndpoints": entry[4], "class": entry[5], "subclass": entry[6],
                "protocol": entry[7], "string_index": entry[8], "raw_hex": entry.hex(),
                "endpoints": [], "class_specific_descriptors": []}
            result["interfaces"].append(current)
            if (entry[5], entry[6]) == (0xfe, 1) and entry[7] in (1, 2):
                result["dfu_interfaces"].append({"number": entry[2], "alternate_setting": entry[3],
                    "protocol": entry[7], "mode": "runtime" if entry[7] == 1 else "DFU"})
        elif kind == 5:
            if current is None or length < 7: raise ValueError("Endpoint lacks a valid interface")
            current["endpoints"].append({"address": entry[2], "direction": "IN" if entry[2] & 0x80 else "OUT",
                "transfer_type": entry[3] & 3, "attributes": entry[3],
                "max_packet_field": int.from_bytes(entry[4:6], "little"), "interval": entry[6],
                "raw_hex": entry.hex()})
        elif kind == 0x21 and current is not None:
            descriptor = {"type": kind, "raw_hex": entry.hex(), "classification": "unclassified"}
            context = (current["class"], current["subclass"], current["protocol"])
            if context[0:2] == (0xfe, 1) and context[2] in (1, 2):
                if length != 9: raise ValueError("Invalid DFU functional descriptor length")
                descriptor.update({"classification": "DFU functional", "attributes": entry[2],
                    "detach_timeout_ms": int.from_bytes(entry[3:5], "little"),
                    "transfer_size": int.from_bytes(entry[5:7], "little"),
                    "bcdDFUVersion": int.from_bytes(entry[7:9], "little")})
            elif current["class"] == 3:
                descriptor["classification"] = "HID descriptor"
            current["class_specific_descriptors"].append(descriptor)
        else:
            result["other_descriptors"].append({"offset": offset, "type": kind, "raw_hex": entry.hex()})
        offset += length
    if len({item["number"] for item in result["interfaces"]}) != raw[4]:
        raise ValueError("Distinct interface count differs from bNumInterfaces")
    for interface in result["interfaces"]:
        if len(interface["endpoints"]) != interface["bNumEndpoints"]:
            raise ValueError("Endpoint count differs within an alternate setting")
    result["dfu_present_in_this_configuration"] = bool(result["dfu_interfaces"])
    return result


def descriptor_request(port, kind, index, count):
    if not 1 <= port <= 255 or kind not in (1, 2):
        raise ValueError("Only DEVICE/CONFIGURATION standard GET_DESCRIPTOR is permitted")
    if kind == 1 and (index != 0 or count != 18): raise ValueError("Device request must be index0/18 bytes")
    if kind == 2 and (not 0 <= index < MAX_CONFIGS or not 9 <= count <= MAX_CONFIGURATION_BYTES):
        raise ValueError("Configuration request exceeds offline bounds")
    # Driver overwrites bmRequest/bRequest to80/06; set those exact values too.
    return struct.pack("<IBBHHH", port, 0x80, 6, (kind << 8) | index, 0, count)


class WindowsMetadata:
    def __init__(self):
        self.setup = ctypes.WinDLL("setupapi", use_last_error=True)
        self.cfg = ctypes.WinDLL("cfgmgr32", use_last_error=True)
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        p_device, p_guid = ctypes.POINTER(SP_DEVINFO_DATA), ctypes.POINTER(GUID)
        self.class_devs = _bind(self.setup, "SetupDiGetClassDevsW",
            [p_guid, wintypes.LPCWSTR, wintypes.HWND, wintypes.DWORD], wintypes.HANDLE)
        self.enum_device = _bind(self.setup, "SetupDiEnumDeviceInfo",
            [wintypes.HANDLE, wintypes.DWORD, p_device], wintypes.BOOL)
        self.property = _bind(self.setup, "SetupDiGetDeviceRegistryPropertyW",
            [wintypes.HANDLE, p_device, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
             ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL)
        self.instance_id = _bind(self.setup, "SetupDiGetDeviceInstanceIdW",
            [wintypes.HANDLE, p_device, wintypes.LPWSTR, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL)
        self.destroy = _bind(self.setup, "SetupDiDestroyDeviceInfoList", [wintypes.HANDLE], wintypes.BOOL)
        self.parent = _bind(self.cfg, "CM_Get_Parent",
            [ctypes.POINTER(ctypes.c_uint32), ctypes.c_uint32, ctypes.c_uint32], ctypes.c_uint32)
        self.device_id = _bind(self.cfg, "CM_Get_Device_IDW",
            [ctypes.c_uint32, wintypes.LPWSTR, ctypes.c_uint32, ctypes.c_uint32], ctypes.c_uint32)

    def cached_property(self, devices, device, prop):
        size, kind = wintypes.DWORD(), wintypes.DWORD()
        ok = self.property(devices, ctypes.byref(device), prop, ctypes.byref(kind), None, 0, ctypes.byref(size))
        error = ctypes.get_last_error()
        if not ok and error == 13: return None, None  # Property unavailable.
        if ok or error != ERROR_INSUFFICIENT_BUFFER or not 1 <= size.value <= MAX_BUFFER:
            raise ctypes.WinError(error)
        buffer = ctypes.create_string_buffer(size.value)
        if not self.property(devices, ctypes.byref(device), prop, ctypes.byref(kind),
                             buffer, size.value, ctypes.byref(size)):
            raise ctypes.WinError(ctypes.get_last_error())
        return kind.value, buffer.raw[:size.value]

    def target_port(self, vid=DEFAULT_VID, pid=DEFAULT_PID):
        identity = physical_id_pattern(vid, pid)
        devices = self.class_devs(None, None, None, 6)  # PRESENT|ALLCLASSES.
        if devices == ctypes.c_void_p(-1).value: raise ctypes.WinError(ctypes.get_last_error())
        candidates = []
        try:
            for index in range(MAX_INTERFACES):
                device = SP_DEVINFO_DATA(); device.cbSize = ctypes.sizeof(device)
                if not self.enum_device(devices, index, ctypes.byref(device)):
                    error = ctypes.get_last_error()
                    if error == ERROR_NO_MORE_ITEMS: break
                    raise ctypes.WinError(error)
                kind, raw = self.cached_property(devices, device, 1)  # HARDWAREID.
                if kind != 7 or raw is None: continue
                ids = decode_multi_sz(raw)
                if not any(identity.fullmatch(item) for item in ids): continue
                kind, raw = self.cached_property(devices, device, 0x1c)  # ADDRESS: USB parent-hub port.
                if kind != 4 or raw is None or len(raw) != 4: raise ValueError("USB port-address property is unavailable")
                port = int.from_bytes(raw, "little")
                if not 1 <= port <= 255: raise ValueError("USB port address is invalid")
                parent = ctypes.c_uint32()
                if self.parent(ctypes.byref(parent), device.DevInst, 0) != 0: raise ValueError("CM_Get_Parent failed")
                parent_id = ctypes.create_unicode_buffer(512)
                if self.device_id(parent.value, parent_id, len(parent_id), 0) != 0: raise ValueError("Parent node ID unavailable")
                instance = ctypes.create_unicode_buffer(512)
                if not self.instance_id(devices, ctypes.byref(device), instance, len(instance), None):
                    raise ctypes.WinError(ctypes.get_last_error())
                candidates.append({"hardware_ids": ids, "instance_id": instance.value,
                    "parent_instance_id": parent_id.value, "parent_devinst": parent.value, "port": port})
            else: raise ValueError("PnP enumeration exceeded its finite bound")
        finally: self.destroy(devices)
        if len(candidates) != 1: raise ValueError(f"Expected one physical {vid:04x}:{pid:04x} USB node, found {len(candidates)}")
        return candidates[0]

    def hub_path(self, target):
        guid = GUID(0xf18a0e88, 0xc30c, 0x11d0, (ctypes.c_uint8 * 8)(0x88,0x15,0,0xa0,0xc9,6,0xbe,0xd8))
        enum = _bind(self.setup, "SetupDiEnumDeviceInterfaces", [wintypes.HANDLE, ctypes.POINTER(SP_DEVINFO_DATA),
            ctypes.POINTER(GUID), wintypes.DWORD, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA)], wintypes.BOOL)
        detail_api = _bind(self.setup, "SetupDiGetDeviceInterfaceDetailW", [wintypes.HANDLE,
            ctypes.POINTER(SP_DEVICE_INTERFACE_DATA), ctypes.c_void_p, wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(SP_DEVINFO_DATA)], wintypes.BOOL)
        devices = self.class_devs(ctypes.byref(guid), None, None, 0x12)
        if devices == ctypes.c_void_p(-1).value: raise ctypes.WinError(ctypes.get_last_error())
        paths = []
        try:
            for index in range(MAX_INTERFACES):
                interface = SP_DEVICE_INTERFACE_DATA(); interface.cbSize = ctypes.sizeof(interface)
                if not enum(devices, None, ctypes.byref(guid), index, ctypes.byref(interface)):
                    error = ctypes.get_last_error()
                    if error == ERROR_NO_MORE_ITEMS: break
                    raise ctypes.WinError(error)
                size = wintypes.DWORD()
                ok = detail_api(devices, ctypes.byref(interface), None, 0, ctypes.byref(size), None)
                if ok or ctypes.get_last_error() != ERROR_INSUFFICIENT_BUFFER or not 6 <= size.value <= MAX_BUFFER:
                    raise ValueError("Hub interface detail size unavailable")
                buffer = ctypes.create_string_buffer(size.value)
                ctypes.c_uint32.from_buffer(buffer).value = 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6
                device = SP_DEVINFO_DATA(); device.cbSize = ctypes.sizeof(device)
                if not detail_api(devices, ctypes.byref(interface), buffer, size.value, ctypes.byref(size), ctypes.byref(device)):
                    raise ctypes.WinError(ctypes.get_last_error())
                if device.DevInst != target["parent_devinst"]: continue
                path = buffer.raw[4:size.value].decode("utf-16-le")
                if "\0" not in path: raise ValueError("Hub path is unterminated")
                paths.append(path.split("\0", 1)[0])
            else: raise ValueError("Hub enumeration exceeded its finite bound")
        finally: self.destroy(devices)
        if len(paths) != 1: raise ValueError("Cached target parent does not match exactly one USB hub interface")
        return paths[0]


class Hub:
    def __init__(self, metadata, path, port, vid=DEFAULT_VID, pid=DEFAULT_PID):
        physical_id_pattern(vid, pid)
        self.vid, self.pid = vid, pid
        self.port = port
        self.queries = []
        create = _bind(metadata.kernel, "CreateFileW", [wintypes.LPCWSTR, wintypes.DWORD,
            wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE], wintypes.HANDLE)
        self.close_api = _bind(metadata.kernel, "CloseHandle", [wintypes.HANDLE], wintypes.BOOL)
        self.ioctl = _bind(metadata.kernel, "DeviceIoControl", [wintypes.HANDLE, wintypes.DWORD,
            ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p], wintypes.BOOL)
        self.handle = create(path, 0, 3, None, 3, 0, None)  # DesiredAccess0; no write-access fallback.
        if self.handle == ctypes.c_void_p(-1).value: raise ctypes.WinError(ctypes.get_last_error())

    def query(self, code, buffer):
        if code not in (IOCTL_CONNECTION, IOCTL_DESCRIPTOR): raise ValueError("IOCTL outside read-only allowlist")
        entry = {"ioctl": f"0x{code:08x}", "buffer_bytes": len(buffer), "completed": False}
        if code == IOCTL_DESCRIPTOR:
            request = USB_DESCRIPTOR_REQUEST.from_buffer(buffer)
            entry.update({"descriptor_type": request.wValue >> 8, "descriptor_index": request.wValue & 255,
                          "request_header_hex": buffer.raw[:12].hex()})
        self.queries.append(entry)
        returned = wintypes.DWORD()
        if not self.ioctl(self.handle, code, buffer, len(buffer), buffer, len(buffer), ctypes.byref(returned), None):
            entry["winerror"] = ctypes.get_last_error()
            raise ctypes.WinError(entry["winerror"])
        if returned.value > len(buffer): raise ValueError("Hub returned more bytes than allocated")
        entry.update({"returned_bytes": returned.value, "completed": True})
        return returned.value

    def node(self):
        buffer = ctypes.create_string_buffer(35 + 32 * 11)  # Room for at most32 USB_PIPE_INFO records.
        info = USB_NODE_CONNECTION_INFORMATION_EX.from_buffer(buffer)
        info.ConnectionIndex = self.port
        count = self.query(IOCTL_CONNECTION, buffer)
        if count < 35 or info.ConnectionIndex != self.port or info.ConnectionStatus != 1 or info.DeviceIsHub:
            raise ValueError("Target port is not a connected non-hub device")
        raw = bytes(info.DeviceDescriptor)
        require_target(raw, self.vid, self.pid)
        return raw, {"current_configuration_value": info.CurrentConfigurationValue,
            "speed_enum": info.Speed, "usb_device_address": info.DeviceAddress,
            "number_open_pipes": info.NumberOfOpenPipes, "connection_status": info.ConnectionStatus}

    def descriptor(self, kind, index, count):
        buffer = ctypes.create_string_buffer(12 + count)
        header = descriptor_request(self.port, kind, index, count)
        buffer[:12] = header
        returned = self.query(IOCTL_DESCRIPTOR, buffer)
        if returned != 12 + count: raise ValueError("Short/oversized descriptor capture")
        self.queries[-1].update({"descriptor_type": kind, "descriptor_index": index,
                                "request_header_hex": header.hex()})
        return buffer.raw[12:returned]

    def close(self): self.close_api(self.handle)


def capture_from_hub(hub, vid=DEFAULT_VID, pid=DEFAULT_PID):
    cached, connection = hub.node()
    live = hub.descriptor(1, 0, 18)
    device = require_target(live, vid, pid)
    if live != cached: raise ValueError("Live and enumerated device descriptors changed")
    if not 1 <= device["bNumConfigurations"] <= MAX_CONFIGS: raise ValueError("Advertised configuration count outside bounds")
    result = {"device": device, "device_raw_hex": live.hex(), "connection": connection,
        "configurations": [], "configuration_attempts": [], "configuration_coverage_complete": False,
        "dfu_present_in_captured_configurations": False,
        "dfu_absent_from_all_advertised_configurations": None, "errors": []}
    for index in range(device["bNumConfigurations"]):
        attempt = {"descriptor_index": index, "completed": False}
        result["configuration_attempts"].append(attempt)
        try:
            current, _ = hub.node()
            if current != live: raise ValueError("Port identity changed before configuration query")
            header = hub.descriptor(2, index, 9)
            attempt["header_hex"] = header.hex()
            if header[:2] != b"\x09\x02": raise ValueError("Invalid configuration header")
            total = int.from_bytes(header[2:4], "little")
            if not 9 <= total <= MAX_CONFIGURATION_BYTES: raise ValueError("Configuration total length outside bound")
            current, _ = hub.node()
            if current != live: raise ValueError("Port identity changed before full configuration query")
            raw = hub.descriptor(2, index, total)
            attempt["raw_hex"] = raw.hex()
            if raw[:9] != header: raise ValueError("Configuration header changed between reads")
            parsed = parse_configuration(raw, index)
            attempt["completed"] = True
            result["configurations"].append(parsed)
            result["dfu_present_in_captured_configurations"] |= parsed["dfu_present_in_this_configuration"]
        except Exception as exc:
            result["errors"].append(f"Configuration{index}: {type(exc).__name__}: {exc}")
            break
    if not result["errors"]:
        try:
            current, _ = hub.node()
            if current != live: raise ValueError("Port identity changed before final device query")
            final = hub.descriptor(1, 0, 18)
            require_target(final, vid, pid)
            if final != live: raise ValueError("Device descriptor changed during capture")
        except Exception as exc:
            result["errors"].append(f"Final identity check: {type(exc).__name__}: {exc}")
    complete = len(result["configurations"]) == device["bNumConfigurations"] and not result["errors"]
    result["configuration_coverage_complete"] = complete
    if complete:
        result["dfu_absent_from_all_advertised_configurations"] = not result["dfu_present_in_captured_configurations"]
    return result


def capture(vid=DEFAULT_VID, pid=DEFAULT_PID):
    physical_id_pattern(vid, pid)
    if os.name != "nt": raise RuntimeError("Hub descriptor capture requires Windows")
    check_layouts()
    record = {"format": "thy333.read-only-usb-hub-descriptors", "version": 1,
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "requested_identity": {"vid": vid, "pid": pid, "vid_pid": f"{vid:04x}:{pid:04x}"},
        "scope": "Standard DEVICE/CONFIGURATION descriptors only; no vendor/mode/firmware requests",
        "hub_access": "desiredAccess0; sharedREAD|WRITE; no write-access fallback",
        "hid_report_descriptor_requested": False, "errors": [],
        "configuration_coverage_complete": False,
        "dfu_absent_from_all_advertised_configurations": None}
    hub = None
    try:
        metadata = WindowsMetadata()
        target = metadata.target_port(vid, pid)
        path = metadata.hub_path(target)
        record["target_mapping"] = target
        record["hub_path"] = path
        hub = Hub(metadata, path, target["port"], vid, pid)
        record.update(capture_from_hub(hub, vid, pid))
    except Exception as exc: record["errors"].append(f"{type(exc).__name__}: {exc}")
    finally:
        if hub is not None:
            record["queries"] = hub.queries
            hub.close()
    return record


def write_capture(record):
    directory = (ROOT / "analysis/local/usb").resolve()
    if not directory.is_relative_to(ROOT): raise ValueError("Output directory escapes project")
    directory.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    path = directory / f"usb-descriptors-{stamp}-{uuid.uuid4().hex[:8]}.json"
    with path.open("x", encoding="utf-8") as stream:
        json.dump(record, stream, indent=2); stream.write("\n")
    return path


def usb_id_argument(value):
    try:
        if not re.fullmatch(r"(?:0[xX])?[0-9a-fA-F]{1,4}", value): raise ValueError
        return int(value, 16)
    except ValueError:
        raise argparse.ArgumentTypeError("Use a 1..4-digit hexadecimal USB ID, optionally prefixed with 0x") from None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-layouts", action="store_true", help="Offline ABI check; load no Windows/device APIs")
    parser.add_argument("--vid", type=usb_id_argument, help="Explicit hexadecimal vendor ID; requires --pid (default pair 0333:0333)")
    parser.add_argument("--pid", type=usb_id_argument, help="Explicit hexadecimal product ID; requires --vid")
    args = parser.parse_args()
    if (args.vid is None) != (args.pid is None): parser.error("Supply both --vid and --pid, or neither")
    vid, pid = (DEFAULT_VID, DEFAULT_PID) if args.vid is None else (args.vid, args.pid)
    if args.check_layouts:
        check_layouts(); print("Packed USB structures and read-only IOCTL constants passed; no device API loaded"); return 0
    record = capture(vid, pid); path = write_capture(record)
    print(f"Captured {len(record.get('configurations', []))} configurations; errors {len(record['errors'])}; {path}")
    print(f"Complete advertised-configuration coverage: {record['configuration_coverage_complete']}")
    print("No vendor, mode, reset, driver-change or firmware requests were sent.")
    return 2 if record["errors"] else 0


if __name__ == "__main__": raise SystemExit(main())
