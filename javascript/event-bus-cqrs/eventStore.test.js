'use strict';

const assert = require('assert');
const { EventStore, ConcurrencyError } = require('./eventStore.js');

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

test('append assigns sequential versions within a stream', () => {
  const store = new EventStore();
  const [first] = store.append('s1', [{ type: 'A', payload: {} }]);
  const [second] = store.append('s1', [{ type: 'B', payload: {} }]);
  assert.strictEqual(first.version, 0);
  assert.strictEqual(second.version, 1);
});

test('getStream returns events in append order', () => {
  const store = new EventStore();
  store.append('s1', [{ type: 'A', payload: 1 }, { type: 'B', payload: 2 }]);
  const events = store.getStream('s1');
  assert.deepStrictEqual(events.map((e) => e.type), ['A', 'B']);
});

test('getStream on an unknown stream returns an empty array', () => {
  const store = new EventStore();
  assert.deepStrictEqual(store.getStream('missing'), []);
});

test('append with a stale expectedVersion throws ConcurrencyError', () => {
  const store = new EventStore();
  store.append('s1', [{ type: 'A', payload: {} }]);
  assert.throws(() => store.append('s1', [{ type: 'B', payload: {} }], 0), ConcurrencyError);
});

test('append with the correct expectedVersion succeeds', () => {
  const store = new EventStore();
  store.append('s1', [{ type: 'A', payload: {} }]);
  assert.doesNotThrow(() => store.append('s1', [{ type: 'B', payload: {} }], 1));
});

test('allEvents merges and sorts events across streams', () => {
  const store = new EventStore();
  store.append('s1', [{ type: 'A', payload: {}, timestamp: 10 }]);
  store.append('s2', [{ type: 'B', payload: {}, timestamp: 5 }]);
  const all = store.allEvents();
  assert.deepStrictEqual(all.map((e) => e.type), ['B', 'A']);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
