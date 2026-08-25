'use strict';

const assert = require('assert');
const { ContentAddressableStore, IntegrityError } = require('./store.js');

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

test('put returns the sha256 hex hash of the content', () => {
  const store = new ContentAddressableStore();
  const hash = store.put('hello world');
  assert.strictEqual(hash, 'b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9');
});

test('storing the same bytes twice returns the same hash', () => {
  const store = new ContentAddressableStore();
  const hashA = store.put('duplicate content');
  const hashB = store.put('duplicate content');
  assert.strictEqual(hashA, hashB);
});

test('storing the same bytes twice does not duplicate storage', () => {
  const store = new ContentAddressableStore();
  store.put('duplicate content');
  store.put('duplicate content');
  assert.strictEqual(store.size(), 1);
});

test('storing different content grows the object count', () => {
  const store = new ContentAddressableStore();
  store.put('a');
  store.put('b');
  store.put('c');
  assert.strictEqual(store.size(), 3);
});

test('get returns the original bytes as a buffer', () => {
  const store = new ContentAddressableStore();
  const hash = store.put('round trip me');
  const result = store.get(hash);
  assert.strictEqual(Buffer.isBuffer(result), true);
  assert.strictEqual(result.toString('utf8'), 'round trip me');
});

test('get on an unknown hash throws RangeError', () => {
  const store = new ContentAddressableStore();
  assert.throws(() => store.get('0'.repeat(64)), RangeError);
});

test('put rejects non-string non-buffer input', () => {
  const store = new ContentAddressableStore();
  assert.throws(() => store.put(42), TypeError);
});

test('get throws IntegrityError when stored bytes are tampered with', () => {
  const store = new ContentAddressableStore();
  const hash = store.put('trustworthy content');
  store._objects.set(hash, Buffer.from('sneaky swapped content'));
  assert.throws(() => store.get(hash), IntegrityError);
});

test('get still succeeds for untampered objects after a sibling is tampered', () => {
  const store = new ContentAddressableStore();
  const safeHash = store.put('safe content');
  const tamperedHash = store.put('will be corrupted');
  store._objects.set(tamperedHash, Buffer.from('corrupted'));
  assert.strictEqual(store.get(safeHash).toString('utf8'), 'safe content');
  assert.throws(() => store.get(tamperedHash), IntegrityError);
});

test('putTree stores a listing of child hashes and names', () => {
  const store = new ContentAddressableStore();
  const blobHash = store.put('leaf content');
  const treeHash = store.putTree([{ name: 'file.txt', hash: blobHash, type: 'blob' }]);
  const entries = store.getTree(treeHash);
  assert.strictEqual(entries.length, 1);
  assert.strictEqual(entries[0].name, 'file.txt');
  assert.strictEqual(entries[0].hash, blobHash);
  assert.strictEqual(entries[0].type, 'blob');
});

test('putTree is deterministic regardless of input entry order', () => {
  const store = new ContentAddressableStore();
  const hashA = store.put('a');
  const hashB = store.put('b');
  const treeOne = store.putTree([
    { name: 'a.txt', hash: hashA, type: 'blob' },
    { name: 'b.txt', hash: hashB, type: 'blob' },
  ]);
  const treeTwo = store.putTree([
    { name: 'b.txt', hash: hashB, type: 'blob' },
    { name: 'a.txt', hash: hashA, type: 'blob' },
  ]);
  assert.strictEqual(treeOne, treeTwo);
});

test('putTree rejects duplicate entry names', () => {
  const store = new ContentAddressableStore();
  const hash = store.put('x');
  assert.throws(() => store.putTree([
    { name: 'dup.txt', hash, type: 'blob' },
    { name: 'dup.txt', hash, type: 'blob' },
  ]), TypeError);
});

test('walkTree recursively resolves a nested tree structure', () => {
  const store = new ContentAddressableStore();
  const mainHash = store.put('main contents');
  const utilHash = store.put('util contents');
  const readmeHash = store.put('readme contents');

  const srcTreeHash = store.putTree([
    { name: 'main.js', hash: mainHash, type: 'blob' },
    { name: 'util.js', hash: utilHash, type: 'blob' },
  ]);
  const rootTreeHash = store.putTree([
    { name: 'README.md', hash: readmeHash, type: 'blob' },
    { name: 'src', hash: srcTreeHash, type: 'tree' },
  ]);

  const walked = store.walkTree(rootTreeHash);
  assert.deepStrictEqual(walked, {
    'README.md': 'readme contents',
    src: {
      'main.js': 'main contents',
      'util.js': 'util contents',
    },
  });
});

test('resolvePath walks down to a single blob by path', () => {
  const store = new ContentAddressableStore();
  const utilHash = store.put('util contents');
  const srcTreeHash = store.putTree([{ name: 'util.js', hash: utilHash, type: 'blob' }]);
  const rootTreeHash = store.putTree([{ name: 'src', hash: srcTreeHash, type: 'tree' }]);
  assert.strictEqual(store.resolvePath(rootTreeHash, 'src/util.js'), 'util contents');
});

test('resolvePath walks down to a subtree by path', () => {
  const store = new ContentAddressableStore();
  const utilHash = store.put('util contents');
  const srcTreeHash = store.putTree([{ name: 'util.js', hash: utilHash, type: 'blob' }]);
  const rootTreeHash = store.putTree([{ name: 'src', hash: srcTreeHash, type: 'tree' }]);
  assert.deepStrictEqual(store.resolvePath(rootTreeHash, 'src'), { 'util.js': 'util contents' });
});

test('resolvePath throws when a path segment is missing', () => {
  const store = new ContentAddressableStore();
  const rootTreeHash = store.putTree([]);
  assert.throws(() => store.resolvePath(rootTreeHash, 'missing.txt'), RangeError);
});

test('resolvePath throws when treating a blob as a tree', () => {
  const store = new ContentAddressableStore();
  const blobHash = store.put('just a file');
  const rootTreeHash = store.putTree([{ name: 'file.txt', hash: blobHash, type: 'blob' }]);
  assert.throws(() => store.resolvePath(rootTreeHash, 'file.txt/nested'), RangeError);
});

test('getTree throws IntegrityError on tampered tree bytes', () => {
  const store = new ContentAddressableStore();
  const treeHash = store.putTree([]);
  store._objects.set(treeHash, Buffer.from('not valid json at all'));
  assert.throws(() => store.getTree(treeHash), IntegrityError);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
