'use strict';

const { Cluster, runUntilConverged } = require('./election.js');

function describe(cluster) {
  for (const id of [...cluster.nodes.keys()].sort((a, b) => a - b)) {
    const node = cluster.nodes.get(id);
    console.log(`  node ${id}: alive=${node.alive} state=${node.state} leader=${node.leaderId}`);
  }
}

function main() {
  const cluster = new Cluster([1, 2, 3, 4, 5], { delay: 3 });

  console.log('=== initial election ===');
  const firstLeader = runUntilConverged(cluster, 200);
  console.log(`leader elected: node ${firstLeader ? firstLeader.id : null}`);
  describe(cluster);

  console.log('\n=== leader crashes ===');
  cluster.crash(firstLeader.id);
  console.log(`node ${firstLeader.id} crashed`);

  console.log('\n=== new election, with jittered message delay ===');
  const originalSend = cluster.bus.send.bind(cluster.bus);
  cluster.bus.send = (src, dest, message, now, delay) => {
    const jitter = (src * 3 + dest) % 5;
    const baseDelay = delay === undefined ? cluster.bus.delay : delay;
    originalSend(src, dest, message, now, baseDelay + jitter);
  };
  const secondLeader = runUntilConverged(cluster, 300);
  console.log(`leader elected: node ${secondLeader ? secondLeader.id : null}`);
  describe(cluster);

  console.log('\n=== crashed node rejoins ===');
  cluster.revive(firstLeader.id);
  cluster.run(60);
  describe(cluster);

  return 0;
}

process.exit(main());
