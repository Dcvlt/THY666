"""Render the production OLED review with the actual shared C renderer.
No firmware/WASM artifacts are replaced; no device is opened.
"""
import ast
import html
import os
from pathlib import Path
import struct
import subprocess
import sys
import zlib
from build_diagnostic import compiler_identity, source_prefix_flags
from native_compile import native_target_flags

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/oled-design-preview'

def png(path, frame, scale):
    width, height = 128 * scale, 64 * scale
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            lit = frame[(y // scale // 8) * 128 + x // scale] & (1 << (y // scale % 8))
            raw.append(255 if lit else 0)
    def chunk(name, data):
        return struct.pack('>I', len(data)) + name + data + struct.pack('>I', zlib.crc32(name + data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB',width,height,8,0,0,0,0))
                    + chunk(b'IDAT',zlib.compress(raw)) + chunk(b'IEND',b''))

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    tree = ast.parse((ROOT / 'tools/build_core.py').read_text(encoding='utf-8-sig'))
    definition = next(node.value for node in ast.walk(tree) if isinstance(node, ast.Assign)
                      and any(isinstance(t, ast.Name) and t.id == 'sources' for t in node.targets))
    names = ast.literal_eval(definition.generators[0].iter)
    sources = [str(ROOT / 'core' / name) for name in names if name != 'thy_studio_ui.c']
    compiler, _ = compiler_identity()
    env = os.environ.copy()
    for key, folder in [('ZIG_GLOBAL_CACHE_DIR','.tools/zig-cache/global'),
                        ('ZIG_LOCAL_CACHE_DIR','.tools/zig-cache/local'),('TEMP','.tools/tmp'),('TMP','.tools/tmp')]:
        directory = ROOT / folder; directory.mkdir(parents=True, exist_ok=True); env[key] = str(directory)
    exe = OUT / ('render.exe' if os.name == 'nt' else 'render')
    subprocess.run([str(compiler),'cc','-std=c11','-Wall','-Wextra','-Werror','-O2',
                    *native_target_flags(),*source_prefix_flags(),'-I',str(ROOT/'core'),
                    *sources,str(ROOT/'tools/oled_design_preview.c'),'-o',str(exe)],cwd=ROOT,env=env,check=True)
    subprocess.run([str(exe),str(OUT)],cwd=ROOT,env=env,check=True)
    cards = []
    descriptions = [
        ('simple','Simple Play','Actual C-major note ownership. Same large notes and output rail; header underline follows the chord name; bottom rule frames the count line below.'),
        ('harmony','Harmony / Fifths','Synthetic Cmaj7 to D minor suggestion. The dotted divider is one pixel farther left; its notch separates the circle from the musical explanation.'),
        ('settings','Settings','Existing category icons and > selection cursor retained. No inverted selected row. Separators frame SETTINGS above and the navigation legend below.'),
        ('resources','Resources','Illustrative measurements, not a device reading. Bars show static RAM/CCM allocation, application flash and observed stack peak. CPU active includes polling; 100% is not an overload indicator.'),
        ('simple-five','Five-note check','Same existing five-note placement and size, with the shared frame. No loss of the center note.'),
        ('simple-seventh','Longer chord label','Synthetic F-sharp minor seventh. The top underline expands to the displayed chord name without touching the output rail.'),
        ('expression-idle','Outputs enabled','USB and DIN drawings use the supplied pixel design. Each is visible only when its destination is enabled and its driver is ready. The board cannot detect a DIN cable.'),
        ('velocity-left','Left Strike / Right Fixed','The left tick beside VEL marks physical-left Strike velocity. The right tick is absent. Swapping musical roles leaves these physical-side markers unchanged.'),
        ('aftertouch-split','Left Poly / Right Channel','P and C flank the centered AT label, showing the effective ordinary-MIDI pressure modes of the physical left and right halves.'),
        ('expression-mpe','MPE enabled','MPE has its own indicator. Ordinary AT is hidden because MPE Channel Pressure is a separate expression path.'),
        ('expression-arp','Arp enabled','The compact ARP drawing leaves every status at a fixed position. Two blank pixel rows separate adjacent indicators.')]
    for name,title,description in descriptions:
        for variant in ['rendered']:
            frame=(OUT/f'{name}-{variant}.bin').read_bytes()
            assert len(frame)==1024
            png(OUT/f'{name}-{variant}.png',frame,1)
            png(OUT/f'{name}-{variant}-4x.png',frame,4)
        cards.append(f'<section><h2>{title}</h2><p>{html.escape(description)}</p><div class="comparison">'+
                     ''.join(f'<article><h3>{label}</h3><div class="native"><img width="128" height="64" src="{name}-{variant}.png" alt="{title}, {label}, native 128 by 64"><span>128 × 64 · 1 CSS pixel per OLED pixel</span></div><img class="enlarged" width="512" height="256" src="{name}-{variant}-4x.png" alt="{title}, {label}, four times enlarged"><small>4× · nearest-neighbor pixels</small></article>' for variant,label in [('rendered','Shared C renderer')])+'</div></section>')
    (OUT/'index.html').write_text('''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>THY666 · OLED design review</title><style>
    *{box-sizing:border-box}body{background:#090909;color:#eee;font:16px/1.5 system-ui,sans-serif;max-width:1200px;margin:0 auto;padding:30px}h1{font-size:28px}h2{color:#ff3d55}p{max-width:95ch;color:#bbb}section{border-top:1px solid #69232b;margin-top:36px;padding-top:12px}.comparison{display:grid;grid-template-columns:1fr;gap:32px}article{min-width:0}.native{display:flex;align-items:center;gap:18px;min-height:90px}.native img{flex:none;image-rendering:pixelated}.native span,small{color:#888;font-size:12px}.enlarged{display:block;width:512px;max-width:100%;height:auto;image-rendering:pixelated;outline:1px solid #282828}small{display:block;margin-top:8px}@media(max-width:850px){.comparison{grid-template-columns:1fr}}
    </style><h1>THY666 / OLED design review</h1><p>Actual C-rendered screens and expression indicators, using synthetic input. No device access or flashing.</p><p>Native frames use 128 × 64 CSS pixels; physical size depends on screen density and browser zoom. Enlarged frames preserve every pixel. Selection keeps the current cursor; inversion remains reserved for editing.</p>'''+''.join(cards)+'</html>',encoding='utf-8')
    print(OUT/'index.html')

if __name__ == '__main__':
    main()
