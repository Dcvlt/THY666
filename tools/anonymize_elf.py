"""Anonymize non-loadable ELF32 DWARF strings without relocating the ELF.

The byte count of every replacement is fixed. Section/program headers, symbol
tables, relocation offsets and loadable bytes remain untouched. This also covers
Zig runtime DWARF, which is not affected by Clang's prefix-map options.
"""
from __future__ import annotations

import re
import struct


def anonymize_debug_paths(data: bytes, prefixes: list[tuple[str, str]]) -> tuple[bytes, dict]:
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("Expected a little-endian ELF32 image")
    fields = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    phoff, shoff, phsize, phcount, shsize, shcount, names_index = (
        fields[5], fields[6], fields[9], fields[10], fields[11], fields[12], fields[13])
    if (phsize != 32 or shsize != 40 or not shcount or names_index >= shcount or
            phoff + phcount * phsize > len(data) or shoff + shcount * shsize > len(data)):
        raise ValueError("Invalid ELF tables for debug-path anonymization")
    sections = [struct.unpack_from("<10I", data, shoff + index * shsize) for index in range(shcount)]
    name_table = sections[names_index]
    if name_table[4] + name_table[5] > len(data):
        raise ValueError("ELF section-name table leaves the file")
    names = data[name_table[4]:name_table[4] + name_table[5]]
    loads = []
    for index in range(phcount):
        kind, offset, _, _, size, _, _, _ = struct.unpack_from("<8I", data, phoff + index * phsize)
        if kind == 1:
            if offset + size > len(data):
                raise ValueError("ELF load segment leaves the file")
            loads.append((offset, offset + size))
    replacements = []
    for prefix, label in sorted(set(prefixes), key=lambda item: len(item[0]), reverse=True):
        original = prefix.encode("utf-8")
        replacement = label.encode("ascii")
        if len(original) < len(replacement) or not original or b"\0" in original:
            raise ValueError("Invalid debug-path replacement")
        replacements.append((re.compile(re.escape(original), re.I), replacement.ljust(len(original), b"/")))
    result = bytearray(data)
    changed, count = [], 0
    for section in sections:
        name_offset, _, flags, _, offset, size, _, _, _, _ = section
        if name_offset >= len(names):
            raise ValueError("Invalid ELF section name")
        end = names.find(b"\0", name_offset)
        if end < 0:
            raise ValueError("Unterminated ELF section name")
        name = names[name_offset:end].decode("ascii")
        if name.startswith(".zdebug") or (name.startswith(".debug") and flags & 0x800):
            raise ValueError("Compressed DWARF requires a separate anonymization path")
        if not name.startswith(".debug"):
            continue
        if (flags & 2 or offset + size > len(data) or
                any(offset < stop and start < offset + size for start, stop in loads)):
            raise ValueError("Debug section overlaps loadable bytes or leaves the file")
        block, occurrences = data[offset:offset + size], 0
        for pattern, replacement in replacements:
            block, matches = pattern.subn(lambda _: replacement, block)
            occurrences += matches
        if occurrences:
            result[offset:offset + size] = block
            count += occurrences
            changed.append(name)
    output = bytes(result)
    if len(output) != len(data) or any(output[start:stop] != data[start:stop] for start, stop in loads):
        raise ValueError("Debug anonymization changed ELF layout or loadable bytes")
    return output, {"strategy": "equal-length-non-loadable-dwarf-prefixes", "replacements": count,
                    "sections_changed": changed, "loadable_bytes_unchanged": True,
                    "symbols_and_headers_unchanged": True}
