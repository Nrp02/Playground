'use strict';

const assert = require('assert');
const { LockStoreNode } = require('./lockStore.js');

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

test('acquire succeeds when the resource is free', () => {
  const node = new LockStoreNode('n1');
  const result = node.acquire('res-a', 'client-1', 1000, 0);
  assert.strictEqual(result.success, true);
});

test('acquire fails when resource is already held by another client', () => {
  const node = new LockStoreNode('n1');
  node.acquire('res-a', 'client-1', 1000, 0);
  const result = node.acquire('res-a', 'client-2', 1000, 100);
  assert.strictEqual(result.success, false);
  assert.strictEqual(result.reason, 'held');
});

test('acquire succeeds for the same client that already holds it', () => {
  const node = new LockStoreNode('n1');
  node.acquire('res-a', 'client-1', 1000, 0);
  const result = node.acquire('res-a', 'client-1', 1000, 100);
  assert.strictEqual(result.success, true);
});

test('acquire succeeds for a different client once the lock has expired', () => {
  const node = new LockStoreNode('n1');
  node.acquire('res-a', 'client-1', 1000, 0);
  const result = node.acquire('res-a', 'client-2', 1000, 1500);
  assert.strictEqual(result.success, true);
});

test('release clears the lock when called by the holding client', () => {
  const node = new LockStoreNode('n1');
  node.acquire('res-a', 'client-1', 1000, 0);
  const result = node.release('res-a', 'client-1', 100);
  assert.strictEqual(result.success, true);
  assert.strictEqual(node.isLocked('res-a', 100), false);
});

test('release fails when called by a non-holding client', () => {
  const node = new LockStoreNode('n1');
  node.acquire('res-a', 'client-1', 1000, 0);
  const result = node.release('res-a', 'client-2', 100);
  assert.strictEqual(result.success, false);
  assert.strictEqual(result.reason, 'not_held');
  assert.strictEqual(node.isLocked('res-a', 100), true);
});

test('release fails once the lock has already expired', () => {
  const node = new LockStoreNode('n1');
  node.acquire('res-a', 'client-1', 1000, 0);
  const result = node.release('res-a', 'client-1', 2000);
  assert.strictEqual(result.success, false);
  assert.strictEqual(result.reason, 'not_held');
});

test('a node forced down always rejects acquire and release', () => {
  const node = new LockStoreNode('n1');
  node.setDown(true);
  const acquireResult = node.acquire('res-a', 'client-1', 1000, 0);
  assert.strictEqual(acquireResult.success, false);
  assert.strictEqual(acquireResult.reason, 'unavailable');

  node.setDown(false);
  node.acquire('res-a', 'client-1', 1000, 0);
  node.setDown(true);
  const releaseResult = node.release('res-a', 'client-1', 100);
  assert.strictEqual(releaseResult.success, false);
  assert.strictEqual(releaseResult.reason, 'unavailable');
});

test('failure rate uses the injectable rng deterministically', () => {
  const values = [0.1, 0.9];
  let index = 0;
  const rng = () => values[index++];
  const node = new LockStoreNode('n1', { failureRate: 0.5, rng });

  const first = node.acquire('res-a', 'client-1', 1000, 0);
  assert.strictEqual(first.success, false);
  assert.strictEqual(first.reason, 'unavailable');

  const second = node.acquire('res-a', 'client-1', 1000, 0);
  assert.strictEqual(second.success, true);
});

test('acquire and release report the configured latency', () => {
  const node = new LockStoreNode('n1', { latencyMs: 42 });
  const acquireResult = node.acquire('res-a', 'client-1', 1000, 0);
  assert.strictEqual(acquireResult.latencyMs, 42);
  const releaseResult = node.release('res-a', 'client-1', 100);
  assert.strictEqual(releaseResult.latencyMs, 42);
});

test('isLocked reflects expiry over time', () => {
  const node = new LockStoreNode('n1');
  node.acquire('res-a', 'client-1', 500, 0);
  assert.strictEqual(node.isLocked('res-a', 100), true);
  assert.strictEqual(node.isLocked('res-a', 600), false);
});

test('locks on different resources do not interfere with each other', () => {
  const node = new LockStoreNode('n1');
  assert.strictEqual(node.acquire('res-a', 'client-1', 1000, 0).success, true);
  assert.strictEqual(node.acquire('res-b', 'client-2', 1000, 0).success, true);
  assert.strictEqual(node.acquire('res-a', 'client-2', 1000, 0).success, false);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
