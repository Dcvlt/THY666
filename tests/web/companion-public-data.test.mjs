import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createCore } from '../../web/core.js';
import { StudioModel } from '../../web/studio-model.js';
import { SCALE_CATALOG } from '../../web/generated/menu-schema.js';

const bytes = await readFile(new URL('../../web/generated/core.wasm', import.meta.url));

test('public metadata supports every factory scale and configuration mirroring without a pad corpus', async () => {
  const source = await createCore({ bytes });
  const client = await createCore({ bytes });
  const model = new StudioModel({ engine: client, groups: SCALE_CATALOG });
  assert.deepEqual(SCALE_CATALOG, JSON.parse(await readFile(new URL('../../core/scale_catalog.json', import.meta.url))));
  for (const scale of SCALE_CATALOG) {
    assert.ok(source.state.catalog(scale.index, SCALE_CATALOG.length).ok);
    assert.equal(client.settings.apply(source.settings.encode()), true);
    assert.equal(model.get('performance.scale'), scale.index);
    assert.equal(model.format(model.definitions.get('performance.scale')), scale.name);
    assert.equal(client.voices().length, 0);
    assert.equal(client.stats().queued, 0);
  }
});
