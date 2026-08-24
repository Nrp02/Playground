'use strict';

const assert = require('assert');
const { TaskScheduler } = require('./scheduler.js');

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

test('new scheduler is empty', () => {
  const scheduler = new TaskScheduler();
  assert.strictEqual(scheduler.pending, 0);
  assert.strictEqual(scheduler.isEmpty(), true);
  assert.strictEqual(scheduler.peekNext(), null);
  assert.strictEqual(scheduler.runNext(), null);
});

test('runNext returns the highest-priority task first', () => {
  const scheduler = new TaskScheduler();
  scheduler.schedule('low', 10);
  scheduler.schedule('urgent', 1);
  scheduler.schedule('medium', 5);

  assert.strictEqual(scheduler.runNext().name, 'urgent');
  assert.strictEqual(scheduler.runNext().name, 'medium');
  assert.strictEqual(scheduler.runNext().name, 'low');
  assert.strictEqual(scheduler.runNext(), null);
});

test('equal priority tasks run in FIFO order', () => {
  const scheduler = new TaskScheduler();
  scheduler.schedule('first', 5);
  scheduler.schedule('second', 5);
  scheduler.schedule('third', 5);

  const order = scheduler.runAll().map((t) => t.name);
  assert.deepStrictEqual(order, ['first', 'second', 'third']);
});

test('schedule rejects a non-finite priority', () => {
  const scheduler = new TaskScheduler();
  assert.throws(() => scheduler.schedule('bad', NaN), TypeError);
  assert.throws(() => scheduler.schedule('bad', Infinity), TypeError);
});

test('peekNext does not remove the task', () => {
  const scheduler = new TaskScheduler();
  scheduler.schedule('only', 1);
  const peeked = scheduler.peekNext();
  assert.strictEqual(peeked.name, 'only');
  assert.strictEqual(scheduler.pending, 1);
});

test('runAll drains every scheduled task in priority order', () => {
  const scheduler = new TaskScheduler();
  scheduler.schedule('c', 3);
  scheduler.schedule('a', 1);
  scheduler.schedule('b', 2);

  const names = scheduler.runAll().map((t) => t.name);
  assert.deepStrictEqual(names, ['a', 'b', 'c']);
  assert.strictEqual(scheduler.isEmpty(), true);
});

test('executionLog records the order tasks actually ran in', () => {
  const scheduler = new TaskScheduler();
  scheduler.schedule('second', 2);
  scheduler.schedule('first', 1);
  scheduler.runNext();
  scheduler.runNext();
  assert.deepStrictEqual(scheduler.executionLog(), ['first', 'second']);
});

test('snapshot returns pending tasks in priority order without draining', () => {
  const scheduler = new TaskScheduler();
  scheduler.schedule('c', 3);
  scheduler.schedule('a', 1);
  scheduler.schedule('b', 2);

  const names = scheduler.snapshot().map((t) => t.name);
  assert.deepStrictEqual(names, ['a', 'b', 'c']);
  assert.strictEqual(scheduler.pending, 3);
});

test('schedule returns a stable, increasing task id', () => {
  const scheduler = new TaskScheduler();
  const idA = scheduler.schedule('a', 1);
  const idB = scheduler.schedule('b', 1);
  assert.ok(idB > idA);
});

console.log(`\n${passCount} passed, ${failCount} failed (${passCount + failCount} total)`);

if (failCount > 0) {
  process.exitCode = 1;
} else {
  console.log('All assertions passed.');
}
