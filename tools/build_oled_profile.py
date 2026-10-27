"""Project THY666's independent C glyphs and OLED geometry into the web preview."""
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
GLYPH_SOURCE = ROOT / "core/thy_ui.c"


def preview_fonts(source):
    """Read compact glyph declarations; the shared C renderer stays authoritative."""
    entries = re.findall(r"case '([^']+)':\s*bits=0x([0-9a-fA-F]+)u;", source)
    glyphs = {character: int(bits, 16) for character, bits in entries}
    if len(glyphs) < 40 or any(character not in glyphs for character in "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZb"):
        raise ValueError("Independent C glyph declarations are missing or changed format")
    fonts = []
    for identity, scale in (("small", 1), ("large", 2)):
        rows = []
        for code in range(32, 127):
            character = chr(code)
            if "a" <= character <= "z" and character != "b":
                character = character.upper()
            bits = glyphs.get(character, 0)
            for row in range(5 * scale):
                mask = 0
                for column in range(4 * scale):
                    if (bits >> ((column // scale) * 8)) & (1 << (row // scale)):
                        mask |= 0x8000 >> column
                rows.append(mask)
        fonts.append({"id": identity, "width": 4 * scale, "height": 5 * scale,
                      "first_codepoint": 32, "last_codepoint": 126,
                      "rows": rows, "advances": [5 * scale] * 95})
    return fonts


def build_profile():
    raw = GLYPH_SOURCE.read_bytes()
    # The page-transfer simulator has an explicit reference cadence. Actual
    # firmware uses dirty regions; optical panel scan remains unmeasured.
    spi_hz = 42000000 / 128
    service_interval = 41.6875
    page_minimum = (3 + 128) * 8 / spi_hz * 1000
    return {
        "format": "thy333.oled-profile", "version": 1,
        "provenance": {"source": "Independent THY666 C glyphs and OLED driver contract",
                       "glyph_source": GLYPH_SOURCE.relative_to(ROOT).as_posix(),
                       "source_sha256": hashlib.sha256(raw).hexdigest(), "contract": "docs/display.md"},
        "geometry": {"width": 128, "height": 64, "page_bytes": 128, "pages": 8,
                     "buffer_bytes": 1024, "page_height": 8,
                     "pixel_encoding": "byte=(y//8)*128+x; bit=1<<(y%8)"},
        "fonts": preview_fonts(raw.decode("utf-8")),
        "controller": {"identity": "SSD1306-compatible 128x64 monochrome OLED",
                       "init_commands": [int(byte, 16) for byte in
                         "AE 20 02 B0 C0 00 10 40 81 BB A0 A6 A8 3F A4 D3 00 D5 F0 D9 22 DA 12 DB 20 8D 14 AF".split()],
                       "internal_clock_command": "D5 F0; optical panel scan frequency is unmeasured"},
        "timing": {"spi_hz": spi_hz, "spi_divider": 128, "apb1_hz": 42000000,
                   "page_command_bytes": 3, "page_data_bytes": 128,
                   "page_wire_min_ms": page_minimum, "minimum_cycle_ms": page_minimum * 8,
                   "firmware_service_interval_ms": service_interval,
                   "firmware_service_hz": 1000 / service_interval,
                   "known_frame_interval_ms": None, "actual_frame_hz": None, "panel_scan_hz": None,
                   "source_note": "Reference page-transfer cadence for the simulator. Current device rendering prioritizes dirty regions and yields between bounded transfers.",
                   "startup_spi_hz": 42000000 / 64,
                   "initialization_updates": "Startup requests precede regular dirty-region service",
                   "transport": "SPI3; bounded transfers yield to MIDI and pad scanning"},
        "preview_policy": {"pixels": "Native 128x64 page encoding and independent C glyph projections; monochrome, integer-scaled",
                           "content": "Shared C renderer for current UI; projected glyphs support the fallback preview",
                           "timing": "Explicit serial-page simulation; browser scheduling and optical panel scan are separate",
                           "optical_refresh": "Exact measured hardware refresh remains unknown"},
    }


def main():
    output = ROOT / "web/generated/oled-profile.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(build_profile(), separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"OLED profile: 128x64, independent THY666 glyphs: {output}")


if __name__ == "__main__":
    main()
