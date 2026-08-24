'use strict';

const assert = require('assert');
const { EventStore } = require('./eventStore.js');
const { EventBus } = require('./eventBus.js');
const { replay, LiveProjection } = require('./projections.js');

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

function apply(balance, event) {
  if (event.type === 'Deposited') return balance + event.payload.amount;
  if (event.type === 'Withdrawn') return balance - event.payload.amount;
  return balance;
}

test('replay rebuilds state from an event store stream', () => {
  const store = new EventStore();
  store.append('a1', [
    { type: 'Deposited', payload: { amount: 100 } },
    { type: 'Withdrawn', payload: { amount: 30 } },
  ]);
  assert.strictEqual(replay(store, 'a1', 0, apply), 70);
});

test('replay on an empty stream returns the initial state', () => {
  const store = new EventStore();
  assert.strictEqual(replay(store, 'missing', 42, apply), 42);
});

test('LiveProjection seeds from history and updates on new bus events', () => {
  const store = new EventStore();
  const bus = new EventBus();
  store.append('a1', [{ type: 'Deposited', payload: { amount: 50 } }]);

  const projection = new LiveProjection(store, bus, 'a1', 0, apply);
  assert.strictEqual(projection.state, 50);

  const [event] = store.append('a1', [{ type: 'Deposited', payload: { amount: 25 } }], 1);
  bus.publish(event);
  assert.strictEqual(projection.state, 75);
});

test('LiveProjection ignores events from other streams', () => {
  const store = new EventStore();
  const bus = new EventBus();
  const projection = new LiveProjection(store, bus, 'a1', 0, apply);

  const [event] = store.append('a2', [{ type: 'Deposited', payload: { amount: 999 } }]);
  bus.publish(event);
  assert.strictEqual(projection.state, 0);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
