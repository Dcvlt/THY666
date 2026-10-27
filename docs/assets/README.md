# THY666 artwork

Red on pure black, with angular circuit lines and outlined lettering. These are
ready-to-use repository assets; no artwork tools are needed to run the companion
or build the firmware.

| Asset | Size | Use |
| --- | --- | --- |
| [Banner PNG](thy666-banner.png) / [SVG](thy666-banner.svg) | 1200 × 400 | README header; full supplied labyrinth logo and THY666 wordmark |
| [Icon PNG](thy666-icon.png) / [SVG](thy666-icon.svg) | 1024 × 1024 | Square avatar; compact adaptation with clearer small-size geometry |
| [512 px](thy666-icon-512.png), [128 px](thy666-icon-128.png), [64 px](thy666-icon-64.png), [32 px](thy666-icon-32.png) | Square | Ready-made icon exports |
| [Full-detail logo PNG](thy666-logo.png) / [SVG](thy666-logo.svg) | 1024 × 1024 | Original labyrinth contours for larger placements |

The banner and full-detail logo retain all 407 paths from the supplied
[web logo](../../web/assets/thy666-web-logo.svg), normalized to one solid red.
The compact icon preserves the fourteen diamond positions and three sixes from
the supplied [OLED pixel map](branding/signal-labyrinth-oled/pixel-map.json).
It replaces fine circuit contours with nested diamonds so the mark remains
recognizable at avatar sizes. It is a compact adaptation, not a replacement for
the editable full-detail source.

The banner's Monoton wordmark and Oxanium supporting text are converted to paths.
SVGs contain no external font, image, script or network references. Font sources
remain bundled under their SIL Open Font License 1.1:
[Monoton](../../web/assets/fonts/monoton/OFL.txt) and
[Oxanium](../../web/assets/fonts/oxanium/OFL.txt).

## Regenerating the assets

Run these commands from the repository root. FontTools is an optional artwork
dependency; the reference exports use FontTools 4.60.1 and Sharp 0.35.5.

```sh
python -m pip install --target .tools/python-deps fonttools==4.60.1
python tools/build_brand_assets.py
```

For PNG exports, install Sharp in the ignored artwork-tools directory. This does
not add a companion runtime dependency or alter the project's dependency files.

```sh
npm install --prefix .tools/branding --no-save --package-lock=false sharp@0.35.5
python tools/build_brand_assets.py --png --node-modules .tools/branding/node_modules
```

If Sharp is already available to Node, `--node-modules` can be omitted. Outputs
always use repository-relative sources; local dependency paths are never written
into the artwork or manifest.

```sh
python tools/build_brand_assets.py --check
```

The check verifies regenerated vector content and all committed output hashes.
[branding-manifest.json](branding-manifest.json) records source and font hashes,
the reference tool versions, each asset's SHA-256 and byte size, and PNG
dimensions. Refresh both SVG and PNG outputs after changing artwork; committing
only a changed source leaves the corresponding raster exports stale.
