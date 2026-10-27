# Signal Labyrinth — OLED asset

A native one-bit adaptation of the selected Signal Labyrinth / C2 concept for the THY333 open-source firmware project. The original 14-pad arrangement and triangular placement of the three sixes are preserved on a pixel grid. The fine maze engraving has been reduced to a substantial diamond perimeter, a central node and a short signal path.

## Asset

- Canvas: **48 × 40 pixels**.
- Colors: black background and white illuminated pixels only. No antialiasing or gray pixels.
- Fourteen 9 × 9 diamond envelopes, in rows **1 / 2 / 2 / 2 / 3 / 4**.
- Three custom **5 × 7** sixes; one above two below.
- Bitmap storage: **240 bytes** for either encoding (6 bytes per row × 40 rows).
- A 40-pixel-high allocation includes one blank bottom row. The pad grid has a 39-pixel-high visible envelope.

## Files

| File | Purpose |
| --- | --- |
| signal-labyrinth-48x40.png | Native 48 × 40, two-color PNG |
| signal-labyrinth-48x40.svg | Exact pixel master; rectangles on an integer grid |
| signal-labyrinth-48x40.bin | 240 raw bytes, row-major, most-significant bit first |
| signal-labyrinth-48x40.xbm | XBM source, row-major, least-significant bit first |
| signal_labyrinth_oled.h | Both C encodings, documented below |
| oled-layout-128x64.png | Native display composition with illustrative status text |
| signal-labyrinth-oled-preview.png | Exact 10× enlargement of the 128 × 64 composition |
| signal-labyrinth-icon-preview.png | Exact 12× enlargement of the icon |
| pixel-map.json | Pad/digit positions and verified clearance measurements |

## Firmware integration

Choose the bit order expected by your existing bitmap drawing function. In both supplied encodings, byte rows run from top to bottom and pixels run left to right. Bit value **1** means illuminated foreground; **0** means background. Use one array rather than both if storage matters. The header deliberately has no dependency on a particular display library or microcontroller. Add the flash-storage attribute required by your toolchain if appropriate.

For the MSB-first variant, the pixel at (x,y) is:

~~~c
uint8_t on = (signal_labyrinth_msb[y * 6u + x / 8u] >> (7u - (x % 8u))) & 1u;
~~~

For the XBM/LSB-first variant, use a shift of (x % 8u). These are row-major sprite encodings; they are **not** a raw SSD1306 or other display-controller page buffer. If your firmware directly writes controller pages, convert or draw through the existing pixel routine.

The example places the icon at **(4,12)** on a **128 × 64** canvas. A separator occupies x=58; sample status text starts at x=65. “MIDI”, “READY” and “120 BPM” are demonstration labels, not live device data. No existing firmware project was modified.

## Validation

The exported arrays were decoded and compared against the pixel master. All 14 pad positions were checked. Each six is separated from the complete pad envelopes and the other digits by at least one blank pixel, including diagonal adjacency. The native PNG and displayed preview are rendered from the same pixel data. The asset is validated digitally; physical OLED appearance has not been tested.
