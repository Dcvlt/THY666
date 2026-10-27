"""Offline licensing, exact-byte, glyph and fallback checks for browser fonts."""
import hashlib
import json
import pathlib
import re
import struct
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASSETS = ROOT / "web/assets/fonts"
PIN = "e7db6ca603fd4862010383ff17ffd6cc4ffac1c0"
FRENCH = "ÀÂÄÇÉÈÊËÎÏÔÖÙÛÜŸŒÆàâäçéèêëîïôöùûüÿœæ"
SYMBOLS = "♯♭♮♪♫←↑→↓↔⇧⚙✓✕⚠⌁⌕ⓘ▶■●○⏺⌘"


def sfnt_tables(data):
    count = struct.unpack_from(">H", data, 4)[0]
    result = {}
    for n in range(count):
        tag, _, offset, length = struct.unpack_from(">4sIII", data, 12 + n * 16)
        if offset + length > len(data):
            raise ValueError("font table exceeds file")
        result[tag] = (offset, length)
    return result


def unicode_glyphs(data):
    """Resolve Unicode cmap4/12 mappings to actual nonzero glyph IDs."""
    tables = sfnt_tables(data)
    offset, length = tables[b"cmap"]
    cmap = data[offset:offset + length]
    u16 = lambda p: struct.unpack_from(">H", cmap, p)[0]
    u32 = lambda p: struct.unpack_from(">I", cmap, p)[0]
    result = set()
    for n in range(u16(2)):
        platform, encoding, base = struct.unpack_from(">HHI", cmap, 4 + n * 8)
        if platform != 0 and not (platform == 3 and encoding in (1, 10)):
            continue
        fmt = u16(base)
        if fmt == 4:
            count = u16(base + 6) // 2
            ends = base + 14
            starts, deltas, shifts = ends + count * 2 + 2, ends + count * 4 + 2, ends + count * 6 + 2
            for i in range(count):
                for cp in range(u16(starts + i * 2), min(u16(ends + i * 2) + 1, 65535)):
                    delta, shift = u16(deltas + i * 2), u16(shifts + i * 2)
                    glyph = (cp + delta) & 65535
                    if shift:
                        glyph = u16(shifts + i * 2 + shift + (cp - u16(starts + i * 2)) * 2)
                        if glyph:
                            glyph = (glyph + delta) & 65535
                    if glyph:
                        result.add(cp)
        elif fmt == 12:
            for i in range(u32(base + 12)):
                start, end, glyph = struct.unpack_from(">III", cmap, base + 16 + i * 12)
                result.update(cp for cp in range(start, end + 1) if glyph + cp - start)
    return result


def css_points(value):
    points = set()
    for part in value.split(","):
        pair = part.strip().removeprefix("U+").split("-")
        start, end = int(pair[0], 16), int(pair[-1], 16)
        points.update(range(start, end + 1))
    return points


class WebTypographyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads((ASSETS / "manifest.json").read_text(encoding="utf-8"))
        cls.css = (ROOT / "web/typography.css").read_text(encoding="utf-8")
        cls.faces = {}
        for block in re.findall(r"@font-face\s*\{([^}]+)\}", cls.css):
            family = re.search(r"font-family:\s*'([^']+)'", block).group(1)
            cls.faces[family] = block

    def test_primary_pin_and_exact_files_keep_their_licenses(self):
        self.assertEqual(self.manifest["upstream_commit"], PIN)
        self.assertEqual({f["family"] for f in self.manifest["fonts"]}, {"Monoton", "Oxanium", "Orbitron"})
        for font in self.manifest["fonts"]:
            self.assertEqual(font["license"], "SIL Open Font License 1.1")
            for item in font["files"]:
                with self.subTest(font=font["family"], path=item["path"]):
                    data = (ASSETS / item["path"]).read_bytes()
                    self.assertEqual(len(data), item["bytes"])
                    self.assertEqual(hashlib.sha256(data).hexdigest(), item["sha256"])
                    self.assertTrue(item["source"].startswith("https://raw.githubusercontent.com/google/fonts/" + PIN + "/ofl/"))
            license_text = (ASSETS / font["license_path"]).read_text(encoding="utf-8")
            self.assertIn("SIL OPEN FONT LICENSE Version 1.1", license_text)
            self.assertIn(font["copyright"], license_text)
        notices = (ROOT / "THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8")
        for family in self.faces:
            self.assertIn(family, notices)

    def test_base_covers_ascii_french_and_numbers(self):
        base = next(f for f in self.manifest["fonts"] if f["family"] == "Oxanium")
        points = unicode_glyphs((ASSETS / base["font_path"]).read_bytes())
        self.assertTrue(set(range(32, 127)).issubset(points))
        self.assertTrue({ord(c) for c in FRENCH + "±©"}.issubset(points))
        self.assertTrue({ord(c) for c in "0123456789"}.issubset(points))

    def test_declared_unicode_ranges_only_cover_real_font_glyphs(self):
        for font in self.manifest["fonts"]:
            points = unicode_glyphs((ASSETS / font["font_path"]).read_bytes())
            block = self.faces[font["family"]]
            declared = css_points(re.search(r"unicode-range:\s*([^;]+);", block).group(1))
            self.assertTrue(declared)
            self.assertTrue(declared.issubset(points), font["family"])
            self.assertTrue({ord(c) for c in FRENCH}.issubset(declared), font["family"])
            self.assertFalse(declared.intersection(ord(c) for c in SYMBOLS), font["family"])

    def test_variable_weights_and_static_display_match_sources(self):
        expected = {"Oxanium": (200, 800), "Orbitron": (400, 900)}
        for font in self.manifest["fonts"]:
            data = (ASSETS / font["font_path"]).read_bytes()
            tables = sfnt_tables(data)
            block = self.faces[font["family"]]
            if font["family"] == "Monoton":
                self.assertNotIn(b"fvar", tables)
                self.assertRegex(block, r"font-weight:\s*400;")
                continue
            offset, _ = tables[b"fvar"]
            axes_offset, _, axes_count, axis_size = struct.unpack_from(">HHHH", data, offset + 4)
            axes = {}
            for n in range(axes_count):
                tag, minimum, default, maximum = struct.unpack_from(">4siii", data, offset + axes_offset + n * axis_size)
                axes[tag] = (minimum / 65536, maximum / 65536)
            self.assertEqual(axes[b"wght"], expected[font["family"]])
            self.assertRegex(block, r"font-weight:\s*" + " ".join(str(n) for n in expected[font["family"]]) + r";")

    def test_no_network_fonts_and_role_fallbacks_remain(self):
        self.assertNotRegex(self.css, r"url\(\s*['\"]?(?:https?:|//)")
        self.assertNotIn("@import", self.css)
        self.assertEqual(len(self.faces), 3)
        for block in self.faces.values():
            self.assertIn("font-display: swap;", block)
            path = re.search(r"url\('([^']+)'\)", block).group(1)
            self.assertTrue((ROOT / "web" / path).is_file())
        for role in ("base", "display", "accent", "symbol", "mono"):
            self.assertIn("--font-" + role + ":", self.css)
        self.assertIn("'Segoe UI Symbol'", self.css)
        self.assertIn("'Apple Symbols'", self.css)
        self.assertIn("'Noto Sans Symbols 2'", self.css)
        self.assertRegex(self.css, r"--font-accent:\s*'Orbitron',\s*'Oxanium'")
        self.assertNotRegex(self.css, r"(?:body|input|button|textarea|select)\s*\{[^}]*var\(--font-display\)")


if __name__ == "__main__":
    unittest.main()
