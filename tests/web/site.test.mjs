import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { HELP_TOPICS, resolveHelpTopic } from '../../web/help-topics.js';

const read = name => readFileSync(new URL(`../../site/${name}`, import.meta.url), 'utf8');

test('all public guide deep links point to exact existing topics', () => {
  for (const page of ['index.html', 'install.html']) {
    for (const [, id] of read(page).matchAll(/manual\.html#([\w-]+)/g)) {
      assert.equal(resolveHelpTopic(id), id, `${page}: ${id}`);
    }
  }
});

test('homepage and installation diagrams reuse the current illustrated guide', async () => {
  const original = globalThis.document;
  const mounts = ['chordEntry', 'simple', 'update'].map(id => ({
    dataset: { siteFigure: id },
    append(image) { this.image = image; },
  }));
  globalThis.document = {
    querySelectorAll: () => mounts,
    createElement: tag => { assert.equal(tag, 'img'); return {}; },
  };
  try {
    // Exercise the staged artifact: its imports deliberately target only the
    // bounded guide copies, which do not exist inside the authoring directory.
    execFileSync(process.platform === 'win32' ? 'python' : 'python3',
      ['tools/build_site.py', '--output', 'build/site-node-check'],
      { cwd: fileURLToPath(new URL('../../', import.meta.url)), stdio: 'pipe' });
    const { siteFigures } = await import('../../build/site-node-check/site.js');
    for (const mount of mounts) {
      const figure = siteFigures[mount.dataset.siteFigure];
      assert.ok(figure, mount.dataset.siteFigure);
      assert.equal(mount.image.src, figure.src);
      assert.ok(mount.image.alt.length > 12);
      assert.ok(mount.image.width > 0 && mount.image.height > 0);
    }
    const existing = HELP_TOPICS.flatMap(topic => topic.sections.flatMap(s => s.figures || []));
    assert.ok(existing.some(figure => figure.src === siteFigures.simple.src));
  } finally { globalThis.document = original; }
});
