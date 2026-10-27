"""Settings reservation must hold in ELF load allocations, not just names."""
import copy
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from review_instrument_build import APP_START, SETTINGS_START, SETTINGS_END, review_settings_partition


def inspection():
    return {
        "image_start": APP_START, "image_end": SETTINGS_START,
        "symbols": {"__settings_start__": {"address": SETTINGS_START},
                    "__settings_end__": {"address": SETTINGS_END}},
        "segments": [
            {"region": "application_flash", "virtual_address": APP_START,
             "load_address": APP_START, "file_bytes": 0x7FFF0, "memory_bytes": 0x7FFF0},
            {"region": "sram", "virtual_address": 0x20000000,
             "load_address": SETTINGS_START - 16, "file_bytes": 16, "memory_bytes": 256},
        ],
    }


class InstrumentSettingsPartitionTests(unittest.TestCase):
    def test_exact_application_boundary_and_ram_initializer_are_allowed(self):
        result = review_settings_partition(inspection())
        self.assertEqual(result["reserved_bytes"], 256 * 1024)
        self.assertEqual(result["application_capacity_bytes"], 512 * 1024)
        self.assertTrue(result["all_flash_allocations_and_initializers_outside_settings"])

    def test_missing_moved_or_forged_settings_symbols_fail_closed(self):
        for symbol in ("__settings_start__", "__settings_end__"):
            for value in (None, 0, APP_START, SETTINGS_START + 4, SETTINGS_END - 4):
                bad = inspection()
                if value is None:
                    del bad["symbols"][symbol]
                else:
                    bad["symbols"][symbol]["address"] = value
                with self.subTest(symbol=symbol, value=value), self.assertRaisesRegex(ValueError, "settings (start|end)"):
                    review_settings_partition(bad)

    def test_full_image_and_data_load_cannot_overlap_journal(self):
        bad = inspection(); bad["image_end"] = SETTINGS_START + 1
        with self.assertRaisesRegex(ValueError, "image overlaps"):
            review_settings_partition(bad)
        bad = inspection(); bad["segments"][1]["load_address"] += 1
        with self.assertRaisesRegex(ValueError, "initialized load"):
            review_settings_partition(bad)

    def test_flash_memory_extent_is_checked_even_when_file_extent_fits(self):
        bad = inspection()
        bad["segments"][0]["memory_bytes"] = SETTINGS_START - APP_START + 1
        with self.assertRaisesRegex(ValueError, "flash allocation"):
            review_settings_partition(bad)
        bad = inspection()
        bad["segments"].append({"region": "application_flash", "virtual_address": SETTINGS_START,
                                "load_address": SETTINGS_START, "file_bytes": 0, "memory_bytes": 4})
        with self.assertRaisesRegex(ValueError, "flash allocation"):
            review_settings_partition(bad)

    def test_bootloader_and_wrong_image_origins_remain_forbidden(self):
        for field, value in (("image_start", 0x08000000), ("image_end", APP_START)):
            bad = copy.deepcopy(inspection()); bad[field] = value
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "image overlaps"):
                review_settings_partition(bad)
        bad = inspection(); bad["segments"][0]["virtual_address"] = 0x08000000
        with self.assertRaisesRegex(ValueError, "flash allocation"):
            review_settings_partition(bad)


if __name__ == "__main__":
    unittest.main()
