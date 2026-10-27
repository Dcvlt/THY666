"""Receive up to eight existing TheoryBoard HID input reports for two seconds.

Strictly receive-only: GENERIC_READ, ReadFile, bounded overlapped waits and
CancelIoEx. No OUT reports, vendor commands, feature/input-report queries,
buffer configuration, reset or mode transition. Windows DLLs are loaded only
for an explicit capture, not --check-layouts or an import/offline test.

Primary references:
https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/obtaining-hid-reports
https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile
https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex
"""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import sys
import time
import uuid

from capture_hid_inventory import (GUID, SP_DEVICE_INTERFACE_DATA, SP_DEVINFO_DATA,
    HIDD_ATTRIBUTES, HIDP_CAPS, _bind, decode_multi_sz, matches_target,
    validate_layouts, ROOT, MAX_BUFFER, MAX_INTERFACES, ERROR_INSUFFICIENT_BUFFER,
    ERROR_NO_MORE_ITEMS, REG_MULTI_SZ, HIDP_STATUS_SUCCESS)

REPORT_BYTES = 65
MAX_REPORTS = 8
CAPTURE_SECONDS = 2.0
CANCEL_RESERVE_SECONDS = 0.1
ERROR_IO_PENDING = 997
ERROR_IO_INCOMPLETE = 996
ERROR_OPERATION_ABORTED = 995
ERROR_NOT_FOUND = 1168
WAIT_OBJECT_0 = 0
WAIT_TIMEOUT = 258
_UNSETTLED_GUARDS = []  # Never freed normally while an IRP can still access them.


class OVERLAPPED(ctypes.Structure):
    _fields_ = [("Internal", ctypes.c_size_t), ("InternalHigh", ctypes.c_size_t),
                ("Offset", ctypes.c_uint32), ("OffsetHigh", ctypes.c_uint32),
                ("hEvent", ctypes.c_void_p)]


class PendingRead(Exception):
    pass


class CancelledRead(Exception):
    pass


def check_layouts():
    validate_layouts()
    expected = 32 if ctypes.sizeof(ctypes.c_void_p) == 8 else 20
    if ctypes.sizeof(OVERLAPPED) != expected:
        raise ValueError("OVERLAPPED ctypes layout mismatch")


def remaining_ms(deadline, clock):
    return max(0, int(math.ceil((deadline - clock()) * 1000.0)))


def bounded_read(operation, deadline, clock=time.monotonic):
    """Return (raw-or-None, cancellation-record, fatal-unsettled).

    Cancellation is a request, not proof that the kernel finished using the
    buffer. The same operation is never reused or freed before result() confirms
    success/abort. Tests inject a fake transport and clock without device APIs.
    """
    capture_deadline = deadline - CANCEL_RESERVE_SECONDS
    if clock() >= capture_deadline:
        return None, None, False
    operation.begin()
    if operation.wait(remaining_ms(capture_deadline, clock)):
        try:
            return operation.result(), None, False
        except CancelledRead:
            return None, {"requested": False, "completed": True, "aborted": True}, False
        except PendingRead:
            # Event notification alone is insufficient; confirm with GetResult.
            pass
    cancel_error = operation.cancel()
    operation.wait(remaining_ms(deadline, clock))
    try:
        # A cancellation race may return a completed report rather than ABORTED.
        raw = operation.result()
        return raw, {"requested": True, "cancel_error": cancel_error,
                     "completed": True, "aborted": False}, False
    except CancelledRead:
        return None, {"requested": True, "cancel_error": cancel_error,
                      "completed": True, "aborted": True}, False
    except PendingRead:
        return None, {"requested": True, "cancel_error": cancel_error,
                      "completed": False, "aborted": False}, True


def collect_reports(reader, clock=time.monotonic):
    start = clock()
    deadline = start + CAPTURE_SECONDS
    reports = []
    cancellations = []
    fatal = False
    errors = []
    for _ in range(MAX_REPORTS):
        if clock() >= deadline - CANCEL_RESERVE_SECONDS:
            break
        operation = reader.new_operation()
        try:
            raw, cancellation, unsettled = bounded_read(operation, deadline, clock)
            if cancellation is not None:
                cancellations.append(cancellation)
            if unsettled:
                reader.retain_unsettled(operation)
                fatal = True
                errors.append("Pending read did not settle; resources retained until forced process exit")
                break
            operation.close()
            if raw is None:
                break
            item = {"elapsed_ms": round((clock() - start) * 1000, 3),
                    "bytes": len(raw), "raw_hex": raw.hex(),
                    "report_id": raw[0] if raw else None,
                    "unnumbered_65_byte_report": len(raw) == REPORT_BYTES and raw[0] == 0}
            if item["unnumbered_65_byte_report"]:
                item["payload_hex"] = raw[1:].hex()
            else:
                errors.append("Input does not match expected zero-ID 65-byte Windows HID framing")
            reports.append(item)
            if not item["unnumbered_65_byte_report"]:
                break
            if cancellation is not None:
                break
        except Exception as exc:
            # Unexpected errors must still settle any submitted IRP before a
            # buffer/event is released. A driver error is not a completed read.
            errors.append(f"{type(exc).__name__}: {exc}")
            if operation.pending:
                operation.cancel()
                operation.wait(remaining_ms(deadline, clock))
                try:
                    operation.result()
                except CancelledRead:
                    pass
                except PendingRead:
                    reader.retain_unsettled(operation)
                    fatal = True
                    break
                except OSError:
                    # A completed operation can finish with a non-abort error.
                    if operation.pending:
                        reader.retain_unsettled(operation)
                        fatal = True
                        break
            operation.close()
            break
    return {"reports": reports, "cancellations": cancellations, "errors": errors,
            "elapsed_ms": round((clock() - start) * 1000, 3),
            "fatal_cleanup_needed": fatal}


def enumerate_targets(setup, hid):
    """SetupAPI metadata enumeration; no device handles and no path parsing."""
    p_guid = ctypes.POINTER(GUID)
    p_device = ctypes.POINTER(SP_DEVINFO_DATA)
    p_interface = ctypes.POINTER(SP_DEVICE_INTERFACE_DATA)
    p_dword = ctypes.POINTER(wintypes.DWORD)
    get_guid = _bind(hid, "HidD_GetHidGuid", [p_guid], None)
    class_devs = _bind(setup, "SetupDiGetClassDevsW",
                      [p_guid, wintypes.LPCWSTR, wintypes.HWND, wintypes.DWORD], wintypes.HANDLE)
    enum = _bind(setup, "SetupDiEnumDeviceInterfaces",
                 [wintypes.HANDLE, p_device, p_guid, wintypes.DWORD, p_interface], wintypes.BOOL)
    detail_api = _bind(setup, "SetupDiGetDeviceInterfaceDetailW",
                      [wintypes.HANDLE, p_interface, ctypes.c_void_p, wintypes.DWORD, p_dword, p_device], wintypes.BOOL)
    property_api = _bind(setup, "SetupDiGetDeviceRegistryPropertyW",
                        [wintypes.HANDLE, p_device, wintypes.DWORD, p_dword,
                         ctypes.c_void_p, wintypes.DWORD, p_dword], wintypes.BOOL)
    destroy = _bind(setup, "SetupDiDestroyDeviceInfoList", [wintypes.HANDLE], wintypes.BOOL)
    guid = GUID()
    get_guid(ctypes.byref(guid))
    devices = class_devs(ctypes.byref(guid), None, None, 0x12)
    if devices == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    matches = []
    try:
        for index in range(MAX_INTERFACES):
            interface = SP_DEVICE_INTERFACE_DATA()
            interface.cbSize = ctypes.sizeof(interface)
            if not enum(devices, None, ctypes.byref(guid), index, ctypes.byref(interface)):
                code = ctypes.get_last_error()
                if code == ERROR_NO_MORE_ITEMS:
                    break
                raise ctypes.WinError(code)
            size = wintypes.DWORD()
            first = detail_api(devices, ctypes.byref(interface), None, 0, ctypes.byref(size), None)
            if first or ctypes.get_last_error() != ERROR_INSUFFICIENT_BUFFER or not 6 <= size.value <= MAX_BUFFER:
                raise ValueError("Invalid interface-detail size query")
            detail = ctypes.create_string_buffer(size.value)
            ctypes.c_uint32.from_buffer(detail).value = 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6
            device = SP_DEVINFO_DATA()
            device.cbSize = ctypes.sizeof(device)
            if not detail_api(devices, ctypes.byref(interface), detail, size.value, ctypes.byref(size), ctypes.byref(device)):
                raise ctypes.WinError(ctypes.get_last_error())
            property_type = wintypes.DWORD()
            ids_size = wintypes.DWORD()
            first = property_api(devices, ctypes.byref(device), 1, ctypes.byref(property_type), None, 0, ctypes.byref(ids_size))
            if first or ctypes.get_last_error() != ERROR_INSUFFICIENT_BUFFER or not 4 <= ids_size.value <= MAX_BUFFER:
                raise ValueError("Invalid cached hardware-ID size query")
            ids = ctypes.create_string_buffer(ids_size.value)
            if not property_api(devices, ctypes.byref(device), 1, ctypes.byref(property_type),
                                ids, ids_size.value, ctypes.byref(ids_size)):
                raise ctypes.WinError(ctypes.get_last_error())
            if property_type.value != REG_MULTI_SZ:
                raise ValueError("Cached hardware IDs are not MULTI_SZ")
            hardware_ids = decode_multi_sz(ids.raw[:ids_size.value])
            if not matches_target(hardware_ids):
                continue
            raw_path = detail.raw[4:size.value].decode("utf-16-le", errors="strict")
            if "\0" not in raw_path:
                raise ValueError("Unterminated DevicePath")
            matches.append({"hardware_ids": hardware_ids, "device_path": raw_path.split("\0", 1)[0]})
        else:
            raise ValueError("HID enumeration exceeded its finite bound")
    finally:
        destroy(devices)
    return matches


class WindowsReadOperation:
    def __init__(self, reader):
        self.reader = reader
        self.buffer = ctypes.create_string_buffer(REPORT_BYTES)
        self.overlapped = OVERLAPPED()
        self.event = reader.create_event(None, True, False, None)
        if not self.event:
            raise ctypes.WinError(ctypes.get_last_error())
        self.overlapped.hEvent = self.event
        self.pending = False

    def begin(self):
        self.pending = True
        ok = self.reader.read_file(self.reader.handle, self.buffer, REPORT_BYTES,
                                   None, ctypes.byref(self.overlapped))
        if not ok and ctypes.get_last_error() != ERROR_IO_PENDING:
            self.pending = False
            raise ctypes.WinError(ctypes.get_last_error())

    def wait(self, milliseconds):
        result = self.reader.wait_event(self.event, milliseconds)
        if result == WAIT_OBJECT_0:
            return True
        if result == WAIT_TIMEOUT:
            return False
        raise ctypes.WinError(ctypes.get_last_error())

    def result(self):
        count = wintypes.DWORD()
        if not self.reader.get_result(self.reader.handle, ctypes.byref(self.overlapped), ctypes.byref(count), False):
            code = ctypes.get_last_error()
            if code == ERROR_IO_INCOMPLETE:
                raise PendingRead()
            self.pending = False
            if code == ERROR_OPERATION_ABORTED:
                raise CancelledRead()
            raise ctypes.WinError(code)
        self.pending = False
        if count.value > REPORT_BYTES:
            raise ValueError("Driver byte count exceeds input buffer")
        return self.buffer.raw[:count.value]

    def cancel(self):
        if self.reader.cancel_io(self.reader.handle, ctypes.byref(self.overlapped)):
            return 0
        return ctypes.get_last_error()  # ERROR_NOT_FOUND can be a completion race.

    def close(self):
        if self.pending:
            raise RuntimeError("Cannot release an unsettled read operation")
        self.reader.close_handle(self.event)
        self.reader.active.remove(self)


class WindowsReader:
    def __init__(self, kernel, hid, target):
        handle = wintypes.HANDLE
        p_overlapped = ctypes.POINTER(OVERLAPPED)
        self.close_handle = _bind(kernel, "CloseHandle", [handle], wintypes.BOOL)
        create_file = _bind(kernel, "CreateFileW",
                           [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                            wintypes.DWORD, wintypes.DWORD, handle], handle)
        self.read_file = _bind(kernel, "ReadFile",
                              [handle, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), p_overlapped], wintypes.BOOL)
        self.get_result = _bind(kernel, "GetOverlappedResult",
                               [handle, p_overlapped, ctypes.POINTER(wintypes.DWORD), wintypes.BOOL], wintypes.BOOL)
        self.cancel_io = _bind(kernel, "CancelIoEx", [handle, p_overlapped], wintypes.BOOL)
        self.create_event = _bind(kernel, "CreateEventW",
                                 [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR], handle)
        self.wait_event = _bind(kernel, "WaitForSingleObject", [handle, wintypes.DWORD], wintypes.DWORD)
        self.handle = create_file(target["device_path"], 0x80000000, 3, None, 3, 0x40000000, None)
        if self.handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        self.retained = False
        self.active = []
        try:
            get_attributes = _bind(hid, "HidD_GetAttributes", [handle, ctypes.POINTER(HIDD_ATTRIBUTES)], ctypes.c_ubyte)
            get_preparsed = _bind(hid, "HidD_GetPreparsedData", [handle, ctypes.POINTER(ctypes.c_void_p)], ctypes.c_ubyte)
            free_preparsed = _bind(hid, "HidD_FreePreparsedData", [ctypes.c_void_p], ctypes.c_ubyte)
            get_caps = _bind(hid, "HidP_GetCaps", [ctypes.c_void_p, ctypes.POINTER(HIDP_CAPS)], ctypes.c_int32)
            attrs = HIDD_ATTRIBUTES()
            attrs.Size = ctypes.sizeof(attrs)
            if not get_attributes(self.handle, ctypes.byref(attrs)):
                raise ctypes.WinError(ctypes.get_last_error())
            if (attrs.VendorID, attrs.ProductID) != (0x0333, 0x0333):
                raise ValueError("Live attributes do not match target0333:0333")
            preparsed = ctypes.c_void_p()
            if not get_preparsed(self.handle, ctypes.byref(preparsed)):
                raise ctypes.WinError(ctypes.get_last_error())
            try:
                caps = HIDP_CAPS()
                if get_caps(preparsed, ctypes.byref(caps)) != HIDP_STATUS_SUCCESS:
                    raise ValueError("HID capabilities query failed")
            finally:
                free_preparsed(preparsed)
            if (caps.UsagePage, caps.Usage, caps.InputReportByteLength) != (0xffab, 0x0200, REPORT_BYTES):
                raise ValueError("Target HID usage/input length differ from FFAB:0200 /65")
            self.identity = {**target, "vid": "0333", "pid": "0333",
                "version_number": f"{attrs.VersionNumber:04x}", "usage_page": caps.UsagePage,
                "usage": caps.Usage, "input_report_bytes": caps.InputReportByteLength}
        except BaseException:
            self.close_handle(self.handle)
            raise

    def new_operation(self):
        operation = WindowsReadOperation(self)
        self.active.append(operation)
        return operation

    def retain_unsettled(self, operation):
        self.retained = True
        if not any(reader is self and existing is operation for reader, existing in _UNSETTLED_GUARDS):
            _UNSETTLED_GUARDS.append((self, operation))

    def close(self):
        # Even an unexpected cancellation/wait exception must not release a
        # submitted buffer. Propagate fatal cleanup to the process-level caller.
        for operation in self.active:
            if operation.pending:
                self.retain_unsettled(operation)
        if not self.retained:
            self.close_handle(self.handle)
        return not self.retained


def capture_input():
    if os.name != "nt":
        raise RuntimeError("Receive-only HID capture requires Windows")
    check_layouts()
    record = {"format": "thy333.receive-only-hid-input", "version": 1,
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "scope": "Existing IN reports only; no command, firmware, mode or configuration changes",
        "out_reports_sent": 0, "vendor_commands_sent": 0, "feature_requests": 0,
        "maximum_reports": MAX_REPORTS, "capture_seconds": CAPTURE_SECONDS,
        "cancellation_reserve_seconds": CANCEL_RESERVE_SECONDS,
        "reports": [], "errors": [], "fatal_cleanup_needed": False}
    readers = []
    try:
        setup = ctypes.WinDLL("setupapi", use_last_error=True)
        hid = ctypes.WinDLL("hid", use_last_error=True)
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        targets = enumerate_targets(setup, hid)
        if len(targets) != 1:
            raise ValueError(f"Expected exactly one cached-ID target HID interface, found {len(targets)}")
        reader = WindowsReader(kernel, hid, targets[0])
        readers.append(reader)
        record["identity"] = reader.identity
        record.update(collect_reports(reader))
    except BaseException as exc:
        # Ctrl+C must also retain a submitted buffer until forced process exit;
        # propagating KeyboardInterrupt into normal finalization is unsafe.
        record["errors"].append(f"{type(exc).__name__}: {exc}")
    finally:
        for reader in readers:
            if not reader.close():
                record["fatal_cleanup_needed"] = True
                if not record["errors"]:
                    record["errors"].append("Unsettled kernel read requires forced process exit")
    return record


def write_capture(record):
    directory = (ROOT / "analysis/local/usb").resolve()
    if not directory.is_relative_to(ROOT):
        raise ValueError("Capture output escapes the project")
    directory.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    path = directory / f"hid-input-{stamp}-{uuid.uuid4().hex[:8]}.json"
    with path.open("x", encoding="utf-8") as stream:
        json.dump(record, stream, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-layouts", action="store_true", help="Offline ABI check only; no device API")
    args = parser.parse_args()
    if args.check_layouts:
        check_layouts()
        print("Receive-only ctypes layouts passed; no device API was loaded")
        return 0
    record = capture_input()
    try:
        output = write_capture(record)
    except BaseException as exc:
        if record["fatal_cleanup_needed"]:
            # The failure path must also avoid Python finalizing an IRP buffer.
            sys.stderr.write(f"Could not persist fatal capture: {type(exc).__name__}: {exc}\n")
            sys.stderr.flush()
            os._exit(2)
        raise
    print(f"Received {len(record['reports'])} reports; errors {len(record['errors'])}; capture: {output}")
    print("No OUT/vendor/feature/configuration requests were sent. Empty input is not backup or recovery evidence.")
    if record["fatal_cleanup_needed"]:
        # Normal Python finalization could release retained buffers while the
        # driver still owns an IRP. Preserve references until the OS terminates
        # this process. The capture has already been closed, flushed and fsynced.
        sys.stdout.flush()
        sys.stderr.flush()
        os._exit(2)
    return 2 if record["errors"] else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    finally:
        # Also cover Ctrl+C between capture return, file persistence and printing.
        # Do not allow ordinary interpreter cleanup to release an IRP buffer.
        if _UNSETTLED_GUARDS:
            os._exit(2)
