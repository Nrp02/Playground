'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { ChunkStore, IntegrityError, sha256 } = require('./chunkStore.js');

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

function withTempDir(fn) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'chunk-store-test-'));
  try {
    fn(dir);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

test('put returns the sha256 hash of the content', () => {
  withTempDir((dir) => {
    const store = new ChunkStore(dir);
    const buf = Buffer.from('hello');
    const { hash, size } = store.put(buf);
    assert.strictEqual(hash, sha256(buf));
    assert.strictEqual(size, buf.length);
  });
});

test('identical content stored once on disk with refcount 2', () => {
  withTempDir((dir) => {
    const store = new ChunkStore(dir);
    const a = store.put(Buffer.from('dup content'));
    const b = store.put(Buffer.from('dup content'));
    assert.strictEqual(a.hash, b.hash);
    assert.strictEqual(store.size(), 1);
    assert.strictEqual(store.refcount(a.hash), 2);
  });
});

test('release decrements refcount and only deletes at zero', () => {
  withTempDir((dir) => {
    const store = new ChunkStore(dir);
    const { hash } = store.put(Buffer.from('shared'));
    store.put(Buffer.from('shared'));
    store.release(hash);
    assert.ok(fs.existsSync(store.chunkPath(hash)));
    assert.strictEqual(store.refcount(hash), 1);
    store.release(hash);
    assert.ok(!fs.existsSync(store.chunkPath(hash)));
    assert.strictEqual(store.refcount(hash), 0);
  });
});

test('get returns the original bytes', () => {
  withTempDir((dir) => {
    const store = new ChunkStore(dir);
    const original = Buffer.from('round trip content');
    const { hash } = store.put(original);
    assert.ok(store.get(hash).equals(original));
  });
});

test('get throws IntegrityError when the chunk on disk is corrupted', () => {
  withTempDir((dir) => {
    const store = new ChunkStore(dir);
    const { hash } = store.put(Buffer.from('trustworthy'));
    fs.writeFileSync(store.chunkPath(hash), Buffer.from('tampered'));
    assert.throws(() => store.get(hash), IntegrityError);
  });
});

test('get throws for an unknown hash', () => {
  withTempDir((dir) => {
    const store = new ChunkStore(dir);
    assert.throws(() => store.get('0'.repeat(64)), RangeError);
  });
});

test('addRef increments refcount for an existing chunk', () => {
  withTempDir((dir) => {
    const store = new ChunkStore(dir);
    const { hash } = store.put(Buffer.from('ref me'));
    store.addRef(hash);
    assert.strictEqual(store.refcount(hash), 2);
    store.release(hash);
    store.release(hash);
    assert.strictEqual(store.refcount(hash), 0);
  });
});

test('addRef throws for a hash that was never put', () => {
  withTempDir((dir) => {
    const store = new ChunkStore(dir);
    assert.throws(() => store.addRef('f'.repeat(64)), RangeError);
  });
});

test('empty buffer round-trips correctly', () => {
  withTempDir((dir) => {
    const store = new ChunkStore(dir);
    const { hash } = store.put(Buffer.alloc(0));
    assert.strictEqual(store.get(hash).length, 0);
  });
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
