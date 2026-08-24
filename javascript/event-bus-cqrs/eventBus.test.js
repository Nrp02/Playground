'use strict';

const assert = require('assert');
const { EventBus } = require('./eventBus.js');

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

test('publish calls handlers subscribed to the matching event type', () => {
  const bus = new EventBus();
  const received = [];
  bus.subscribe('Foo', (event) => received.push(event));
  bus.publish({ type: 'Foo', value: 1 });
  bus.publish({ type: 'Bar', value: 2 });
  assert.deepStrictEqual(received, [{ type: 'Foo', value: 1 }]);
});

test('wildcard subscribers receive every event', () => {
  const bus = new EventBus();
  const received = [];
  bus.subscribe('*', (event) => received.push(event.type));
  bus.publish({ type: 'Foo' });
  bus.publish({ type: 'Bar' });
  assert.deepStrictEqual(received, ['Foo', 'Bar']);
});

test('unsubscribe stops further delivery', () => {
  const bus = new EventBus();
  const received = [];
  const unsubscribe = bus.subscribe('Foo', (event) => received.push(event));
  bus.publish({ type: 'Foo' });
  unsubscribe();
  bus.publish({ type: 'Foo' });
  assert.strictEqual(received.length, 1);
});

test('publishAll delivers events in order', () => {
  const bus = new EventBus();
  const received = [];
  bus.subscribe('*', (event) => received.push(event.n));
  bus.publishAll([{ type: 'X', n: 1 }, { type: 'X', n: 2 }, { type: 'X', n: 3 }]);
  assert.deepStrictEqual(received, [1, 2, 3]);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
