'use strict';

const { BullyNode } = require('./node.js');

class MessageBus {
  constructor(delay = 3) {
    this.delay = delay;
    this.queue = [];
  }

  send(src, dest, message, now, delay) {
    const actualDelay = delay === undefined ? this.delay : delay;
    this.queue.push({ deliverAt: now + actualDelay, src, dest, message });
  }

  deliverReady(now) {
    const ready = [];
    const remaining = [];
    for (const item of this.queue) {
      if (item.deliverAt <= now) {
        ready.push(item);
      } else {
        remaining.push(item);
      }
    }
    this.queue = remaining;
    return ready;
  }
}

class Cluster {
  constructor(nodeIds, options = {}) {
    this.now = 0;
    this.bus = new MessageBus(options.delay);
    this.nodes = new Map();
    for (const id of nodeIds) {
      const peerIds = nodeIds.filter((otherId) => otherId !== id);
      this.nodes.set(id, new BullyNode(id, peerIds, options.nodeOptions));
    }
  }

  step() {
    this.now += 1;
    for (const item of this.bus.deliverReady(this.now)) {
      const node = this.nodes.get(item.dest);
      if (node) node.handleMessage(this.now, this.bus, item.message);
    }
    for (const node of this.nodes.values()) {
      node.tick(this.now, this.bus);
    }
  }

  run(ticks) {
    for (let i = 0; i < ticks; i += 1) {
      this.step();
    }
  }

  crash(id) {
    const node = this.nodes.get(id);
    if (node) node.crash();
  }

  revive(id) {
    const node = this.nodes.get(id);
    if (node) node.revive(this.now);
  }

  aliveIds() {
    return [...this.nodes.values()]
      .filter((node) => node.alive)
      .map((node) => node.id)
      .sort((a, b) => a - b);
  }

  leader() {
    const aliveNodes = [...this.nodes.values()].filter((node) => node.alive);
    if (aliveNodes.length === 0) return null;
    const leaderIds = new Set(aliveNodes.map((node) => node.leaderId));
    if (leaderIds.size !== 1) return null;
    const [leaderId] = leaderIds;
    if (leaderId === null) return null;
    const leaderNode = this.nodes.get(leaderId);
    if (!leaderNode || !leaderNode.alive || leaderNode.leaderId !== leaderNode.id) return null;
    return leaderNode;
  }
}

function runUntilConverged(cluster, maxTicks) {
  for (let i = 0; i < maxTicks; i += 1) {
    cluster.step();
    const leader = cluster.leader();
    if (leader) return leader;
  }
  return null;
}

module.exports = { MessageBus, Cluster, runUntilConverged };
