'use strict';

const assert = require('assert');
const { TokenBucketLimiter } = require('./tokenBucket.js');

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

test('allows requests up to capacity then denies', () => {
  const limiter = new TokenBucketLimiter(3, 1);
  assert.strictEqual(limiter.allow('k', 0), true);
  assert.strictEqual(limiter.allow('k', 0), true);
  assert.strictEqual(limiter.allow('k', 0), true);
  assert.strictEqual(limiter.allow('k', 0), false);
});

test('refills over time at the configured rate', () => {
  const limiter = new TokenBucketLimiter(1, 1);
  assert.strictEqual(limiter.allow('k', 0), true);
  assert.strictEqual(limiter.allow('k', 500), false);
  assert.strictEqual(limiter.allow('k', 1000), true);
});

test('never exceeds bucket capacity after a long idle period', () => {
  const limiter = new TokenBucketLimiter(2, 1);
  limiter.allow('k', 0);
  assert.strictEqual(limiter.allow('k', 100000), true);
  assert.strictEqual(limiter.allow('k', 100000), true);
  assert.strictEqual(limiter.allow('k', 100000), false);
});

test('tracks separate buckets per key', () => {
  const limiter = new TokenBucketLimiter(1, 1);
  assert.strictEqual(limiter.allow('a', 0), true);
  assert.strictEqual(limiter.allow('b', 0), true);
  assert.strictEqual(limiter.allow('a', 0), false);
});

test('reset clears all bucket state', () => {
  const limiter = new TokenBucketLimiter(1, 1);
  limiter.allow('k', 0);
  assert.strictEqual(limiter.allow('k', 0), false);
  limiter.reset();
  assert.strictEqual(limiter.allow('k', 0), true);
});

test('constructor rejects non-positive parameters', () => {
  assert.throws(() => new TokenBucketLimiter(0, 1), RangeError);
  assert.throws(() => new TokenBucketLimiter(1, 0), RangeError);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
