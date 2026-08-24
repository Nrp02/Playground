'use strict';

const { LockStoreNode } = require('./lockStore.js');
const { Redlock, FencingTokenCounter } = require('./redlock.js');

class ProtectedResource {
  constructor(name) {
    this.name = name;
    this.highestToken = 0;
    this.value = null;
  }

  write(token, payload) {
    if (token <= this.highestToken) {
      return { accepted: false, reason: 'stale_fencing_token', highestToken: this.highestToken };
    }
    this.highestToken = token;
    this.value = payload;
    return { accepted: true, highestToken: this.highestToken };
  }
}

function buildCluster(size, options = {}) {
  const nodes = [];
  for (let i = 0; i < size; i += 1) {
    nodes.push(new LockStoreNode(`node-${i}`, options));
  }
  return nodes;
}

function scenarioOne() {
  console.log('--- scenario 1: two clients race for the same lock across 5 nodes ---');
  const nodes = buildCluster(5, { latencyMs: 5 });
  const tokenCounter = new FencingTokenCounter();
  const clientA = new Redlock(nodes, { tokenCounter });
  const clientB = new Redlock(nodes, { tokenCounter });

  const resultA = clientA.acquire('order-42', 'client-a', 10000, 0, 5000);
  const resultB = clientB.acquire('order-42', 'client-b', 10000, 25, 5000);

  console.log(`client-a acquire: success=${resultA.success} nodes=${resultA.acquiredNodeCount || 0} token=${resultA.token || '-'}`);
  console.log(`client-b acquire: success=${resultB.success} nodes=${resultB.acquiredNodeCount || 0} reason=${resultB.reason || '-'}`);
  console.log(`exactly one winner: ${resultA.success !== resultB.success}\n`);
}

function scenarioTwo() {
  console.log('--- scenario 2: one node is down but the lock still succeeds via majority ---');
  const nodes = buildCluster(5, { latencyMs: 5 });
  nodes[3].setDown(true);
  console.log(`node-3 is forced down; nodes up: ${nodes.filter((n) => !n.down).length}/${nodes.length}`);

  const redlock = new Redlock(nodes);
  const result = redlock.acquire('inventory-7', 'client-c', 10000, 0, 5000);
  console.log(`acquire: success=${result.success} nodes=${result.acquiredNodeCount} quorum=${result.quorum} token=${result.token}\n`);
}

function scenarioThree() {
  console.log('--- scenario 3: a stale fencing token gets rejected by a downstream check ---');
  const nodes = buildCluster(5, { latencyMs: 5 });
  const tokenCounter = new FencingTokenCounter();
  const resource = new ProtectedResource('inventory-7');

  const clientD = new Redlock(nodes, { tokenCounter });
  const grantD = clientD.acquire('inventory-7', 'client-d', 500, 0, 5000);
  console.log(`client-d acquires the lock, token=${grantD.token}`);

  const writeD1 = resource.write(grantD.token, 'reserved-by-d');
  console.log(`client-d writes with its token: accepted=${writeD1.accepted}`);

  console.log('client-d stalls (e.g. a long GC pause) past its own TTL...');
  const expiredAt = 0 + grantD.ttlMs + 600;

  const clientE = new Redlock(nodes, { tokenCounter });
  const grantE = clientE.acquire('inventory-7', 'client-e', 500, expiredAt, 5000);
  console.log(`client-e re-acquires the now-expired lock, token=${grantE.token}`);

  const writeE = resource.write(grantE.token, 'reserved-by-e');
  console.log(`client-e writes with its fresh token: accepted=${writeE.accepted}`);

  const staleWriteD = resource.write(grantD.token, 'reserved-by-d-again');
  console.log(`client-d retries its write using its stale token: accepted=${staleWriteD.accepted} reason=${staleWriteD.reason}`);
  console.log(`resource still holds client-e's value: ${resource.value === 'reserved-by-e'}\n`);
}

function main() {
  scenarioOne();
  scenarioTwo();
  scenarioThree();
}

main();
