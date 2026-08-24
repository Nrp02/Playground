'use strict';

const assert = require('assert');
const { SlidingWindowLimiter } = require('./slidingWindow.js');

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

test('allows requests up to the limit within the window', () => {
  const limiter = new SlidingWindowLimiter(3, 1000);
  assert.strictEqual(limiter.allow('k', 0), true);
  assert.strictEqual(limiter.allow('k', 100), true);
  assert.strictEqual(limiter.allow('k', 200), true);
  assert.strictEqual(limiter.allow('k', 300), false);
});

test('old requests fall out of the window and free up capacity', () => {
  const limiter = new SlidingWindowLimiter(2, 1000);
  assert.strictEqual(limiter.allow('k', 0), true);
  assert.strictEqual(limiter.allow('k', 100), true);
  assert.strictEqual(limiter.allow('k', 200), false);
  assert.strictEqual(limiter.allow('k', 1001), true);
});

test('window is per key', () => {
  const limiter = new SlidingWindowLimiter(1, 1000);
  assert.strictEqual(limiter.allow('a', 0), true);
  assert.strictEqual(limiter.allow('b', 0), true);
  assert.strictEqual(limiter.allow('a', 0), false);
});

test('reset clears all logs', () => {
  const limiter = new SlidingWindowLimiter(1, 1000);
  limiter.allow('k', 0);
  assert.strictEqual(limiter.allow('k', 0), false);
  limiter.reset();
  assert.strictEqual(limiter.allow('k', 0), true);
});

test('constructor rejects non-positive parameters', () => {
  assert.throws(() => new SlidingWindowLimiter(0, 1000), RangeError);
  assert.throws(() => new SlidingWindowLimiter(1, 0), RangeError);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
