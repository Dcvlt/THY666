/* SPDX-License-Identifier: GPL-3.0-or-later
 * Presentation-only SVG assets. They never inspect, open or control a device.
 * data-state changes are owned by the updater's real reports, not this module.
 */
const SVG = 'http://www.w3.org/2000/svg';
let artSequence = 0;

function node(doc, tag, attributes = {}, text) {
  const value = doc.createElementNS(SVG, tag);
  for (const [name, setting] of Object.entries(attributes)) value.setAttribute(name, String(setting));
  if (text !== undefined) value.textContent = String(text);
  return value;
}

function brandSource(doc) {
  // The standalone bundle embeds this same editable vector in the startup and
  // header. Reuse its resolved URL instead of introducing a second logo copy.
  return doc.getElementById?.('startup-brand')?.getAttribute?.('href') ||
    doc.querySelector?.('.companion-brand-logo')?.getAttribute?.('src') || './assets/thy666-web-logo.svg';
}

function requireDocument(doc) {
  if (!doc?.createElement || !doc?.createElementNS) throw new TypeError('Update art requires a DOM document.');
}

export function createUpdateBoardArt(doc) {
  requireDocument(doc);
  const figure = doc.createElement('figure'); figure.className = 'update-board-art'; figure.dataset.state = 'waiting';
  const id = `update-board-${++artSequence}`;
  const svg = node(doc, 'svg', { viewBox: '0 0 1080 336', role: 'img', 'aria-labelledby': `${id}-title ${id}-description`, focusable: 'false' });
  svg.append(node(doc, 'title', { id: `${id}-title` }, 'Enter TheoryBoard update mode'),
    node(doc, 'desc', { id: `${id}-description` }, 'Illustration of the physical 96 pads and 43 small controls. The top-left and bottom-left large pads of the LEFT chord cluster are highlighted. The marked pair is an instruction, not sensed pad telemetry.'));
  const hull = node(doc, 'g', { class: 'update-board-hull' });
  hull.append(node(doc, 'path', { d: 'M71 128V100l12-12h361l16 12h160l16-12h361l12 12v28M71 274v25l12 12h361l16-12h160l16 12h361l12-12v-25' }),
    node(doc, 'path', { class: 'update-board-circuit', d: 'M80 137v29l-5 5v62l5 5v27M1000 137v29l5 5v62l-5 5v27M96 96h63l7 7h75M984 96h-63l-7 7h-75M96 303h63l7-7h75M984 303h-63l-7-7h-75' }));
  for (let n = 0; n < 15; n++) {
    const x = 336 + n * 29;
    hull.append(node(doc, 'path', { class: 'update-board-tick', d: `M${x} 91v${n % 3 ? 3 : 6}M${x} 306v-${n % 3 ? 3 : 6}` }));
  }
  svg.append(hull);
  const surface = node(doc, 'g', { class: 'update-board-surface' });
  for (let side = 0; side < 2; side++) {
    const left = side ? 648 : 100;
    for (let column = 0; column < 12; column++) {
      const x = left + column * 28;
      surface.append(node(doc, 'rect', { class: 'update-board-control update-board-upper', 'data-control': `upper-${side * 12 + column}`,
        x, y: 109, width: 24, height: 16, rx: 1 }));
      for (let row = 0; row < 4; row++) {
        const y = 141 + row * 36, held = side === 0 && column === 0 && (row === 0 || row === 3);
        const pad = node(doc, 'rect', { class: `update-board-pad${held ? ' update-board-pad-held' : ''}`,
          'data-pad': side * 48 + column * 4 + row, 'data-visual-pad': side * 48 + row * 12 + column,
          'data-side': side ? 'right' : 'left', 'data-row': row, 'data-column': column, 'data-held': held,
          x, y, width: 24, height: 29, rx: 1 });
        surface.append(pad);
        if (held) surface.append(node(doc, 'path', { class: 'update-board-hold-mark', d: `M${x + 7} ${y + 10}v9M${x + 17} ${y + 10}v9M${x + 7} ${y + 14.5}h10` }));
      }
    }
  }
  const oled = node(doc, 'g', { class: 'update-board-oled' });
  oled.append(node(doc, 'rect', { x: 457, y: 112, width: 166, height: 69, rx: 1 }),
    node(doc, 'image', { href: brandSource(doc), x: 502, y: 117, width: 76, height: 58, preserveAspectRatio: 'xMidYMid meet', 'aria-hidden': 'true' }));
  surface.append(oled);
  const navigation = node(doc, 'g', { class: 'update-board-navigation' });
  for (let index = 0; index < 5; index++) {
    const x = 472 + index * 34;
    navigation.append(index === 2 ? node(doc, 'ellipse', { class: 'update-board-control', 'data-control': 'navigation-center', cx: x, cy: 202, rx: 9, ry: 6 }) :
      node(doc, 'path', { class: 'update-board-control', 'data-control': `navigation-${index}`,
        d: index === 0 ? `M${x - 7} 202l13-7v14Z` : index === 1 ? `M${x} 195l7 13h-14Z` : index === 3 ? `M${x} 209l7-13h-14Z` : `M${x + 7} 202l-13-7v14Z` }));
  }
  surface.append(navigation);
  // Preserve the physical four voicing/strum, three Spread and seven lower
  // controls, rather than arranging fourteen decorative buttons in a grid.
  const functions = [[472,225],[517,225],[562,225],[607,225], [494,239],[540,239],[586,239],
    [472,253],[494,264],[517,275],[540,286],[562,275],[586,264],[607,253]];
  functions.forEach(([x,y], index) => surface.append(node(doc, 'path', { class: 'update-board-control update-board-function', 'data-control': `function-${index}`,
    d: `M${x} ${y - 6}l10 6-10 6-10-6Z` })));
  svg.append(surface);
  const cable = node(doc, 'g', { class: 'update-board-usb', 'aria-hidden': 'true' });
  cable.append(node(doc, 'path', { class: 'update-board-cable', d: 'M540 12v39' }),
    node(doc, 'path', { class: 'update-board-plug', d: 'M530 48h20v18l-4 5h-12l-4-5ZM535 48v-8h10v8M535 57h10' }),
    node(doc, 'path', { class: 'update-board-insert', d: 'M540 73v15m-4-4 4 4 4-4' }));
  svg.append(cable);
  const labels = node(doc, 'g', { class: 'update-board-labels' });
  labels.append(node(doc, 'text', { x: 100, y: 69 }, 'LEFT / CHORD PADS'), node(doc, 'text', { x: 648, y: 69 }, 'RIGHT / NOTE PADS'));
  const holdGuide = node(doc, 'g', { class: 'update-board-hold-guide', 'aria-hidden': 'true' });
  holdGuide.append(node(doc, 'path', { d: 'M95 155H84v108h11M84 209H61' }),
    node(doc, 'path', { d: 'M95 151l-4 4 4 4M95 259l-4 4 4 4' }), node(doc, 'text', { x: 34, y: 213 }, 'HOLD'));
  svg.append(holdGuide, labels);
  figure.append(svg); return figure;
}

export function createUpdateLogoArt(doc) {
  requireDocument(doc);
  const root = doc.createElement('div'); root.className = 'update-logo-art'; root.dataset.state = 'waiting';
  const id = `update-logo-${++artSequence}`;
  const svg = node(doc, 'svg', { viewBox: '0 0 1442 1091', role: 'img', 'aria-label': 'THY666', focusable: 'false' });
  const defs = node(doc, 'defs'), source = node(doc, 'image', { id: `${id}-source`, href: brandSource(doc), width: 1442, height: 1091 });
  defs.append(source);
  // Same fourteen diamonds as the established startup reveal; only the frame
  // and motion below are new. The project logo itself is never redrawn.
  const centers = [[721,181],[568,329],[875,329],[416,477],[1028,477],[264,626],[1180,626],
    [416,774],[721,774],[1028,774],[264,923],[568,923],[875,923],[1180,923]];
  const centerMask = node(doc, 'mask', { id: `${id}-center`, maskUnits: 'userSpaceOnUse', x: 0, y: 0, width: 1442, height: 1091 });
  centerMask.append(node(doc, 'rect', { width: 1442, height: 1091, fill: 'white' }));
  const tiles = [];
  centers.forEach(([x,y], index) => {
    const points = `${x},${y - 132} ${x + 132},${y} ${x},${y + 132} ${x - 132},${y}`;
    const clip = node(doc, 'clipPath', { id: `${id}-piece-${index}` }); clip.append(node(doc, 'polygon', { points })); defs.append(clip);
    centerMask.append(node(doc, 'polygon', { points, fill: 'black' }));
    const tile = node(doc, 'g', { class: 'update-logo-tile', style: `--update-piece:${index}` });
    tile.append(node(doc, 'use', { href: `#${id}-source`, 'clip-path': `url(#${id}-piece-${index})` })); tiles.push(tile);
  });
  defs.append(centerMask); svg.append(defs, ...tiles);
  const center = node(doc, 'g', { class: 'update-logo-center' });
  center.append(node(doc, 'use', { href: `#${id}-source`, mask: `url(#${id}-center)` })); svg.append(center);
  const frame = node(doc, 'g', { class: 'update-logo-frame', 'aria-hidden': 'true' });
  frame.append(node(doc, 'path', { d: 'M105 338V197l93-93h129M1115 104h129l93 93v141M1337 755v139l-93 93h-129M327 987H198l-93-93V755' }),
    node(doc, 'path', { class: 'update-logo-signal', d: 'M73 463v166M1369 463v166M492 47h154M796 47h154M492 1044h154M796 1044h154' }));
  svg.append(frame); root.append(svg); return root;
}

const iconPaths = {
  detect: ['M8 3H4v4M16 3h4v4M4 17v4h4M20 17v4h-4', 'M7 9h10v7H7ZM10 6h4M10 19h4'],
  backup: ['M7 3h8l4 4v14H5V3Z', 'M14 3v5h5M9 13l3 3 3-3M12 10v6M9 19h6'],
  install: ['M6 7V3h12v4M6 17v4h12v-4', 'M3 9h18v6H3ZM8 12h1m3 0h1m3 0h1'],
  verify: ['M12 2l8 3v6c0 5-3 8-8 11-5-3-8-6-8-11V5Z', 'M8 12l3 3 5-6'],
  usb: ['M12 20V4m-3 3 3-4 3 4M12 13l-6-4V6M12 16l6-4V8', 'M4.5 3h3v3h-3ZM16.5 5h3v3h-3ZM10 20h4v2h-4Z'],
  file: ['M6 2h8l5 5v15H6Z', 'M14 2v6h5M9 12h7M9 16h7M9 19h4'],
  check: ['M4 12l5 5L20 6'],
  shield: ['M12 2l8 3v6c0 5-3 8-8 11-5-3-8-6-8-11V5Z', 'M12 7v6m0 4v.1'],
  warning: ['M11 3h2l9 16-1 2H3l-1-2Z', 'M11 9h2l-.3 5h-1.4ZM11.3 17h1.4v1.4h-1.4Z'],
  arrow: ['M4 12h15m-6-6 6 6-6 6'],
  download: ['M4 16v5h16v-5', 'M12 3v12m-5-5 5 5 5-5'],
  close: ['m6 6 12 12M18 6 6 18'],
  clock: ['M20 12a8 8 0 1 1-16 0 8 8 0 0 1 16 0Z', 'M12 6v6l4 2'],
  board: ['M2 6h20v12H2Z', 'M5 9h3m-3 3h3m-3 3h3m8-6h3m-3 3h3m-3 3h3M10 9h4v3h-4Zm1 6h2'],
  pulse: ['M2 12h4l3-7 5 14 3-7h5'],
  refresh: ['M20 8V3l-4 4M4 16v5l4-4', 'M20 8a8 8 0 0 0-14-3M4 16a8 8 0 0 0 14 3'],
};

export function createUpdateIcon(doc, name) {
  requireDocument(doc);
  if (!Object.hasOwn(iconPaths, name)) throw new RangeError(`Unknown update icon: ${name}`);
  const svg = node(doc, 'svg', { class: `update-icon update-icon-${name}`, viewBox: '0 0 24 24', 'aria-hidden': 'true', focusable: 'false' });
  for (const d of iconPaths[name]) svg.append(node(doc, 'path', { d })); return svg;
}

export function createUpdateProgressArt(doc) {
  requireDocument(doc);
  const svg = node(doc, 'svg', { class: 'update-progress-art', viewBox: '0 0 720 34', 'aria-hidden': 'true', focusable: 'false' });
  svg.append(node(doc, 'path', { class: 'update-progress-rail', d: 'M1 12V1h260l9 5h180l9-5h260v11M1 22v11h260l9-5h180l9 5h260V22' }));
  for (let index = 0; index < 30; index++) svg.append(node(doc, 'path', { class: 'update-progress-segment', 'data-segment': index, 'data-state': 'pending',
    d: `M${10 + index * 23.3} 10h16v14h-16Z` }));
  return svg;
}
