'use strict';

const assert = require('assert');
const {
  computeWeakChecksum,
  rollWeakChecksum,
  computeDelta,
  applyDelta,
} = require('./rollingHashDiff.js');

let passCount = 0;
let failCount = 0;

function test(name, fn) {
  try {
    fn();
    passCount++;
    console.log(`  PASS  ${name}`);
  } catch (err) {
    failCount++;
    console.log(`  FAIL  ${name}`);
    console.log(`        ${err.message}`);
  }
}

test('rolling forward matches a from-scratch recomputation over a sliding window', () => {
  const data = Buffer.from('the quick brown fox jumps over the lazy dog repeatedly and again', 'utf8');
  const windowLen = 8;
  let window = computeWeakChecksum(data, 0, windowLen);
  for (let pos = 0; pos + windowLen < data.length; pos++) {
    const oldByte = data[pos];
    const newByte = data[pos + windowLen];
    const rolled = rollWeakChecksum(window, oldByte, newByte, windowLen);
    const fresh = computeWeakChecksum(data, pos + 1, windowLen);
    assert.strictEqual(rolled.a, fresh.a);
    assert.strictEqual(rolled.b, fresh.b);
    assert.strictEqual(rolled.hash, fresh.hash);
    window = rolled;
  }
});

test('block matching finds a shifted block that moved to a different position', () => {
  const oldData = Buffer.from('AAAAAAAA' + 'BBBBBBBB' + 'CCCCCCCC' + 'DDDDDDDD', 'utf8');
  const newData = Buffer.from('XXXXXXXX' + 'AAAAAAAA' + 'BBBBBBBB' + 'CCCCCCCC' + 'DDDDDDDD', 'utf8');
  const delta = computeDelta(oldData, newData, 8);
  const copyOps = delta.ops.filter((op) => op.type === 'copy');
  assert.strictEqual(copyOps.length, 4);
  assert.deepStrictEqual(copyOps.map((op) => op.index), [0, 1, 2, 3]);
  const reconstructed = applyDelta(oldData, delta);
  assert.strictEqual(Buffer.compare(reconstructed, newData), 0);
});

test('block matching finds a moved block even when it changes order', () => {
  const oldData = Buffer.from('11111111' + '22222222' + '33333333', 'utf8');
  const newData = Buffer.from('33333333' + '11111111' + '22222222', 'utf8');
  const delta = computeDelta(oldData, newData, 8);
  const copyOps = delta.ops.filter((op) => op.type === 'copy');
  assert.deepStrictEqual(copyOps.map((op) => op.index), [2, 0, 1]);
});

test('delta round-trips byte-identically when new data has an insertion', () => {
  const oldData = Buffer.from('the quick brown fox jumps over the lazy dog', 'utf8');
  const newData = Buffer.from('the quick brown INSERTED fox jumps over the lazy dog', 'utf8');
  const delta = computeDelta(oldData, newData, 6);
  const reconstructed = applyDelta(oldData, delta);
  assert.strictEqual(Buffer.compare(reconstructed, newData), 0);
});

test('delta round-trips byte-identically when new data has a deletion', () => {
  const oldData = Buffer.from('the quick brown fox jumps over the lazy dog', 'utf8');
  const newData = Buffer.from('the quick fox jumps the lazy dog', 'utf8');
  const delta = computeDelta(oldData, newData, 6);
  const reconstructed = applyDelta(oldData, delta);
  assert.strictEqual(Buffer.compare(reconstructed, newData), 0);
});

test('delta round-trips byte-identically when nothing changed', () => {
  const oldData = Buffer.from('unchanged content stays exactly the same across versions', 'utf8');
  const newData = Buffer.from('unchanged content stays exactly the same across versions', 'utf8');
  const delta = computeDelta(oldData, newData, 10);
  const reconstructed = applyDelta(oldData, delta);
  assert.strictEqual(Buffer.compare(reconstructed, newData), 0);
  const copyOps = delta.ops.filter((op) => op.type === 'copy');
  assert.ok(copyOps.length > 0);
});

test('delta round-trips byte-identically when the data is completely different', () => {
  const oldData = Buffer.from('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', 'utf8');
  const newData = Buffer.from('zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz', 'utf8');
  const delta = computeDelta(oldData, newData, 8);
  const reconstructed = applyDelta(oldData, delta);
  assert.strictEqual(Buffer.compare(reconstructed, newData), 0);
  const copyOps = delta.ops.filter((op) => op.type === 'copy');
  assert.strictEqual(copyOps.length, 0);
});

test('strong hash confirmation avoids a deliberately forced weak-hash collision', () => {
  const blockA = Buffer.from([10, 10, 10, 10]);
  const blockB = Buffer.from([11, 7, 13, 9]);

  const weakA = computeWeakChecksum(blockA, 0, 4);
  const weakB = computeWeakChecksum(blockB, 0, 4);
  assert.strictEqual(weakA.hash, weakB.hash, 'test setup requires a genuine weak-hash collision');
  assert.notStrictEqual(blockA.toString('hex'), blockB.toString('hex'));

  const oldData = Buffer.concat([blockA, Buffer.from('unrelated trailing padding data here', 'utf8')]);
  const newData = blockB;

  const delta = computeDelta(oldData, newData, 4);
  const copyOps = delta.ops.filter((op) => op.type === 'copy');
  assert.strictEqual(copyOps.length, 0, 'strong hash should have rejected the weak-hash collision');

  const reconstructed = applyDelta(oldData, delta);
  assert.strictEqual(Buffer.compare(reconstructed, newData), 0);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
