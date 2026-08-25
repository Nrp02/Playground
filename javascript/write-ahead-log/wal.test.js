'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { WriteAheadLog, HEADER_BYTES } = require('./wal.js');

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

function tempWalPath() {
  return path.join(os.tmpdir(), `wal-test-${process.pid}-${Date.now()}-${Math.random().toString(16).slice(2)}.log`);
}

const tempPaths = [];

function makeWal() {
  const p = tempWalPath();
  tempPaths.push(p);
  return new WriteAheadLog(p);
}

test('append then replay returns records in order', () => {
  const wal = makeWal();
  wal.append({ op: 'set', key: 'a', value: 1 });
  wal.append({ op: 'set', key: 'b', value: 2 });
  wal.append({ op: 'set', key: 'c', value: 3 });

  const reopened = new WriteAheadLog(wal.filePath);
  const result = reopened.replay();
  assert.strictEqual(result.corrupted, false);
  assert.strictEqual(result.records.length, 3);
  assert.deepStrictEqual(result.records[0], { op: 'set', key: 'a', value: 1 });
  assert.deepStrictEqual(result.records[1], { op: 'set', key: 'b', value: 2 });
  assert.deepStrictEqual(result.records[2], { op: 'set', key: 'c', value: 3 });
});

test('replay on a fresh empty file returns no records', () => {
  const wal = makeWal();
  const result = wal.replay();
  assert.strictEqual(result.corrupted, false);
  assert.strictEqual(result.records.length, 0);
  assert.strictEqual(result.validBytes, 0);
});

test('detects a corrupted record via checksum mismatch and stops before it', () => {
  const wal = makeWal();
  wal.append({ op: 'set', key: 'a', value: 1 });
  const validSize = wal.size();
  wal.append({ op: 'set', key: 'b', value: 2 });

  const fd = fs.openSync(wal.filePath, 'r+');
  fs.writeSync(fd, Buffer.from('X'), 0, 1, validSize + HEADER_BYTES);
  fs.closeSync(fd);

  const reopened = new WriteAheadLog(wal.filePath);
  const result = reopened.replay();
  assert.strictEqual(result.corrupted, true);
  assert.strictEqual(result.validBytes, validSize);
  assert.strictEqual(result.records.length, 1);
  assert.deepStrictEqual(result.records[0], { op: 'set', key: 'a', value: 1 });
});

test('detects a truncated final record and stops replay cleanly', () => {
  const wal = makeWal();
  wal.append({ op: 'set', key: 'a', value: 1 });
  const validSize = wal.size();
  wal.append({ op: 'set', key: 'b', value: 2 });

  const truncatedSize = validSize + HEADER_BYTES + 3;
  fs.truncateSync(wal.filePath, truncatedSize);

  const reopened = new WriteAheadLog(wal.filePath);
  const result = reopened.replay();
  assert.strictEqual(result.corrupted, true);
  assert.strictEqual(result.validBytes, validSize);
  assert.strictEqual(result.records.length, 1);
});

test('truncated header at the very end of the file is handled without crashing', () => {
  const wal = makeWal();
  wal.append({ op: 'set', key: 'a', value: 1 });
  const validSize = wal.size();
  fs.appendFileSync(wal.filePath, Buffer.from([1, 2, 3]));

  const reopened = new WriteAheadLog(wal.filePath);
  const result = reopened.replay();
  assert.strictEqual(result.corrupted, true);
  assert.strictEqual(result.validBytes, validSize);
  assert.strictEqual(result.records.length, 1);
});

test('compaction discards records before the given index and keeps the rest', () => {
  const wal = makeWal();
  wal.append({ op: 'set', key: 'a', value: 1 });
  wal.append({ op: 'set', key: 'b', value: 2 });
  wal.append({ op: 'set', key: 'c', value: 3 });
  wal.append({ op: 'set', key: 'd', value: 4 });

  const kept = wal.compact(2);
  assert.strictEqual(kept, 2);

  const result = wal.replay();
  assert.strictEqual(result.records.length, 2);
  assert.deepStrictEqual(result.records[0], { op: 'set', key: 'c', value: 3 });
  assert.deepStrictEqual(result.records[1], { op: 'set', key: 'd', value: 4 });
});

test('compaction reduces file size and further appends still replay correctly', () => {
  const wal = makeWal();
  for (let i = 0; i < 10; i++) {
    wal.append({ op: 'set', key: `k${i}`, value: i });
  }
  const sizeBefore = wal.size();
  wal.compact(8);
  const sizeAfter = wal.size();
  assert.ok(sizeAfter < sizeBefore);

  wal.append({ op: 'set', key: 'new', value: 99 });
  const result = wal.replay();
  assert.strictEqual(result.records.length, 3);
  assert.deepStrictEqual(result.records[2], { op: 'set', key: 'new', value: 99 });
});

test('compact(0) keeps every record', () => {
  const wal = makeWal();
  wal.append({ op: 'set', key: 'a', value: 1 });
  wal.append({ op: 'set', key: 'b', value: 2 });
  const kept = wal.compact(0);
  assert.strictEqual(kept, 2);
  assert.strictEqual(wal.replay().records.length, 2);
});

for (const p of tempPaths) {
  try {
    fs.unlinkSync(p);
  } catch (err) {
    if (err.code !== 'ENOENT') {
      throw err;
    }
  }
}

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
