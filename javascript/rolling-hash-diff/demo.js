'use strict';

const { computeDelta, applyDelta, estimateDeltaSize } = require('./rollingHashDiff.js');

function buildOldData() {
  const lines = [];
  for (let i = 0; i < 400; i++) {
    lines.push(`line ${i}: the quick brown fox jumps over the lazy dog number ${i * 7}`);
  }
  return Buffer.from(lines.join('\n') + '\n', 'utf8');
}

function buildNewData(oldData) {
  const text = oldData.toString('utf8');
  const lines = text.split('\n');

  lines.splice(50, 0, 'INSERTED: a brand new line that was not in the old version');
  lines.splice(50, 0, 'INSERTED: a second brand new line right after the first one');

  lines.splice(150, 20);

  for (let i = 250; i < 255; i++) {
    lines[i] = `line ${i}: this line was edited and no longer matches the original text`;
  }

  lines.push('APPENDED: one final trailing line added at the very end of the file');

  return Buffer.from(lines.join('\n'), 'utf8');
}

const oldData = buildOldData();
const newData = buildNewData(oldData);

console.log('old data size:', oldData.length, 'bytes');
console.log('new data size:', newData.length, 'bytes');

const blockSize = 64;
const delta = computeDelta(oldData, newData, blockSize);

const copyOps = delta.ops.filter((op) => op.type === 'copy').length;
const literalOps = delta.ops.filter((op) => op.type === 'literal');
const literalBytes = literalOps.reduce((sum, op) => sum + op.data.length, 0);

console.log('\ndelta computed with block size', blockSize);
console.log('  copy ops:', copyOps);
console.log('  literal ops:', literalOps.length, '(', literalBytes, 'literal bytes )');

const deltaSize = estimateDeltaSize(delta);
console.log('\nestimated delta size:', deltaSize, 'bytes');
console.log('full new data size:  ', newData.length, 'bytes');
console.log(
  'delta is',
  ((1 - deltaSize / newData.length) * 100).toFixed(1) + '%',
  'smaller than sending the full new data',
);

const reconstructed = applyDelta(oldData, delta);
const matches = Buffer.compare(reconstructed, newData) === 0;

console.log('\nreconstructed data size:', reconstructed.length, 'bytes');
console.log('reconstructed data is byte-identical to real new data:', matches);

if (!matches) {
  throw new Error('round trip failed: reconstructed data does not match new data');
}
