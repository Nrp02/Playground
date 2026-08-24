'use strict';

const assert = require('assert');
const { LFUCache } = require('./lfuCache.js');

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

test('basic put and get roundtrip', () => {
  const cache = new LFUCache(2);
  cache.put('a', 1);
  cache.put('b', 2);
  assert.strictEqual(cache.get('a'), 1);
  assert.strictEqual(cache.get('b'), 2);
});

test('get on missing key returns -1', () => {
  const cache = new LFUCache(2);
  cache.put('a', 1);
  assert.strictEqual(cache.get('missing'), -1);
});

test('put on existing key updates value without evicting', () => {
  const cache = new LFUCache(2);
  cache.put('a', 1);
  cache.put('b', 2);
  cache.put('a', 100);
  assert.strictEqual(cache.get('a'), 100);
  assert.strictEqual(cache.get('b'), 2);
  assert.strictEqual(cache.size, 2);
});

test('evicts when inserting beyond capacity', () => {
  const cache = new LFUCache(2);
  cache.put('a', 1);
  cache.put('b', 2);
  cache.put('c', 3);
  assert.strictEqual(cache.has('a'), false);
  assert.strictEqual(cache.get('b'), 2);
  assert.strictEqual(cache.get('c'), 3);
  assert.strictEqual(cache.size, 2);
});

test('eviction prefers lower frequency over more recent access', () => {
  const cache = new LFUCache(2);
  cache.put('a', 1);
  cache.put('b', 2);
  cache.get('a');
  cache.get('a');
  cache.get('b');

  cache.put('c', 3);

  assert.strictEqual(cache.has('a'), true);
  assert.strictEqual(cache.has('b'), false);
  assert.strictEqual(cache.has('c'), true);
});

test('tie-breaks by least recently used within the same frequency', () => {
  const cache = new LFUCache(2);
  cache.put('a', 1);
  cache.put('b', 2);
  cache.get('a');
  cache.get('b');

  cache.put('c', 3);

  assert.strictEqual(cache.has('a'), false);
  assert.strictEqual(cache.has('b'), true);
  assert.strictEqual(cache.has('c'), true);
});

test('frequency increments on repeated get calls', () => {
  const cache = new LFUCache(3);
  cache.put('a', 1);
  assert.strictEqual(cache.peekFrequency('a'), 1);
  cache.get('a');
  assert.strictEqual(cache.peekFrequency('a'), 2);
  cache.get('a');
  assert.strictEqual(cache.peekFrequency('a'), 3);
});

test('minimum frequency tracking survives eviction of the only key at that frequency', () => {
  const cache = new LFUCache(2);
  cache.put('a', 1);
  cache.put('b', 2);
  cache.get('a');
  cache.get('a');
  cache.get('b');
  cache.get('b');
  cache.get('b');

  cache.put('c', 3);
  assert.strictEqual(cache.has('a'), false);

  cache.put('d', 4);
  assert.strictEqual(cache.has('c'), false);
  assert.strictEqual(cache.has('b'), true);
  assert.strictEqual(cache.has('d'), true);
});

test('capacity of zero never stores anything', () => {
  const cache = new LFUCache(0);
  cache.put('a', 1);
  assert.strictEqual(cache.get('a'), -1);
  assert.strictEqual(cache.size, 0);
});

test('updating an existing key resets its recency within its bumped frequency', () => {
  const cache = new LFUCache(2);
  cache.put('a', 1);
  cache.put('b', 2);
  cache.get('a');
  cache.get('b');
  cache.put('a', 999);

  cache.put('c', 3);

  assert.strictEqual(cache.has('b'), false);
  assert.strictEqual(cache.get('a'), 999);
  assert.strictEqual(cache.has('c'), true);
});

test('constructor rejects negative or non-integer capacity', () => {
  assert.throws(() => new LFUCache(-1), RangeError);
  assert.throws(() => new LFUCache(1.5), RangeError);
});

test('handles a long mixed sequence without losing invariants', () => {
  const cache = new LFUCache(3);
  cache.put('a', 1);
  cache.put('b', 2);
  cache.put('c', 3);
  cache.get('a');
  cache.get('a');
  cache.get('b');
  cache.put('d', 4);
  assert.strictEqual(cache.has('c'), false);
  cache.get('d');
  cache.get('d');
  cache.put('e', 5);
  assert.strictEqual(cache.has('b'), false);
  assert.strictEqual(cache.has('a'), true);
  assert.strictEqual(cache.has('d'), true);
  assert.strictEqual(cache.has('e'), true);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
