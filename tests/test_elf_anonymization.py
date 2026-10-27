"""DWARF anonymization preserves offsets, symbols and executable payloads."""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from anonymize_elf import anonymize_debug_paths


def fixture(*, flags=0, offset=256, name=".debug_str"):
    names = b"\0.shstrtab\0.text\0" + name.encode("ascii") + b"\0.symtab\0"
    data = bytearray(712)
    struct.pack_into("<16sHHIIIIIHHHHHH", data, 0, b"\x7fELF\x01\x01\x01" + bytes(9),
                     2, 40, 1, 0x08040001, 52, 512, 0, 52, 32, 1, 40, 5, 1)
    struct.pack_into("<8I", data, 52, 1, 128, 0x08040000, 0x08040000, 16, 16, 5, 4)
    data[128:144] = b"executable-bytes"
    data[160:160 + len(names)] = names
    debug = b"/private/test-owner/project/core.c\0public string\0"
    data[256:256 + len(debug)] = debug
    data[384:400] = b"synthetic-symbol"
    rows = [(0,) * 10,
            (1, 3, 0, 0, 160, len(names), 0, 0, 1, 0),
            (names.index(b".text"), 1, 6, 0x08040000, 128, 16, 0, 0, 4, 0),
            (names.index(name.encode()), 1, flags, 0, offset, 64, 0, 0, 1, 0),
            (names.index(b".symtab"), 2, 0, 0, 384, 16, 1, 0, 4, 16)]
    for index, row in enumerate(rows):
        struct.pack_into("<10I", data, 512 + index * 40, *row)
    return bytes(data)


class ELFAnonymizationTests(unittest.TestCase):
    def test_equal_length_debug_changes_preserve_headers_symbols_and_payload(self):
        original = fixture()
        changed, report = anonymize_debug_paths(original, [("/private/test-owner", "/home")])
        self.assertEqual(len(changed), len(original))
        self.assertNotIn(b"/private/test-owner", changed)
        self.assertIn(b"/private/test-owner", original)
        self.assertEqual(changed[:256], original[:256])
        self.assertEqual(changed[320:], original[320:])
        self.assertEqual(report["replacements"], 1)
        self.assertTrue(report["loadable_bytes_unchanged"])
        self.assertTrue(report["symbols_and_headers_unchanged"])

    def test_matching_is_case_insensitive_and_idempotent(self):
        original = fixture()
        prefixes = [("/PRIVATE/TEST-OWNER", "/home")]
        changed, _ = anonymize_debug_paths(original, prefixes)
        repeated, report = anonymize_debug_paths(changed, prefixes)
        self.assertEqual(changed, repeated)
        self.assertEqual(report["replacements"], 0)

    def test_longest_prefix_wins(self):
        changed, report = anonymize_debug_paths(fixture(), [
            ("/private", "/local"), ("/private/test-owner", "/home")])
        self.assertIn(b"/home", changed)
        self.assertNotIn(b"/local", changed)
        self.assertEqual(report["replacements"], 1)

    def test_loadable_or_out_of_file_debug_sections_are_rejected(self):
        for args in ({"flags": 2}, {"offset": 132}, {"offset": 700}):
            with self.subTest(args=args), self.assertRaises(ValueError):
                anonymize_debug_paths(fixture(**args), [("/private/test-owner", "/home")])

    def test_compressed_debug_is_rejected_instead_of_silently_leaking_paths(self):
        for args in ({"flags": 0x800}, {"name": ".zdebug_str"}):
            with self.subTest(args=args), self.assertRaises(ValueError):
                anonymize_debug_paths(fixture(**args), [("/private/test-owner", "/home")])

    def test_malformed_headers_are_rejected(self):
        for data in (b"not an ELF", fixture()[:600]):
            with self.assertRaises(ValueError):
                anonymize_debug_paths(data, [("/private/test-owner", "/home")])


if __name__ == "__main__":
    unittest.main()
