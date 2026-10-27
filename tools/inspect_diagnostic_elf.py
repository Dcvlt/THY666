"""Bounded ELF32/ARM inspection and flat-image extraction; no device access."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

APP_START = 0x08040000
APP_END = 0x08100000
RAM_START = 0x20000000
RAM_END = 0x20020000
# AN2606 F405/407 ROM versions use the first 8 or 12 KiB of SRAM. Leave 16 KiB
# untouched while a ROM DFU transfer is in progress. Reachability is unverified.
ROM_DFU_RAM_START = 0x20004000
CCM_START = 0x10000000
CCM_END = 0x10010000
VECTOR_WORDS = 98
RESERVED_VECTORS = {7, 8, 9, 10, 13, 77, 78, 94, 95}


def slice_checked(data: bytes, offset: int, length: int) -> bytes:
    if offset < 0 or length < 0 or offset > len(data) or length > len(data) - offset:
        raise ValueError("ELF field extends beyond file")
    return data[offset:offset + length]


def region_for(address: int, size: int) -> str:
    for label, start, end in (("application_flash", APP_START, APP_END),
                               ("sram", RAM_START, RAM_END), ("ccm", CCM_START, CCM_END)):
        if start <= address <= end and size <= end - address:
            return label
    raise ValueError(f"Load region outside candidate memory: 0x{address:08x} + {size}")


def inspect_elf(data: bytes, *, execution: str = "flash") -> tuple[dict, bytes]:
    if execution not in ("flash", "ram", "rom-dfu-ram"):
        raise ValueError("Execution mode must be flash, ram or rom-dfu-ram")
    image_start, image_limit = ((APP_START, APP_END) if execution == "flash" else
                                (ROM_DFU_RAM_START if execution == "rom-dfu-ram" else RAM_START, RAM_END))
    image_region = "application_flash" if execution == "flash" else "sram"
    header = slice_checked(data, 0, 52)
    if header[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("Expected ELF32 little-endian version 1")
    (kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phcount,
     shsize, shcount, shnames) = struct.unpack_from("<HHIIIIIHHHHHH", header, 16)
    if kind != 2 or machine != 40 or version != 1 or ehsize != 52 or phsize != 32 or not phcount:
        raise ValueError("Expected executable ARM ELF with bounded program headers")
    if not (image_start <= (entry & ~1) < image_limit) or not entry & 1:
        raise ValueError(f"Entry must be a Thumb address in the {execution} execution region")
    slice_checked(data, phoff, phsize * phcount)
    segments = []
    pieces = []
    occupied = []
    for index in range(phcount):
        ptype, offset, vaddr, paddr, filesz, memsz, pflags, align = struct.unpack_from("<IIIIIIII", data, phoff + index * phsize)
        if ptype != 1:
            continue
        if filesz > memsz or align and align & (align - 1):
            raise ValueError("Invalid load size/alignment")
        payload = slice_checked(data, offset, filesz)
        region = region_for(vaddr, memsz)
        if execution != "flash" and (region != "sram" or vaddr < image_start or memsz > image_limit - vaddr):
            raise ValueError("RAM runtime segment overlaps the reserved region or leaves usable SRAM")
        if memsz:
            if any(vaddr < end and start < vaddr + memsz for start, end in occupied):
                raise ValueError("Overlapping virtual load segments")
            occupied.append((vaddr, vaddr + memsz))
        if region != image_region and pflags & 1:
            raise ValueError("Candidate executable code must reside in its declared execution region")
        if pflags & 1 and paddr != vaddr:
            raise ValueError("Executable flash load and virtual addresses must match")
        if filesz:
            if region_for(paddr, filesz) != image_region:
                raise ValueError("Initialized load bytes must reside in the declared load region")
            if paddr < image_start or filesz > image_limit - paddr:
                raise ValueError("Initialized load bytes overlap the reserved region or exceed the image limit")
            if any(paddr < start + len(piece) and start < paddr + filesz for start, piece in pieces):
                raise ValueError("Overlapping flash load bytes")
            pieces.append((paddr, payload))
        segments.append({"index": index, "virtual_address": vaddr, "load_address": paddr,
                         "file_bytes": filesz, "memory_bytes": memsz, "flags": pflags, "region": region})
    if not pieces or min(start for start, piece in pieces) != image_start:
        raise ValueError(f"Image must begin at 0x{image_start:08x}")
    last = max(start + len(piece) for start, piece in pieces)
    flat = bytearray(b"\xff" * (last - image_start))
    for start, payload in pieces:
        flat[start - image_start:start - image_start + len(payload)] = payload
    if len(flat) < VECTOR_WORDS * 4:
        raise ValueError("Incomplete F405 vector table")
    vectors = list(struct.unpack_from(f"<{VECTOR_WORDS}I", flat))
    executable_ranges = [(segment["virtual_address"], segment["virtual_address"] + segment["file_bytes"])
                         for segment in segments if segment["flags"] & 1]
    stack_floor = image_start if execution != "flash" else RAM_START
    if not stack_floor < vectors[0] <= RAM_END or vectors[0] % 8:
        raise ValueError("Initial stack must be aligned inside SRAM")
    if vectors[1] != entry:
        raise ValueError("ELF entry does not match reset vector")
    for index, handler in enumerate(vectors[1:], 1):
        if index in RESERVED_VECTORS:
            if handler:
                raise ValueError(f"Reserved vector {index} is nonzero")
        elif not handler & 1 or not any(start <= (handler & ~1) < end for start, end in executable_ranges):
            raise ValueError(f"Vector {index} does not target file-backed executable Thumb code")
    symbols = {}
    sections = []
    if shcount:
        if shsize != 40 or shnames >= shcount:
            raise ValueError("Invalid section header table")
        slice_checked(data, shoff, shsize * shcount)
        sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shsize) for i in range(shcount)]
        for section in sections:
            _, stype, _, _, offset, length, link, _, _, entsize = section
            if stype == 8:
                continue
            slice_checked(data, offset, length)
            if stype != 2:
                continue
            if entsize != 16 or length % 16 or link >= shcount or sections[link][1] != 3:
                raise ValueError("Invalid symbol table")
            strings = slice_checked(data, sections[link][4], sections[link][5])
            for position in range(offset, offset + length, 16):
                name, value, size, info, other, section_index = struct.unpack_from("<IIIBBH", data, position)
                if name >= len(strings):
                    raise ValueError("Symbol string outside table")
                end = strings.find(b"\0", name)
                if end < 0:
                    raise ValueError("Unterminated symbol name")
                label = strings[name:end].decode("utf-8", errors="strict")
                if label and section_index:
                    symbols[label] = {"address": value, "bytes": size}
    result = {"format": "thy666.stm32-diagnostic-inspection", "version": 1,
              "target": "Candidate STM32F405VGT6; owner board identity unconfirmed",
              "execution_mode": execution, "image_start": image_start, "image_end": last,
              "reserved_rom_ram_bytes": ROM_DFU_RAM_START - RAM_START if execution == "rom-dfu-ram" else 0,
              "application_start": APP_START if execution == "flash" else None,
              "application_end": last if execution == "flash" else None, "entry": entry,
              "image_bytes": len(flat), "image_sha256": hashlib.sha256(flat).hexdigest(),
              "elf_sha256": hashlib.sha256(data).hexdigest(), "elf_flags": flags,
              "vector_words": VECTOR_WORDS, "initial_stack": vectors[0], "segments": segments,
              "memory_bytes": {region: sum(s["memory_bytes"] for s in segments if s["region"] == region)
                               for region in ("application_flash", "sram", "ccm")},
              "symbols": symbols, "flash_ready": False, "device_execution_ready": False,
              "unresolved": ["Actual hardware revision", "Backup and independent recovery",
                             "Bootloader handoff and updater metadata/erase contract", "On-device driver validation"]}
    return result, bytes(flat)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--execution", choices=("flash", "ram", "rom-dfu-ram"), default="flash")
    args = parser.parse_args()
    report, image = inspect_elf(args.elf.read_bytes(), execution=args.execution)
    if args.binary:
        args.binary.parent.mkdir(parents=True, exist_ok=True)
        args.binary.write_bytes(image)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("image_bytes", "entry", "memory_bytes", "flash_ready")}, indent=2))


if __name__ == "__main__":
    main()
