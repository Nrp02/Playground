'use strict';

const assert = require('assert');
const { Cluster, runUntilConverged } = require('./election.js');

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

test('highest-id surviving node always wins the initial election', () => {
  const cluster = new Cluster([1, 2, 3, 4, 5]);
  const leader = runUntilConverged(cluster, 200);
  assert.ok(leader);
  assert.strictEqual(leader.id, 5);
  for (const node of cluster.nodes.values()) {
    assert.strictEqual(node.leaderId, 5);
  }
});

test('after the leader crashes the next-highest surviving node wins', () => {
  const cluster = new Cluster([1, 2, 3, 4, 5]);
  runUntilConverged(cluster, 200);
  cluster.crash(5);
  const leader = runUntilConverged(cluster, 200);
  assert.ok(leader);
  assert.strictEqual(leader.id, 4);
});

test('crashing successive leaders converges down the id chain', () => {
  const cluster = new Cluster([1, 2, 3, 4, 5]);
  runUntilConverged(cluster, 200);
  cluster.crash(5);
  runUntilConverged(cluster, 200);
  cluster.crash(4);
  const leader = runUntilConverged(cluster, 200);
  assert.ok(leader);
  assert.strictEqual(leader.id, 3);
});

test('multiple nodes starting elections at the same time still converge on one leader', () => {
  const cluster = new Cluster([2, 5, 7, 9, 13]);
  for (const id of [2, 5, 7]) {
    cluster.nodes.get(id).startElection(cluster.now, cluster.bus);
  }
  const leader = runUntilConverged(cluster, 200);
  assert.ok(leader);
  assert.strictEqual(leader.id, 13);
  for (const node of cluster.nodes.values()) {
    assert.strictEqual(node.leaderId, 13);
  }
});

test('election still converges with a large fixed message delay', () => {
  const cluster = new Cluster([1, 2, 3, 4, 5], {
    delay: 12,
    nodeOptions: { electionTimeout: 40, coordinatorTimeout: 50, heartbeatTimeout: 60 },
  });
  const leader = runUntilConverged(cluster, 400);
  assert.ok(leader);
  assert.strictEqual(leader.id, 5);
});

test('election converges when individual messages arrive with jittered delay', () => {
  const cluster = new Cluster([1, 2, 3, 4], {
    nodeOptions: { electionTimeout: 30, coordinatorTimeout: 30, heartbeatTimeout: 40 },
  });
  const originalSend = cluster.bus.send.bind(cluster.bus);
  cluster.bus.send = (src, dest, message, now, delay) => {
    const jitter = (src + dest) % 7;
    const baseDelay = delay === undefined ? cluster.bus.delay : delay;
    originalSend(src, dest, message, now, baseDelay + jitter);
  };
  const leader = runUntilConverged(cluster, 300);
  assert.ok(leader);
  assert.strictEqual(leader.id, 4);
});

test('a mid-election leader crash forces a coordinator-timeout restart that still converges', () => {
  const cluster = new Cluster([1, 2, 3], {
    delay: 3,
    nodeOptions: { heartbeatTimeout: 1000000, electionTimeout: 30, coordinatorTimeout: 5 },
  });
  const node1 = cluster.nodes.get(1);
  cluster.step();
  node1.startElection(cluster.now, cluster.bus);
  cluster.crash(3);

  const leader = runUntilConverged(cluster, 300);

  assert.ok(leader);
  assert.strictEqual(leader.id, 2);
  assert.ok(node1.log.some((line) => line.includes('restarting election')));
});

test('a node that suspects a live higher-id leader backs off once the coordinator answers', () => {
  const cluster = new Cluster([1, 2, 3]);
  const leader = runUntilConverged(cluster, 200);
  assert.strictEqual(leader.id, 3);
  const node1 = cluster.nodes.get(1);
  node1.startElection(cluster.now, cluster.bus);
  const stableLeader = runUntilConverged(cluster, 200);
  assert.ok(stableLeader);
  assert.strictEqual(stableLeader.id, 3);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
