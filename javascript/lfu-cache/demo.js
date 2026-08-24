'use strict';

const { LFUCache } = require('./lfuCache.js');

function currentKeys(cache, universe) {
  return universe.filter((key) => cache.has(key));
}

function describeState(cache, universe) {
  return currentKeys(cache, universe)
    .map((key) => `${key}(f=${cache.peekFrequency(key)})`)
    .join(', ');
}

function runStep(cache, universe, label, action) {
  const before = new Set(currentKeys(cache, universe));
  const result = action();
  const after = new Set(currentKeys(cache, universe));
  const evicted = [...before].filter((key) => !after.has(key));

  let line = `${label.padEnd(24)} -> state: [${describeState(cache, universe)}]`;
  if (evicted.length > 0) {
    line += `  EVICTED: ${evicted.join(', ')}`;
  }
  if (result !== undefined) {
    line += `  returned: ${result}`;
  }
  console.log(line);
}

function main() {
  const cache = new LFUCache(3);
  const universe = ['a', 'b', 'c', 'd', 'e'];

  console.log('capacity = 3\n');

  runStep(cache, universe, "put('a', 1)", () => cache.put('a', 1));
  runStep(cache, universe, "put('b', 2)", () => cache.put('b', 2));
  runStep(cache, universe, "put('c', 3)", () => cache.put('c', 3));
  runStep(cache, universe, "get('a')", () => cache.get('a'));
  runStep(cache, universe, "get('a')", () => cache.get('a'));
  runStep(cache, universe, "get('b')", () => cache.get('b'));

  console.log('\nfrequencies now: a=3, b=2, c=1 -> c is the least frequently used, so it evicts next\n');

  runStep(cache, universe, "put('d', 4)", () => cache.put('d', 4));

  console.log('\nfrequencies now: a=3, b=2, d=1 -> tie-break time: bump b and d to freq 2 each\n');

  runStep(cache, universe, "get('d')", () => cache.get('d'));

  console.log('\nnow a=3, b=2, d=2 -> b and d tie at freq 2; b was touched least recently among them\n');

  runStep(cache, universe, "put('e', 5)", () => cache.put('e', 5));

  console.log('\nfinal contents:');
  runStep(cache, universe, "(no-op)", () => {});
}

main();
