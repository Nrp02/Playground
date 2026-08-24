'use strict';

const assert = require('assert');
const { FixedWindowLimiter } = require('./fixedWindow.js');

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

test('allows requests up to the limit within a window', () => {
  const limiter = new FixedWindowLimiter(2, 1000);
  assert.strictEqual(limiter.allow('k', 0), true);
  assert.strictEqual(limiter.allow('k', 500), true);
  assert.strictEqual(limiter.allow('k', 900), false);
});

test('resets the counter at the next window boundary', () => {
  const limiter = new FixedWindowLimiter(1, 1000);
  assert.strictEqual(limiter.allow('k', 0), true);
  assert.strictEqual(limiter.allow('k', 999), false);
  assert.strictEqual(limiter.allow('k', 1000), true);
});

test('window is per key', () => {
  const limiter = new FixedWindowLimiter(1, 1000);
  assert.strictEqual(limiter.allow('a', 0), true);
  assert.strictEqual(limiter.allow('b', 0), true);
  assert.strictEqual(limiter.allow('a', 0), false);
});

test('reset clears all counters', () => {
  const limiter = new FixedWindowLimiter(1, 1000);
  limiter.allow('k', 0);
  assert.strictEqual(limiter.allow('k', 0), false);
  limiter.reset();
  assert.strictEqual(limiter.allow('k', 0), true);
});

test('constructor rejects non-positive parameters', () => {
  assert.throws(() => new FixedWindowLimiter(0, 1000), RangeError);
  assert.throws(() => new FixedWindowLimiter(1, 0), RangeError);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
