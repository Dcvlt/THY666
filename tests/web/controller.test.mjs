import test from 'node:test';
import assert from 'node:assert/strict';
import { ROOT_KEYS, MODE_KEYS } from '../../web/controller.js';

test('physical upper controls keep pitch ordering and all twelve shortcut identities', () => {
  assert.deepEqual(ROOT_KEYS, ['C', 'Db', 'D', 'Eb', 'E', 'F', 'Gb', 'G', 'Ab', 'A', 'Bb', 'B']);
  assert.equal(new Set(ROOT_KEYS).size, 12);
  assert.deepEqual(MODE_KEYS, ['Io', 'Do', 'Ph', 'Ly', 'Mx', 'Ae', 'Lo']);
  const right = ['5', '12', ...MODE_KEYS, 'U1', 'U2', 'U3'];
  assert.equal(new Set(right).size, 12);
  assert.equal(right.indexOf('U1'), 9);
  assert.equal(right.indexOf('U3'), 11);
});
