"""Execute the target reset bytes to check stack watermark isolation, offline."""
import os
from pathlib import Path
import struct
import unittest
from test_diagnostic_startup import Cs, ResetModel, inspect_elf

BUILD = os.environ.get("THY_RESOURCE_TEST_BUILD")

@unittest.skipUnless(Cs is not None and BUILD, "Set THY_RESOURCE_TEST_BUILD to a prepared instrument build; local Capstone required")
class InstrumentResourceTests(unittest.TestCase):
    def test_reserved_stack_only_is_painted_before_main(self):
        report, image = inspect_elf((Path(BUILD) / "THY666-instrument-alpha.elf").read_bytes())
        symbols = report["symbols"]
        addr = lambda key: symbols[key]["address"]
        low, high = addr("__stack_limit__"), addr("__stack_top__")
        self.assertEqual(high - low, 8192)
        self.assertLessEqual(addr("__ram_end__"), low)
        self.assertEqual(addr("__flash_used_end__") - report["image_start"], len(image))
        for psp in (False, True):
            model = ResetModel(image, report, use_psp=psp)
            model.execute()
            self.assertEqual(model.stop_target, addr("main") & ~1)
            self.assertEqual(model.special["primask"], 1)
            self.assertTrue(model.dma_quiesced)
            self.assertEqual(model.ram[low - 0x20000000:high - 0x20000000], struct.pack("<I", 0xa55aa55a) * 2048)
            gap = model.ram[addr("__ram_end__") - 0x20000000:low - 0x20000000]
            self.assertEqual(gap, bytes([0xa5]) * len(gap))
            stack_writes = [(a, v) for _, a, v in model.trace if low <= a < high]
            self.assertEqual(len(stack_writes), 2048)
            self.assertTrue(all(v == 0xa55aa55a for _, v in stack_writes))

if __name__ == "__main__":
    unittest.main()
