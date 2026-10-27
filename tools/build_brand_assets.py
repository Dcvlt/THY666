"""Regenerate portable THY666 red/black branding from the supplied logo.

Optional artwork tools: FontTools reads the bundled OFL fonts; --png uses Sharp.
No firmware, device data, external images, system fonts or network is accessed.
"""
from __future__ import annotations
import argparse
import hashlib
from html import escape
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'docs/assets'
LOGO = ROOT / 'web/assets/thy666-web-logo.svg'
RED, DIM = '#ff2948', '#781529'
COMPACT_SOURCE = ROOT / 'docs/assets/branding/signal-labyrinth-oled/pixel-map.json'
FONT_FILES = {'title': 'monoton/Monoton-Regular.ttf', 'body': 'oxanium/Oxanium-Variable.ttf'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def logo_paths():
    source = ET.fromstring(LOGO.read_bytes())
    paths = []
    for node in source.iter():
        if node.tag.split('}')[-1] != 'path':
            continue
        rule = ' fill-rule="evenodd"' if node.get('fill-rule') == 'evenodd' else ''
        paths.append(f'<path d="{escape(node.attrib["d"], quote=True)}"{rule}/>')
    if len(paths) != 407 or source.get('viewBox') != '0 0 1442 1091':
        raise ValueError('Supplied logo geometry changed; review artwork placement')
    return ''.join(paths)



def compact_mark():
    mapping = json.loads(COMPACT_SOURCE.read_text(encoding='utf-8'))
    if len(mapping['pad_centers']) != 14 or len(mapping['digit_origins']) != 3:
        raise ValueError('Approved compact logo arrangement changed')
    parts = []
    for n, (x, y) in enumerate(mapping['pad_centers']):
        x, y = x + .5, y + .5
        parts.append(f'<g data-brand-pad="{n}" transform="translate({x} {y})"><path d="M0-3.6 3.6 0 0 3.6-3.6 0Z" fill="none" stroke="{RED}" stroke-width="1.15"/><path d="M0-1.7 1.7 0 0 1.7-1.7 0ZM0-3.6V-2.3M3.6 0H2.3M0 3.6V2.3M-3.6 0H-2.3" fill="none" stroke="{RED}" stroke-width=".65"/><path d="M0-.55.55 0 0 .55-.55 0Z"/></g>')
    for n, (x, y) in enumerate(mapping['digit_origins']):
        runs = []
        for row, bits in enumerate(mapping['digit_rows']):
            for column, value in enumerate(bits):
                if value == '1':runs.append(f'M{x+column} {y+row}h1v1h-1Z')
        parts.append(f'<path data-brand-six="{n}" d="{"".join(runs)}"/>')
    return ''.join(parts)


def svg(width, height, title, description, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" role="img" aria-labelledby="title description">'
        f'<title id="title">{escape(title)}</title><desc id="description">{escape(description)}</desc>'
        f'<rect width="{width}" height="{height}" fill="#000"/>{body}</svg>\n')


def generate():
    sys.path.insert(0, str(ROOT / '.tools/python-deps'))
    try:
        from fontTools.ttLib import TTFont
        from fontTools.pens.svgPathPen import SVGPathPen
        from fontTools.pens.transformPen import TransformPen
    except ImportError as error:
        raise ValueError('Artwork regeneration requires FontTools; committed assets need no extra tools') from error
    fonts = {key: TTFont(ROOT / 'web/assets/fonts' / name) for key, name in FONT_FILES.items()}
    def text(value, x, baseline, size, *, title=False, opacity=1):
        font = fonts['title' if title else 'body']
        glyphs = font.getGlyphSet(location=None if title else {'wght': 500})
        cmap = font.getBestCmap()
        pen, advance = SVGPathPen(glyphs), 0
        for character in value:
            glyph = glyphs[cmap[ord(character)]]
            glyph.draw(TransformPen(pen, (1, 0, 0, 1, advance, 0)))
            advance += glyph.width
        scale = size / font['head'].unitsPerEm
        return (f'<g fill="{RED}" opacity="{opacity}" transform="translate({x} {baseline}) '
            f'scale({scale:.9f} {-scale:.9f})"><path d="{pen.getCommands()}"/></g>')
    mark = logo_paths()
    full_logo = svg(1024, 1024, 'THY666 full-detail logo',
        'The supplied fourteen-diamond labyrinth logo, normalized to red on black.',
        f'<g fill="{RED}" transform="translate(43.35 150) scale(.65)">{mark}</g>')
    icon = svg(1024, 1024, 'THY666 compact icon',
        'Fourteen geometric diamond pads and three sixes, adapted from the approved compact OLED mark for a readable red-on-black avatar.',
        f'<g fill="{RED}" transform="translate(104 170) scale(17)">{compact_mark()}</g>')
    frame = f'''<g fill="none" stroke="{RED}" stroke-width="1">
      <path d="M12 92V32L32 12H324L338 24H862L876 12H1168L1188 32V92M12 308V368L32 388H324L338 376H862L876 388H1168L1188 368V308"/>
      <path d="M22 80V41L41 22H304M896 22H1159L1178 41V80M22 320V359L41 378H304M896 378H1159L1178 359V320" opacity=".38"/>
      <path d="M390 49V112L379 123V277L390 288V351M436 49H678L690 61H1038L1050 49H1135M436 351H654L667 339H1000L1012 351H1135" opacity=".58"/>
      <path d="M12 108V146L20 154V246L12 254V292M1188 108V146L1180 154V246L1188 254V292" opacity=".42"/>
      <path d="M46 57H112l13-13h53M46 326H85l12-12h43M1135 88v41l-12 12v35M1135 312v-34l-12-12v-23" opacity=".4"/>
    </g>'''
    ticks = []
    for n in range(43):
        x, height = 445 + n * 16, 6 if n % 4 == 0 else 3
        ticks.append(f'<path d="M{x} 33v{height}M{x} 367v-{height}"/>')
    for y in range(172, 229, 8):
        ticks.append(f'<path stroke-width="3" d="M29 {y}v4M1171 {y}v4"/>')
    banner = svg(1200, 400, 'THY666 - Open source firmware for TheoryBoard THY333',
        'Open source firmware for TheoryBoard THY333. Firmware, updater and web client. Supplied signal-labyrinth logo, outlined wordmark and angular circuit frame on pure black.',
        frame + f'<g stroke="{DIM}" fill="none">{"".join(ticks)}</g>' +
        f'<g fill="{RED}" transform="translate(44 61) scale(.235)">{mark}</g>' +
        text('THY666', 423, 229, 146, title=True) +
        text('OPEN SOURCE FIRMWARE FOR THEORYBOARD THY333', 430, 273, 20, opacity=.9) +
        text('FIRMWARE  /  UPDATER  /  WEB CLIENT', 430, 311, 15, opacity=.7) +
        f'<path d="M430 106h44l6-6h54m-104 139h414l12 12h210" fill="none" stroke="{DIM}" stroke-width="1"/>')
    for font in fonts.values():
        font.close()
    return {'thy666-banner.svg': banner, 'thy666-icon.svg': icon, 'thy666-logo.svg': full_logo}


def rasterize(node_modules):
    node = shutil.which('node')
    if not node:
        raise ValueError('PNG export requires Node.js and Sharp')
    script = """const path=require('path'),sharp=require('sharp'); const root=process.argv[1];
(async()=>{await sharp(path.join(root,'thy666-banner.svg')).png().toFile(path.join(root,'thy666-banner.png'));
await sharp(path.join(root,'thy666-logo.svg')).png().toFile(path.join(root,'thy666-logo.png'));
for(const size of [1024,512,128,64,32])await sharp(path.join(root,'thy666-icon.svg')).resize(size,size).png().toFile(path.join(root,size===1024?'thy666-icon.png':'thy666-icon-'+size+'.png'));
console.log('PNG branding exported with Sharp '+sharp.versions.sharp);})().catch(error=>{console.error(error.message);process.exit(1);});"""
    env = os.environ.copy()
    if node_modules:
        env['NODE_PATH'] = str(node_modules)
    subprocess.run([node, '-e', script, str(ASSETS)], env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--png', action='store_true', help='Export PNG sizes with local Node.js/Sharp')
    parser.add_argument('--node-modules', type=Path, help='Optional local Node package directory for Sharp; never written to output')
    parser.add_argument('--check', action='store_true', help='Verify SVGs and committed asset identities without rewriting')
    args = parser.parse_args()
    if args.check and args.png:
        parser.error('--check cannot rewrite PNGs')
    outputs = generate()
    ASSETS.mkdir(parents=True, exist_ok=True)
    for name, value in outputs.items():
        output = ASSETS / name
        if args.check:
            if not output.exists() or output.read_text(encoding='utf-8') != value:
                raise ValueError('Brand SVG is stale: ' + name)
        else:
            output.write_text(value, encoding='utf-8', newline='\n')
    if args.png:
        rasterize(args.node_modules)
    files = {}
    sizes = {'thy666-banner.png': (1200, 400), 'thy666-icon.png': (1024, 1024), 'thy666-logo.png': (1024, 1024),
        **{f'thy666-icon-{size}.png': (size, size) for size in (512, 128, 64, 32)}}
    for name in [*outputs, *sizes]:
        output = ASSETS / name
        if output.is_file():
            files[name] = {'sha256': sha(output.read_bytes()), 'bytes': output.stat().st_size}
            if name in sizes:
                files[name]['dimensions'] = list(sizes[name])
    manifest = {'format': 'thy666.brand-assets', 'version': 1,
        'palette': {'background': '#000000', 'red': RED, 'dim_red': DIM},
        'logo_source': 'web/assets/thy666-web-logo.svg', 'logo_source_sha256': sha(LOGO.read_bytes()),
        'logo_paths': 407, 'logo_policy': 'Original supplied contours, normalized to solid red',
        'compact_source': 'docs/assets/branding/signal-labyrinth-oled/pixel-map.json',
        'compact_source_sha256': sha(COMPACT_SOURCE.read_bytes()),
        'icon_policy': 'Approved 14-pad/3-six arrangement; simplified geometric contours and original compact digit grid',
        'font_license': 'SIL Open Font License 1.1',
        'reference_tools': {'fonttools': '4.60.1', 'sharp': '0.35.5'},
        'fonts': {name: {'path': 'web/assets/fonts/'+file, 'sha256': sha((ROOT/'web/assets/fonts'/file).read_bytes())} for name,file in FONT_FILES.items()},
        'files': files}
    target = ASSETS / 'branding-manifest.json'
    value = json.dumps(manifest, indent=2) + '\n'
    if args.check:
        if target.read_text(encoding='utf-8') != value:
            raise ValueError('Brand asset manifest differs')
    else:
        target.write_text(value, encoding='utf-8', newline='\n')
    print('THY666 red/black branding '+('verified' if args.check else 'generated')+': '+str(len(files))+' assets')


if __name__ == '__main__':
    main()
