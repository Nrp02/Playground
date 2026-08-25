'use strict';

const assert = require('assert');
const { LruTtlCache } = require('./lruTtlCache.js');

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

test('constructor rejects non-positive capacity', () => {
  assert.throws(() => new LruTtlCache(0), RangeError);
  assert.throws(() => new LruTtlCache(-1), RangeError);
  assert.throws(() => new LruTtlCache(1.5), RangeError);
});

test('constructor rejects non-positive defaultTtlMs', () => {
  assert.throws(() => new LruTtlCache(2, 0), RangeError);
  assert.throws(() => new LruTtlCache(2, -100), RangeError);
});

test('set/get roundtrip returns stored value', () => {
  const cache = new LruTtlCache(2);
  cache.set('a', 1, Infinity, 0);
  assert.strictEqual(cache.get('a', 0), 1);
});

test('get on missing key returns undefined', () => {
  const cache = new LruTtlCache(2);
  assert.strictEqual(cache.get('missing', 0), undefined);
});

test('plain LRU eviction: inserting past capacity evicts least recently used', () => {
  const cache = new LruTtlCache(2);
  cache.set('a', 1, Infinity, 0);
  cache.set('b', 2, Infinity, 0);
  cache.set('c', 3, Infinity, 0);
  assert.strictEqual(cache.has('a', 0), false);
  assert.strictEqual(cache.has('b', 0), true);
  assert.strictEqual(cache.has('c', 0), true);
});

test('plain LRU eviction: get refreshes recency and saves a key from eviction', () => {
  const cache = new LruTtlCache(2);
  cache.set('a', 1, Infinity, 0);
  cache.set('b', 2, Infinity, 0);
  cache.get('a', 0);
  cache.set('c', 3, Infinity, 0);
  assert.strictEqual(cache.has('a', 0), true);
  assert.strictEqual(cache.has('b', 0), false);
  assert.strictEqual(cache.has('c', 0), true);
});

test('plain LRU eviction: overwriting an existing key refreshes recency without evicting', () => {
  const cache = new LruTtlCache(2);
  cache.set('a', 1, Infinity, 0);
  cache.set('b', 2, Infinity, 0);
  cache.set('a', 10, Infinity, 0);
  cache.set('c', 3, Infinity, 0);
  assert.strictEqual(cache.has('a', 0), true);
  assert.strictEqual(cache.get('a', 0), 10);
  assert.strictEqual(cache.has('b', 0), false);
});

test('TTL expiry: get treats an expired entry as a miss', () => {
  const cache = new LruTtlCache(3);
  cache.set('a', 1, 100, 0);
  assert.strictEqual(cache.get('a', 50), 1);
  assert.strictEqual(cache.get('a', 100), undefined);
});

test('TTL expiry: expired entry is evicted from the map, not just hidden', () => {
  const cache = new LruTtlCache(3);
  cache.set('a', 1, 100, 0);
  cache.get('a', 200);
  assert.strictEqual(cache.size, 0);
});

test('TTL expiry: is independent of recency, a recently touched entry still expires', () => {
  const cache = new LruTtlCache(3);
  cache.set('a', 1, 100, 0);
  cache.set('b', 2, Infinity, 0);
  cache.get('a', 10);
  cache.get('a', 20);
  cache.get('a', 30);
  assert.strictEqual(cache.get('a', 150), undefined, 'frequent recent access does not extend TTL');
});

test('TTL expiry: default TTL applies when set() omits an explicit ttl', () => {
  const cache = new LruTtlCache(3, 100);
  cache.set('a', 1, undefined, 0);
  assert.strictEqual(cache.get('a', 50), 1);
  assert.strictEqual(cache.get('a', 150), undefined);
});

test('TTL expiry: Infinity ttl never expires', () => {
  const cache = new LruTtlCache(3);
  cache.set('a', 1, Infinity, 0);
  assert.strictEqual(cache.get('a', 1e15), 1);
});

test('has() also treats an expired entry as absent and evicts it', () => {
  const cache = new LruTtlCache(3);
  cache.set('a', 1, 100, 0);
  assert.strictEqual(cache.has('a', 200), false);
  assert.strictEqual(cache.size, 0);
});

test('peek() reports freshness without mutating recency order', () => {
  const cache = new LruTtlCache(2);
  cache.set('a', 1, Infinity, 0);
  cache.set('b', 2, Infinity, 0);
  cache.peek('a', 0);
  cache.set('c', 3, Infinity, 0);
  assert.strictEqual(cache.has('a', 0), false, 'peek must not refresh recency the way get does');
  assert.strictEqual(cache.has('b', 0), true);
});

test('interaction: an entry that is both stale and LRU-oldest is gone either way', () => {
  const cache = new LruTtlCache(2);
  cache.set('stale', 'old', 50, 0);
  cache.set('fresh', 'new', 10000, 0);
  assert.strictEqual(cache.get('stale', 100), undefined, 'expired before any capacity pressure was applied');
  assert.strictEqual(cache.size, 1);
});

test('interaction: inserting past capacity when the oldest slot already expired does not double-evict', () => {
  const cache = new LruTtlCache(2);
  cache.set('stale', 'old', 50, 0);
  cache.set('fresh', 'new', 10000, 0);
  cache.set('incoming', 'newest', 10000, 100);
  assert.strictEqual(cache.has('stale', 100), false);
  assert.strictEqual(cache.has('fresh', 100), true);
  assert.strictEqual(cache.has('incoming', 100), true);
  assert.strictEqual(cache.size, 2);
});

test('interaction: a still-fresh oldest entry is evicted by capacity, not by TTL', () => {
  const cache = new LruTtlCache(2);
  cache.set('oldButFresh', 'a', 10000, 0);
  cache.set('b', 'b', 10000, 0);
  cache.set('c', 'c', 10000, 0);
  assert.strictEqual(cache.has('oldButFresh', 0), false);
  assert.strictEqual(cache.peek('oldButFresh', 0), undefined);
});

test('delete removes a key and returns whether it existed', () => {
  const cache = new LruTtlCache(2);
  cache.set('a', 1, Infinity, 0);
  assert.strictEqual(cache.delete('a'), true);
  assert.strictEqual(cache.delete('a'), false);
  assert.strictEqual(cache.has('a', 0), false);
});

test('clear empties the cache', () => {
  const cache = new LruTtlCache(2);
  cache.set('a', 1, Infinity, 0);
  cache.set('b', 2, Infinity, 0);
  cache.clear();
  assert.strictEqual(cache.size, 0);
});

test('keys() excludes expired entries but leaves them for lazy purge', () => {
  const cache = new LruTtlCache(3);
  cache.set('a', 1, 100, 0);
  cache.set('b', 2, Infinity, 0);
  assert.deepStrictEqual(cache.keys(200), ['b']);
});

test('purgeExpired removes all currently-expired entries and returns their keys', () => {
  const cache = new LruTtlCache(3);
  cache.set('a', 1, 100, 0);
  cache.set('b', 2, 100, 0);
  cache.set('c', 3, Infinity, 0);
  const purged = cache.purgeExpired(200);
  assert.deepStrictEqual(purged.sort(), ['a', 'b']);
  assert.strictEqual(cache.size, 1);
  assert.strictEqual(cache.has('c', 200), true);
});

test('set rejects non-positive explicit ttlMs', () => {
  const cache = new LruTtlCache(2);
  assert.throws(() => cache.set('a', 1, 0, 0), RangeError);
  assert.throws(() => cache.set('a', 1, -5, 0), RangeError);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
