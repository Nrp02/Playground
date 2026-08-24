'use strict';

const assert = require('assert');
const { LockStoreNode } = require('./lockStore.js');
const { Redlock, FencingTokenCounter } = require('./redlock.js');

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

function buildNodes(count, options = {}) {
  const nodes = [];
  for (let i = 0; i < count; i += 1) {
    nodes.push(new LockStoreNode(`node-${i}`, options));
  }
  return nodes;
}

test('acquires the lock when all nodes are healthy and returns a fencing token', () => {
  const nodes = buildNodes(5);
  const redlock = new Redlock(nodes);
  const result = redlock.acquire('res-a', 'client-1', 10000, 0, 5000);
  assert.strictEqual(result.success, true);
  assert.strictEqual(result.acquiredNodeCount, 5);
  assert.strictEqual(typeof result.token, 'number');
  assert.strictEqual(result.token >= 1, true);
});

test('rejects a competing client once the majority is already held', () => {
  const nodes = buildNodes(5);
  const redlock = new Redlock(nodes);
  const first = redlock.acquire('res-a', 'client-1', 10000, 0, 5000);
  const second = redlock.acquire('res-a', 'client-2', 10000, 10, 5000);
  assert.strictEqual(first.success, true);
  assert.strictEqual(second.success, false);
  assert.strictEqual(second.reason, 'no_quorum');
});

test('two racing clients: exactly one wins the lock', () => {
  const nodes = buildNodes(5);
  const clientA = new Redlock(nodes);
  const clientB = new Redlock(nodes);
  const resultA = clientA.acquire('res-a', 'client-a', 10000, 0, 5000);
  const resultB = clientB.acquire('res-a', 'client-b', 10000, 0, 5000);
  assert.strictEqual(resultA.success !== resultB.success, true);
});

test('still reaches quorum with one node down out of five', () => {
  const nodes = buildNodes(5);
  nodes[2].setDown(true);
  const redlock = new Redlock(nodes);
  const result = redlock.acquire('res-a', 'client-1', 10000, 0, 5000);
  assert.strictEqual(result.success, true);
  assert.strictEqual(result.acquiredNodeCount, 4);
});

test('fails when three nodes out of five are down, breaking quorum', () => {
  const nodes = buildNodes(5);
  nodes[0].setDown(true);
  nodes[1].setDown(true);
  nodes[2].setDown(true);
  const redlock = new Redlock(nodes);
  const result = redlock.acquire('res-a', 'client-1', 10000, 0, 5000);
  assert.strictEqual(result.success, false);
  assert.strictEqual(result.reason, 'no_quorum');
});

test('a failed acquisition releases any partial locks it took', () => {
  const nodes = buildNodes(5);
  nodes[0].setDown(true);
  nodes[1].setDown(true);
  nodes[2].setDown(true);
  const redlock = new Redlock(nodes);
  redlock.acquire('res-a', 'client-1', 10000, 0, 5000);
  for (const node of nodes) {
    assert.strictEqual(node.isLocked('res-a', 10), false);
  }
});

test('fails when the accumulated latency exceeds the time budget', () => {
  const nodes = buildNodes(5, { latencyMs: 200 });
  const redlock = new Redlock(nodes);
  const result = redlock.acquire('res-a', 'client-1', 10000, 0, 100);
  assert.strictEqual(result.success, false);
  assert.strictEqual(result.reason, 'time_budget_exceeded');
});

test('release on the redlock client releases the lock on every node', () => {
  const nodes = buildNodes(5);
  const redlock = new Redlock(nodes);
  redlock.acquire('res-a', 'client-1', 10000, 0, 5000);
  const releasedCount = redlock.release('res-a', 'client-1', 100);
  assert.strictEqual(releasedCount, 5);
  for (const node of nodes) {
    assert.strictEqual(node.isLocked('res-a', 100), false);
  }
});

test('a client can re-acquire the same resource after releasing it', () => {
  const nodes = buildNodes(5);
  const redlock = new Redlock(nodes);
  redlock.acquire('res-a', 'client-1', 10000, 0, 5000);
  redlock.release('res-a', 'client-1', 100);
  const result = redlock.acquire('res-a', 'client-2', 10000, 200, 5000);
  assert.strictEqual(result.success, true);
});

test('fencing tokens strictly increase across successive successful grants', () => {
  const nodes = buildNodes(5);
  const redlock = new Redlock(nodes);
  const first = redlock.acquire('res-a', 'client-1', 1000, 0, 5000);
  redlock.release('res-a', 'client-1', 100);
  const second = redlock.acquire('res-a', 'client-2', 1000, 200, 5000);
  assert.strictEqual(second.token > first.token, true);
});

test('a shared token counter hands out sequential tokens across two clients', () => {
  const nodes = buildNodes(5);
  const tokenCounter = new FencingTokenCounter();
  const clientA = new Redlock(nodes, { tokenCounter });
  const clientB = new Redlock(nodes, { tokenCounter });
  const first = clientA.acquire('res-a', 'client-a', 1000, 0, 5000);
  clientA.release('res-a', 'client-a', 100);
  const second = clientB.acquire('res-a', 'client-b', 1000, 200, 5000);
  assert.strictEqual(first.token, 1);
  assert.strictEqual(second.token, 2);
});

test('constructor rejects an empty node list', () => {
  assert.throws(() => new Redlock([]), RangeError);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
