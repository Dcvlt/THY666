"""Receive-only transport deadlines/cancellation tests with no Windows APIs."""
import ast
import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import capture_hid_input as module


class Clock:
    def __init__(self): self.now = 0.0
    def __call__(self): return self.now


class Operation:
    def __init__(self, clock, waits, results, cancel_error=0):
        self.clock = clock
        self.waits = list(waits)
        self.results = list(results)
        self.cancel_error = cancel_error
        self.pending = False
        self.cancelled = False
        self.closed = False
        self.begun = False
        self.wait_budget = []

    def begin(self): self.pending = True; self.begun = True

    def wait(self, milliseconds):
        self.wait_budget.append(milliseconds)
        action = self.waits.pop(0)
        if isinstance(action, BaseException): raise action
        self.clock.now += 0.001 if action else milliseconds / 1000.0
        return action

    def result(self):
        action = self.results.pop(0)
        if isinstance(action, module.PendingRead): raise action
        self.pending = False
        if isinstance(action, BaseException): raise action
        return action

    def cancel(self): self.cancelled = True; return self.cancel_error

    def close(self):
        if self.pending: raise AssertionError("Test detected premature buffer release")
        self.closed = True


class Reader:
    def __init__(self, operations):
        self.operations = list(operations)
        self.created = 0
        self.retained = []

    def new_operation(self):
        self.created += 1
        return self.operations.pop(0)

    def retain_unsettled(self, operation): self.retained.append(operation)


class ReceiveTests(unittest.TestCase):
    def test_structure_layout_without_dll_loading(self): module.check_layouts()

    def test_wait_timeout_cancels_and_waits_for_abort_before_freeing(self):
        clock = Clock()
        operation = Operation(clock, [False, True], [module.CancelledRead()])
        result = module.collect_reports(Reader([operation]), clock)
        self.assertTrue(operation.cancelled and operation.closed)
        self.assertFalse(result["fatal_cleanup_needed"])
        self.assertEqual(result["reports"], [])
        self.assertTrue(result["cancellations"][0]["aborted"])
        self.assertLessEqual(clock.now, 2.001)

    def test_cancel_completion_race_preserves_the_received_report(self):
        clock = Clock()
        raw = b"\0" + b"FILCNTSN" + b"x" * 56
        operation = Operation(clock, [False, True], [raw], module.ERROR_NOT_FOUND)
        result = module.collect_reports(Reader([operation]), clock)
        self.assertEqual(result["reports"][0]["raw_hex"], raw.hex())
        self.assertEqual(result["reports"][0]["payload_hex"], raw[1:].hex())
        self.assertFalse(result["fatal_cleanup_needed"])
        self.assertEqual(result["cancellations"][0]["cancel_error"], 1168)
        self.assertTrue(operation.closed)

    def test_unsettled_cancellation_retains_kernel_resources(self):
        clock = Clock()
        operation = Operation(clock, [False, False], [module.PendingRead()])
        reader = Reader([operation])
        result = module.collect_reports(reader, clock)
        self.assertTrue(result["fatal_cleanup_needed"])
        self.assertEqual(reader.retained, [operation])
        self.assertFalse(operation.closed)
        self.assertTrue(operation.pending)
        self.assertLessEqual(clock.now, 2.001)

    def test_eight_report_hard_limit(self):
        clock = Clock()
        operations = [Operation(clock, [True], [b"\0" + bytes([i]) * 64]) for i in range(9)]
        reader = Reader(operations)
        result = module.collect_reports(reader, clock)
        self.assertEqual(len(result["reports"]), 8)
        self.assertEqual(reader.created, 8)
        self.assertFalse(operations[8].begun)

    def test_bad_report_framing_is_retained_without_protocol_normalization(self):
        for raw in (b"\0" + b"x" * 63, b"\x01" + b"x" * 64):
            clock = Clock()
            operation = Operation(clock, [True], [raw])
            result = module.collect_reports(Reader([operation]), clock)
            self.assertTrue(result["errors"])
            self.assertEqual(result["reports"][0]["raw_hex"], raw.hex())
            self.assertNotIn("payload_hex", result["reports"][0])

    def test_no_new_read_when_only_cancel_reserve_remains(self):
        clock = Clock()
        clock.now = 1.9
        operation = Operation(clock, [], [])
        raw, cancellation, fatal = module.bounded_read(operation, 2.0, clock)
        self.assertEqual((raw, cancellation, fatal), (None, None, False))
        self.assertFalse(operation.begun)

    def test_wait_error_still_settles_pending_read(self):
        clock = Clock()
        operation = Operation(clock, [OSError("event wait failed"), True], [module.CancelledRead()])
        result = module.collect_reports(Reader([operation]), clock)
        self.assertTrue(operation.cancelled and operation.closed)
        self.assertTrue(result["errors"])
        self.assertFalse(result["fatal_cleanup_needed"])

    def test_reader_does_not_close_a_handle_owning_pending_io(self):
        clock = Clock()
        operation = Operation(clock, [], [])
        operation.pending = True
        reader = module.WindowsReader.__new__(module.WindowsReader)
        reader.active = [operation]
        reader.retained = False
        reader.handle = 123
        reader.close_handle = mock.Mock()
        with mock.patch.object(module, "_UNSETTLED_GUARDS", []):
            self.assertFalse(reader.close())
            reader.close_handle.assert_not_called()
            self.assertEqual(module._UNSETTLED_GUARDS, [(reader, operation)])

    def test_cli_flushes_saved_fatal_capture_then_uses_process_exit(self):
        class ForcedExit(BaseException): pass
        record = {"reports": [], "errors": ["unsettled"], "fatal_cleanup_needed": True}
        with mock.patch.object(module, "capture_input", return_value=record), \
             mock.patch.object(module, "write_capture", return_value=Path("capture.json")) as save, \
             mock.patch.object(module.os, "_exit", side_effect=ForcedExit) as terminate, \
             mock.patch("sys.argv", ["capture_hid_input.py"]), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(ForcedExit): module.main()
        save.assert_called_once_with(record)
        terminate.assert_called_once_with(2)

    def test_only_receive_and_metadata_api_bindings(self):
        tree = ast.parse((ROOT / "tools/capture_hid_input.py").read_text())
        actual = {node.args[1].value for node in ast.walk(tree)
                  if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                  and node.func.id == "_bind"}
        self.assertEqual(actual, {"HidD_GetHidGuid", "SetupDiGetClassDevsW",
            "SetupDiEnumDeviceInterfaces", "SetupDiGetDeviceInterfaceDetailW",
            "SetupDiGetDeviceRegistryPropertyW", "SetupDiDestroyDeviceInfoList",
            "CreateFileW", "CloseHandle", "ReadFile", "GetOverlappedResult",
            "CancelIoEx", "CreateEventW", "WaitForSingleObject",
            "HidD_GetAttributes", "HidD_GetPreparsedData", "HidD_FreePreparsedData", "HidP_GetCaps"})


if __name__ == "__main__": unittest.main()
