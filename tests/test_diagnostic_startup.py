"""Execute the built reset routine in a small, fail-closed Thumb model.

This is an offline architectural regression, not a Cortex-M/peripheral emulator.
Only instructions used before the call to main are supported; an unknown opcode
fails instead of becoming a no-op. The tests inspect actual ELF load bytes.
"""
from __future__ import annotations

import os
from pathlib import Path
import re
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".tools/python-deps"))
sys.path.insert(0, str(ROOT / "tools"))
from inspect_diagnostic_elf import inspect_elf

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB
except ImportError:
    Cs = None

BUILD = Path(os.environ.get("THY_DIAG_STARTUP_BUILD", str(ROOT / "build/diagnostic-first-test-20261022-02")))
ELF = BUILD / "THY666-diagnostic.elf"
ALIASES = {"sb": "r9", "sl": "r10", "fp": "r11", "ip": "r12", "sp": "r13", "lr": "r14", "pc": "r15"}
RESET_SRAM_BYTES = 0x20000
RESET_FIXED_STEP_ALLOWANCE = 1024
# The supported startup copies each .data word in five instructions and clears
# each .bss word in four. Their combined storage cannot exceed the modeled
# 128 KiB SRAM. Keep a finite hardware-bounded budget, including conservative
# headroom for the fixed entry/snapshot sequence; this is not a timing limit.
RESET_STEP_LIMIT = RESET_FIXED_STEP_ALLOWANCE + 5 * (RESET_SRAM_BYTES // 4)


class ResetModel:
    def __init__(self, image, inspection, *, use_psp=True, ram_fill=0xa5):
        self.image, self.inspection = image, inspection
        self.registers = {f"r{i}": 0xdead0000 + i for i in range(16)}
        self.special = {"control": 2 if use_psp else 0, "ipsr": 0, "primask": 0,
                        "basepri": 0x90, "faultmask": 1, "msp": 0x2001a000, "psp": 0x20017000}
        self.incoming = dict(self.special)
        self.ram = bytearray([ram_fill] * RESET_SRAM_BYTES)
        self.mmio = {
            0xe000ed08: 0x08000000, 0xe000ed94: 5,
            0xe000ed88: 0x00f00000, 0xe000ef34: 0xc0000000,
            0xe000ed0c: 0xfa050700, 0xe000ed14: 0x109, 0xe000ed04: 0x04400000,
            0xe000ed24: 0x0000f000, 0xe000ed28: 0x02008200,
            0xe000ed2c: 0x40000000, 0x40023800: 0x03030003,
            0x40023808: 0x0000940a, 0x40023874: 0x34000000,
            0x40023840: 0x10200800, 0x4000300c: 0,
            0x40003004: 4, 0x40003008: 0xabc,
            0x40002c00: 0xff, 0x40002c04: 0x27f,
            0x40023830: 0x0010001f, 0x40023810: 0,
        }
        self.initial_mmio = dict(self.mmio)
        self.trace = []
        self.dma_asserted = self.dma_quiesced = False
        self.unsigned_ge = False
        self.steps = 0
        self.step_limit = RESET_STEP_LIMIT
        self.stop_target = None

    def reg(self, name):
        name = ALIASES.get(name, name)
        if name == "r15":
            return (self.pc + 4) & ~3
        if name == "r13":
            return self.special["psp" if self.special["control"] & 2 else "msp"]
        return self.registers[name]

    def set_reg(self, name, value):
        name, value = ALIASES.get(name, name), value & 0xffffffff
        if name == "r13":
            self.special["psp" if self.special["control"] & 2 else "msp"] = value
        else:
            self.registers[name] = value

    def value(self, operand):
        return int(operand[1:], 0) & 0xffffffff if operand.startswith("#") else self.reg(operand)

    def read(self, address):
        if address & 3:
            raise AssertionError(f"Unaligned modeled word read {address:#x}")
        if 0x20000000 <= address <= 0x2001fffc:
            return struct.unpack_from("<I", self.ram, address - 0x20000000)[0]
        if self.inspection["image_start"] <= address <= self.inspection["image_end"] - 4:
            return struct.unpack_from("<I", self.image, address - self.inspection["image_start"])[0]
        if address not in self.mmio:
            raise AssertionError(f"Unexpected modeled MMIO read {address:#x}")
        return self.mmio[address]

    def write(self, address, value):
        value &= 0xffffffff
        self.trace.append((self.pc, address, value))
        if address & 3:
            raise AssertionError(f"Unaligned modeled word write {address:#x}")
        if 0x20000000 <= address <= 0x2001fffc:
            if not self.dma_quiesced:
                raise AssertionError("Application RAM written before both DMA engines were reset")
            struct.pack_into("<I", self.ram, address - 0x20000000, value)
            return
        if address == 0x40023810:
            if value & 0x600000 == 0x600000:
                if self.mmio[0x40023830] & 0x600000 != 0x600000:
                    raise AssertionError("DMA reset asserted before DMA clocks were enabled")
                self.dma_asserted = True
            elif self.dma_asserted and value & 0x600000 == 0:
                self.dma_quiesced = True
        # Status clear registers have W1C semantics. NVIC and SysTick are
        # checked through the complete write trace rather than a bus model.
        if address in (0xe000ed28, 0xe000ed2c):
            self.mmio[address] &= ~value
        else:
            self.mmio[address] = value

    def memory_operand(self, operand):
        match = re.fullmatch(r"\[(\w+)(?:, (#-?0x[0-9a-f]+|#-?\d+))?\](?:, (#-?0x[0-9a-f]+|#-?\d+))?", operand)
        if not match:
            raise AssertionError(f"Unsupported memory operand {operand}")
        base, offset, post = match.groups()
        return (self.reg(base) + (self.value(offset) if offset else 0)) & 0xffffffff, base, post

    def execute(self, *, omit_dma_reset=False):
        decoder = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
        self.pc = self.inspection["entry"] & ~1
        while self.steps < self.step_limit:
            self.steps += 1
            off = self.pc - self.inspection["image_start"]
            decoded = list(decoder.disasm(self.image[off:off + 4], self.pc, count=1))
            if len(decoded) != 1:
                raise AssertionError(f"Cannot decode reset instruction at {self.pc:#x}")
            insn = decoded[0]
            op, text = insn.mnemonic.removesuffix(".w"), insn.op_str
            next_pc = self.pc + insn.size
            args = text.split(", ")
            if op in ("dsb", "isb", "nop"):
                pass  # Ordering is represented by sequential execution only.
            elif op == "cpsid":
                if text != "i":
                    raise AssertionError(f"Unsupported cpsid {text}")
                self.special["primask"] = 1
            elif op == "mrs":
                self.set_reg(args[0], self.special[args[1]])
            elif op == "msr":
                self.special[args[0]] = self.reg(args[1])
            elif op in ("mov", "movs", "movw"):
                self.set_reg(args[0], self.value(args[1]))
            elif op == "movt":
                self.set_reg(args[0], (self.reg(args[0]) & 0xffff) | self.value(args[1]) << 16)
            elif op == "mvn":
                self.set_reg(args[0], ~self.value(args[1]))
            elif op in ("orr", "bic"):
                lhs, rhs = self.reg(args[1]), self.value(args[2])
                self.set_reg(args[0], lhs | rhs if op == "orr" else lhs & ~rhs)
            elif op in ("ldr", "str"):
                target, operand = text.split(", ", 1)
                address, base, post = self.memory_operand(operand)
                if op == "ldr":
                    self.set_reg(target, self.read(address))
                elif not (omit_dma_reset and address == 0x40023810):
                    self.write(address, self.reg(target))
                if post:
                    self.set_reg(base, self.reg(base) + self.value(post))
            elif op in ("stm", "stmia"):
                base, reg_list = text.split(", ", 1)
                if not base.endswith("!") or not reg_list.startswith("{") or not reg_list.endswith("}"):
                    raise AssertionError(f"Unsupported stm {text}")
                base = base[:-1]
                for register in reg_list[1:-1].split(", "):
                    self.write(self.reg(base), self.reg(register))
                    self.set_reg(base, self.reg(base) + 4)
            elif op == "cmp":
                self.unsigned_ge = self.reg(args[0]) >= self.value(args[1])
            elif op in ("b", "bhs"):
                if op == "b" or self.unsigned_ge:
                    next_pc = self.value(text)
            elif op == "bl":
                self.stop_target = self.value(text)
                return
            else:
                raise AssertionError(f"Unsupported reset instruction at {self.pc:#x}: {op} {text}")
            self.pc = next_pc
        raise AssertionError(f"Reset routine exceeded bounded model step limit ({self.step_limit})")


@unittest.skipUnless(Cs is not None, "Local Capstone is unavailable")
class ResetModelBudgetTests(unittest.TestCase):
    def model(self, code):
        return ResetModel(code, {"entry": 0x08040001, "image_start": 0x08040000})

    def test_large_valid_bss_clear_exceeds_old_budget_and_finishes(self):
        # Actual Thumb bytes for the same four-instruction zero loop used by
        # startup.S: CMP; BHS exit; STR post-index; B loop; then BL. Set only
        # its input registers and the already-completed DMA precondition.
        model = self.model(bytes.fromhex("914202d241f8043bfae700f000f8"))
        size = 100 * 1024
        model.set_reg("r1", 0x20000000)
        model.set_reg("r2", 0x20000000 + size)
        model.set_reg("r3", 0)
        model.dma_asserted = model.dma_quiesced = True
        model.execute()
        self.assertEqual(model.steps, size + 3)
        self.assertGreater(model.steps, 100000)
        self.assertLess(model.steps, model.step_limit)
        self.assertIsNotNone(model.stop_target)
        self.assertEqual(model.ram[:size], bytes(size))
        self.assertEqual(model.ram[size:], bytes([0xa5]) * (RESET_SRAM_BYTES - size))

    def test_infinite_branch_still_exhausts_finite_budget(self):
        model = self.model(bytes.fromhex("fee7"))  # B to this same instruction.
        with self.assertRaisesRegex(AssertionError, "bounded model step limit"):
            model.execute()
        self.assertEqual(model.steps, RESET_STEP_LIMIT)
        self.assertIsNone(model.stop_target)

    def test_unknown_instruction_still_fails_immediately(self):
        model = self.model(bytes.fromhex("00be"))  # BKPT is outside the subset.
        with self.assertRaisesRegex(AssertionError, "Unsupported reset instruction"):
            model.execute()
        self.assertEqual(model.steps, 1)


@unittest.skipUnless(Cs is not None and ELF.exists(), "Prepared diagnostic ELF or local Capstone is unavailable")
class DiagnosticResetExecutionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.inspection, cls.image = inspect_elf(ELF.read_bytes())
        cls.symbols = cls.inspection["symbols"]

    def address(self, symbol):
        return self.symbols[symbol]["address"]

    def run_dirty(self, use_psp):
        model = ResetModel(self.image, self.inspection, use_psp=use_psp)
        model.execute()
        self.assertEqual(model.stop_target, self.address("main") & ~1)
        self.assertEqual(model.special["msp"], self.address("__stack_top__"))
        self.assertEqual(model.special["psp"], self.address("__stack_top__"))
        self.assertEqual({k: model.special[k] for k in ("control", "basepri", "faultmask", "primask")},
                         {"control": 0, "basepri": 0, "faultmask": 0, "primask": 1})
        self.assertEqual(model.mmio[0xe000ed94], 0)
        self.assertEqual(model.mmio[0xe000ed08], self.address("g_pfnVectors"))
        self.assertEqual(model.mmio[0xe000ed0c], 0x05fa0300)
        self.assertEqual(model.mmio[0xe000ed14], 0x210)
        self.assertEqual(model.mmio[0xe000ed28], 0)
        self.assertEqual(model.mmio[0xe000ed2c], 0)
        dstart, dend, load = map(self.address, ("__data_start__", "__data_end__", "__data_load__"))
        self.assertEqual(model.ram[dstart - 0x20000000:dend - 0x20000000],
                         self.image[load - self.inspection["image_start"]:load - self.inspection["image_start"] + dend - dstart])
        bstart, bend = map(self.address, ("__bss_start__", "__bss_end__"))
        self.assertEqual(model.ram[bstart - 0x20000000:bend - 0x20000000], bytes(bend - bstart))
        entry = self.address("g_diag_entry")
        size = self.symbols["g_diag_entry"]["bytes"]
        self.assertEqual(size, 112)
        self.assertGreaterEqual(entry, bend)
        expected = [0x5448454e, 1] + [model.incoming[k] for k in
                    ("control", "ipsr", "primask", "basepri", "faultmask", "msp", "psp")]
        expected += [model.initial_mmio[a] for a in (0xe000ed08, 0xe000ed94,
                     0xe000ed88, 0xe000ef34, 0xe000ed0c, 0xe000ed14, 0xe000ed04, 0xe000ed24,
                     0xe000ed28, 0xe000ed2c, 0x40023800, 0x40023808, 0x40023874,
                     0x40023840, 0x4000300c, 0x40003004, 0x40003008, 0x40002c00, 0x40002c04)]
        self.assertEqual(struct.unpack_from("<28I", model.ram, entry - 0x20000000), tuple(expected))
        writes = [(a, v) for _, a, v in model.trace]
        snapshot_writes = [(a, v) for a, v in writes if entry <= a < entry + size]
        self.assertEqual(len(snapshot_writes), 28)
        self.assertEqual(snapshot_writes[-1], (entry, 0x5448454e))
        self.assertLess(writes.index((entry, 0x5448454e)), writes.index((0xe000ed0c, 0x05fa0300)))
        self.assertLess(writes.index((entry, 0x5448454e)), writes.index((0xe000ed28, 0xffffffff)))
        for index in range(3):
            self.assertIn((0xe000e180 + index * 4, 0xffffffff), writes)
            self.assertIn((0xe000e280 + index * 4, 0xffffffff), writes)
        self.assertIn((0xe000ed04, 0x0a000000), writes)
        for address in (0xe000e010, 0xe000e014, 0xe000e018):
            self.assertIn((address, 0), writes)
        return model

    def test_dirty_psp_entry_snapshot_and_memory_initialization(self):
        self.run_dirty(True)

    def test_dirty_msp_entry_snapshot_and_memory_initialization(self):
        self.run_dirty(False)

    def test_missing_dma_quiescence_is_detected_before_first_ram_store(self):
        model = ResetModel(self.image, self.inspection)
        with self.assertRaisesRegex(AssertionError, "before both DMA engines were reset"):
            model.execute(omit_dma_reset=True)


if __name__ == "__main__":
    unittest.main()
