#!/usr/bin/env python3
"""Preserve prepared pixel artwork and project its exact MSB bytes into C.

No resampling, thresholding, network or device access. Pillow verifies native
PNG pixels and renders actual C frame bytes; use the workspace Python runtime.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "docs/assets/branding/signal-labyrinth-oled"
HEADER = ROOT / "core/thy_brand_logo.h"
FILES = ("README.md", "signal_labyrinth_oled.h", "signal-labyrinth-48x40.bin",
         "signal-labyrinth-48x40.png", "signal-labyrinth-48x40.svg", "signal-labyrinth-48x40.xbm",
         "pixel-map.json", "oled-layout-128x64.png", "signal-labyrinth-icon-preview.png",
         "signal-labyrinth-oled-preview.png")


def preserve(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError(f"Existing source/evidence differs; use a fresh path: {path}")
    else:
        with path.open("xb") as out:
            out.write(data)


def frames(path, output):
    from io import BytesIO
    found = set()
    for line in path.read_text().splitlines():
        match = re.fullmatch(r"(BOOT|CREDITS) ([0-9a-f]{2048})", line)
        if not match or match[1] in found:
            raise ValueError("Expected unique actual C BOOT/CREDITS frame hex")
        name, raw = match[1].lower(), bytes.fromhex(match[2]);found.add(match[1])
        image = Image.new("1", (128, 64), 0)
        for y in range(64):
            for x in range(128):
                image.putpixel((x, y), 255 if raw[y // 8 * 128 + x] & (1 << (y % 8)) else 0)
        for suffix, pixels in (("native", image), ("8x", image.resize((1024, 512), Image.Resampling.NEAREST))):
            stream = BytesIO();pixels.save(stream, format="PNG")
            preserve(output / f"{name}-{suffix}.png", stream.getvalue())
    if found != {"BOOT", "CREDITS"}:
        raise ValueError("Both actual C frames required")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=ASSETS)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--frames", type=Path)
    args = parser.parse_args()
    if args.frames:
        frames(args.frames, args.output_dir)
        print("Rendered actual native C frames without interpolation");return
    source = {name: (args.source_dir / name).read_bytes() for name in FILES}
    raw = source["signal-labyrinth-48x40.bin"]
    array = re.search(r"signal_labyrinth_msb\[240\]\s*=\s*\{([^}]+)\}", source["signal_labyrinth_oled.h"].decode())
    if len(raw) != 240 or not array or bytes(int(n, 16) for n in re.findall(r"0x([0-9a-fA-F]{2})", array[1])) != raw:
        raise ValueError("Prepared C MSB array/raw bitmap disagree or are not 240 bytes")
    with Image.open(args.source_dir / "signal-labyrinth-48x40.png") as image:
        if image.size != (48, 40):raise ValueError("Expected native 48x40 PNG")
        rgb = image.convert("RGB")
        for y in range(40):
            for x in range(48):
                color = 255 if raw[y * 6 + x // 8] & (1 << (7 - x % 8)) else 0
                if rgb.getpixel((x, y)) != (color, color, color):raise ValueError("Prepared pixel mismatch/gray value")
    digest = hashlib.sha256(raw).hexdigest()
    lines = ["/* SPDX-License-Identifier: GPL-3.0-or-later */", "/* Exact supplied Signal Labyrinth MSB bitmap; no pixel changes.",
             " * Source: docs/assets/branding/signal-labyrinth-oled/", f" * Bitmap SHA256: {digest}",
             " * Projected by tools/import_oled_logo.py; alternative LSB array not linked. */",
             "#ifndef THY_BRAND_LOGO_H", "#define THY_BRAND_LOGO_H", "#include <stdint.h>",
             "#define THY_BRAND_LOGO_WIDTH 48u", "#define THY_BRAND_LOGO_HEIGHT 40u", "#define THY_BRAND_LOGO_STRIDE 6u",
             "static const uint8_t thy_brand_logo_bits[240] = {"]
    lines += ["    " + ",".join(f"0x{n:02x}" for n in raw[at:at + 12]) + "," for at in range(0, 240, 12)]
    header = ("\n".join(lines + ["};", "#endif", ""])).encode()
    if args.check:
        if HEADER.read_bytes() != header:raise ValueError("Core header differs from prepared pixels")
    else:
        for name, data in source.items():preserve(ASSETS / name, data)
        HEADER.write_bytes(header)
    report = {"format": "thy666.prepared-oled-art-import", "version": 1,
              "source_directory": str(args.source_dir.resolve()), "preserved_directory": str(ASSETS.relative_to(ROOT)),
              "dimensions": [48, 40], "bitmap_bytes": 240, "bitmap_sha256": digest, "pixel_changes": 0,
              "png_pixels_verified": 1920, "thresholding_or_resampling": False,
              "packing": "row-major, MSB first, six bytes per row",
              "source_files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name, data in source.items()},
              "core_header_sha256": hashlib.sha256(header).hexdigest(),
              "scope": "Exact asset projection; physical OLED appearance remains unqualified"}
    preserve(args.output_dir / "prepared-logo-import.json", (json.dumps(report, indent=2) + "\n").encode())
    print(f"Exact prepared logo: 48x40, 240 bytes, zero pixel changes, {digest}")


if __name__ == "__main__":main()
