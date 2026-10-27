/* Render a comparison sheet from actual shared C/WASM OLED frames. No device. */
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { createCore } from '../web/core.js';
import { MENU_ROWS } from '../web/studio-menu.js';

const root = fileURLToPath(new URL('../', import.meta.url));
const wasm = await readFile(path.join(root, 'web/generated/core.wasm'));
const build = JSON.parse(await readFile(path.join(root, 'web/generated/build-info.json'), 'utf8'));
if (createHash('sha256').update(wasm).digest('hex') !== build.artifacts.webassembly.sha256)
  throw new Error('Build the shared core before rendering the comparison.');
const output = process.argv[2] || path.join(root, 'web/generated/views-preview.svg');
const descriptions = [
  ['Detailed', 0, 'Six configurable information rows'],
  ['Piano', 1, 'Pitch-class keys and two information slots'],
  ['Simple', 4, 'Playing notes and two information slots'],
  ['Chladni', 5, 'Note-driven nodal interference'],
  ['Orbit', 6, 'Pitch-class ring and harmonic constellation'],
  ['Waves', 7, 'Layered note-driven harmonic traces'],
  ['Circle of fifths', 8, 'Fifths order, root marker and sounding chord'],
  ['Simple / Arp', 4, 'Available notes dim; sounding gate bright', true],
  ['Piano / Arp', 1, 'Available keys dim; sounding gate bright', true],
];
const viewRow = MENU_ROWS.findIndex(row => row.id === 'lights.view');
const pads = Array.from({ length: 96 }, (_, pad) => ({
  notes: pad < 48 ? [60, 64, 67, 0, 0, 0, 0] : [62, 0, 0, 0, 0, 0, 0],
  mask: pad < 48 ? 7 : 1, color: 1,
}));
const height = 142 + Math.ceil(descriptions.length / 3) * 284;
let svg = `<svg xmlns="http://www.w3.org/2000/svg" width="100%" height="100%" viewBox="0 0 1320 ${height}" style="background:#11171d">`;
svg += `<rect width="1320" height="${height}" fill="#11171d"/><g font-family="Consolas,monospace" fill="#f5e9cf">`;
svg += '<text x="24" y="36" font-size="24">THY666 / PLAY VIEWS</text><text x="24" y="63" font-size="14" fill="#a8b5bc">Actual 128 x 64 shared C frames / same held C major + D / 3x pixel scale</text>';
for (const [index, [name, id, description, arpeggiate]] of descriptions.entries()) {
  const engine = await createCore({ bytes: wasm });
  if (!engine.firmware.layout(pads).ok || !engine.state.set(viewRow, id).ok)
    throw new Error(`Cannot initialize ${name}`);
  engine.rhythm.advance(0);
  if (arpeggiate && !engine.arp.configure(0, { enabled: true }).ok)
    throw new Error('Cannot initialize the live Arp preview');
  engine.firmware.pressPad(0); engine.firmware.pressPad(48);
  engine.rhythm.advance(1000);
  engine.state.play();
  const frame = engine.state.frame({ now: 1000, scaleName: 'MAJOR C', animations: true });
  if (frame.length !== 1024) throw new Error('Unexpected OLED frame size');
  let pixels = '';
  for (let y = 0; y < 64; y++) for (let x = 0; x < 128;) {
    const lit = xx => frame[(y >> 3) * 128 + xx] & (1 << (y & 7));
    if (!lit(x)) { x++; continue; }
    const start = x;
    while (x < 128 && lit(x)) x++;
    pixels += `M${start} ${y}h${x - start}v1h-${x - start}z`;
  }
  const x = 24 + index % 3 * 424, y = 88 + Math.floor(index / 3) * 284;
  svg += `<g transform="translate(${x} ${y})"><rect width="408" height="268" rx="12" fill="#1e2932"/><text x="12" y="28" font-size="18">${name}</text><g transform="translate(12 43) scale(3)" shape-rendering="crispEdges"><rect width="128" height="64" fill="#03080c"/><path d="${pixels}" fill="#d5f5f4"/></g><text x="12" y="253" font-size="12" fill="#a8b5bc">${description}</text></g>`;
}
svg += `<text x="24" y="${height - 23}" font-size="13" fill="#a8b5bc">Graphical views respond to MIDI notes, not audio. Their motion and information slots remain configurable.</text></g></svg>`;
await mkdir(path.dirname(output), { recursive: true });
await writeFile(output, svg);
console.log(`C-rendered view comparison: ${output}`);
