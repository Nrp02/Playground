'use strict';

const { LruTtlCache } = require('./lruTtlCache.js');

function section(title) {
  console.log(`\n${title}`);
  console.log('-'.repeat(title.length));
}

function main() {
  section('Plain LRU eviction (no TTL pressure)');
  const lru = new LruTtlCache(3);
  let t = 0;
  lru.set('a', 1, Infinity, t);
  lru.set('b', 2, Infinity, t);
  lru.set('c', 3, Infinity, t);
  console.log('keys after a,b,c inserted:', lru.keys(t));
  lru.get('a', t);
  console.log('touched a, keys order:', lru.keys(t));
  lru.set('d', 4, Infinity, t);
  console.log('inserted d over capacity, keys:', lru.keys(t), '(b should be evicted, it was least recently used)');

  section('TTL expiry independent of recency');
  const ttlCache = new LruTtlCache(5, 1000);
  t = 0;
  ttlCache.set('short', 'expires-fast', 100, t);
  ttlCache.set('long', 'stays-fresh', 10000, t);
  t = 50;
  console.log(`t=${t}, get('short') before expiry:`, ttlCache.get('short', t));
  t = 200;
  console.log(`t=${t}, get('short') after expiry:`, ttlCache.get('short', t), '(TTL miss even though it was never evicted for capacity)');
  console.log(`t=${t}, get('long') still fresh:`, ttlCache.get('long', t));

  section('Interaction: an entry that is both stale and LRU-oldest');
  const combo = new LruTtlCache(2);
  t = 0;
  combo.set('stale', 'old-value', 50, t);
  combo.set('fresh', 'newer-value', 10000, t);
  t = 100;
  console.log(`t=${t}, "stale" has expired by TTL and is also the least recently used entry`);
  combo.set('incoming', 'newest-value', 10000, t);
  console.log('keys after inserting a third key over capacity:', combo.keys(t));
  console.log('get("stale") after eviction/expiry:', combo.get('stale', t));
  console.log('size:', combo.size);
}

main();
