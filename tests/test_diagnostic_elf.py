import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("diagnostic_elf", ROOT / "tools/inspect_diagnostic_elf.py")
elf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(elf)


def synthetic_elf():
    entry = elf.APP_START + 0x201
    result = bytearray(0x410)
    result[:16] = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    struct.pack_into("<HHIIIIIHHHHHH", result, 16, 2, 40, 1, entry, 52, 0, 0x05000200, 52, 32, 2, 40, 0, 0)
    struct.pack_into("<IIIIIIII", result, 52, 1, 0x100, elf.APP_START, elf.APP_START, 0x300, 0x300, 5, 0x100)
    struct.pack_into("<IIIIIIII", result, 84, 1, 0x400, elf.RAM_START, elf.APP_START + 0x300, 16, 256, 6, 0x100)
    vectors = [elf.RAM_END] + [entry] * 97
    for index in elf.RESERVED_VECTORS:
        vectors[index] = 0
    struct.pack_into("<98I", result, 0x100, *vectors)
    result[0x400:] = b"\x12" * 16
    return result


def synthetic_ram_elf(origin):
    data = synthetic_elf()
    entry = origin + 0x201
    struct.pack_into("<I", data, 24, entry)
    struct.pack_into("<II", data, 60, origin, origin)
    struct.pack_into("<II", data, 92, origin + 0x400, origin + 0x300)
    for index in range(1, 98):
        if index not in elf.RESERVED_VECTORS:
            struct.pack_into("<I", data, 0x100 + index * 4, entry)
    return data


class DiagnosticELFTests(unittest.TestCase):
    def test_rom_dfu_reader_keeps_boot_rom_ram_clear_and_has_distinct_origin(self):
        data = synthetic_ram_elf(elf.ROM_DFU_RAM_START)
        report, binary = elf.inspect_elf(bytes(data), execution="rom-dfu-ram")
        self.assertEqual(report["image_start"], 0x20004000)
        self.assertEqual(report["reserved_rom_ram_bytes"], 16384)
        self.assertEqual(report["memory_bytes"]["application_flash"], 0)
        self.assertEqual(len(binary), 0x310)
        self.assertFalse(report["device_execution_ready"])
        for mode in ("flash", "ram"):
            with self.assertRaises(ValueError):
                elf.inspect_elf(bytes(data), execution=mode)
        with self.assertRaises(ValueError):
            elf.inspect_elf(bytes(synthetic_ram_elf(elf.RAM_START)), execution="rom-dfu-ram")

    def test_rom_dfu_rejects_initializers_bss_and_stack_inside_rom_reservation(self):
        for offset in (92, 96, 0x100):
            broken = synthetic_ram_elf(elf.ROM_DFU_RAM_START)
            struct.pack_into("<I", broken, offset, elf.RAM_START + 0x1000)
            with self.assertRaisesRegex(ValueError, "reserved|SRAM"):
                elf.inspect_elf(bytes(broken), execution="rom-dfu-ram")
        # A zero-file-size BSS segment must obey the reservation too.
        broken = synthetic_ram_elf(elf.ROM_DFU_RAM_START)
        struct.pack_into("<II", broken, 92, elf.RAM_START + 0x1000, elf.RAM_START + 0x1000)
        struct.pack_into("<I", broken, 100, 0)
        with self.assertRaisesRegex(ValueError, "reserved"):
            elf.inspect_elf(bytes(broken), execution="rom-dfu-ram")

    def test_ram_reader_loads_are_confined_to_sram_and_cannot_be_mislabeled_flash(self):
        data = synthetic_elf()
        entry = elf.RAM_START + 0x201
        struct.pack_into("<I", data, 24, entry)
        struct.pack_into("<I", data, 60, elf.RAM_START)
        struct.pack_into("<I", data, 64, elf.RAM_START)
        struct.pack_into("<I", data, 92, elf.RAM_START + 0x400)
        struct.pack_into("<I", data, 96, elf.RAM_START + 0x300)
        for index in range(1, 98):
            if index not in elf.RESERVED_VECTORS:
                struct.pack_into("<I", data, 0x100 + index * 4, entry)
        report, binary = elf.inspect_elf(bytes(data), execution="ram")
        self.assertEqual(report["image_start"], elf.RAM_START)
        self.assertEqual(report["execution_mode"], "ram")
        self.assertEqual(report["memory_bytes"]["application_flash"], 0)
        self.assertFalse(report["device_execution_ready"])
        self.assertEqual(len(binary), 0x310)
        with self.assertRaises(ValueError):
            elf.inspect_elf(bytes(data))
        struct.pack_into("<I", data, 96, elf.APP_START)
        with self.assertRaisesRegex(ValueError, "load region"):
            elf.inspect_elf(bytes(data), execution="ram")

    def test_extracts_data_load_address_and_retains_ram_budget(self):
        report, binary = elf.inspect_elf(bytes(synthetic_elf()))
        self.assertEqual(len(binary), 0x310)
        self.assertEqual(binary[0x300:], b"\x12" * 16)
        self.assertEqual(report["memory_bytes"]["sram"], 256)
        self.assertEqual(report["initial_stack"], elf.RAM_END)
        self.assertFalse(report["flash_ready"])

    def test_rejects_truncated_or_wrong_architecture_inputs(self):
        for length in (0, 51, 70, 0x30f):
            with self.assertRaises(ValueError):
                elf.inspect_elf(bytes(synthetic_elf()[:length]))
        wrong = synthetic_elf()
        struct.pack_into("<H", wrong, 18, 3)
        with self.assertRaisesRegex(ValueError, "ARM"):
            elf.inspect_elf(bytes(wrong))

    def test_rejects_bootloader_writes_and_ram_overflow(self):
        for offset, value in ((60, 0x08000000), (64, 0x08000000), (92, elf.RAM_END - 128)):
            broken = synthetic_elf()
            struct.pack_into("<I", broken, offset, value)
            with self.assertRaises(ValueError):
                elf.inspect_elf(bytes(broken))

    def test_rejects_bad_stack_reset_reserved_and_outside_handlers(self):
        for index, value in ((0, elf.RAM_END + 8), (0, elf.RAM_END - 4), (1, elf.APP_START + 0x203),
                             (7, elf.APP_START + 0x201), (51, elf.APP_END + 1), (3, elf.APP_START + 0x200)):
            broken = synthetic_elf()
            struct.pack_into("<I", broken, 0x100 + index * 4, value)
            with self.assertRaises(ValueError):
                elf.inspect_elf(bytes(broken))

    def test_rejects_overlapping_loads_and_file_bounds(self):
        for offset, value in ((96, elf.APP_START + 0x200), (100, 17), (104, 8), (108, 7), (112, 7)):
            broken = synthetic_elf()
            struct.pack_into("<I", broken, offset, value)
            with self.assertRaises(ValueError):
                elf.inspect_elf(bytes(broken))

    def test_handlers_cannot_point_to_data_load_bytes_or_nonexecutable_flash(self):
        for handler in (elf.APP_START + 0x301, elf.APP_START + 0x315):
            broken = synthetic_elf()
            struct.pack_into("<I", broken, 0x100 + 3 * 4, handler)
            with self.assertRaisesRegex(ValueError, "executable"):
                elf.inspect_elf(bytes(broken))
        broken = synthetic_elf()
        struct.pack_into("<I", broken, 52 + 6 * 4, 4)
        with self.assertRaisesRegex(ValueError, "executable"):
            elf.inspect_elf(bytes(broken))


if __name__ == "__main__":
    unittest.main()
