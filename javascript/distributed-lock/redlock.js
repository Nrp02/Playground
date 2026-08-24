'use strict';

class FencingTokenCounter {
  constructor(start = 0) {
    this._value = start;
  }

  next() {
    this._value += 1;
    return this._value;
  }

  current() {
    return this._value;
  }
}

class Redlock {
  constructor(nodes, options = {}) {
    if (!Array.isArray(nodes) || nodes.length === 0) {
      throw new RangeError('redlock requires at least one lock store node');
    }
    this.nodes = nodes;
    this.quorum = Math.floor(nodes.length / 2) + 1;
    this.tokenCounter = options.tokenCounter || new FencingTokenCounter();
    this.clockDriftFactor = options.clockDriftFactor || 0.01;
  }

  acquire(resource, clientId, ttlMs, now, timeBudgetMs = Math.floor(ttlMs / 2)) {
    const acquiredNodes = [];
    let elapsedMs = 0;

    for (const node of this.nodes) {
      const callNow = now + elapsedMs;
      const result = node.acquire(resource, clientId, ttlMs, callNow);
      elapsedMs += result.latencyMs;
      if (result.success) {
        acquiredNodes.push(node);
      }
      if (elapsedMs > timeBudgetMs) {
        break;
      }
    }

    const driftMs = Math.round(ttlMs * this.clockDriftFactor) + 2;
    const remainingTtlMs = ttlMs - elapsedMs - driftMs;
    const withinBudget = elapsedMs <= timeBudgetMs;
    const hasQuorum = acquiredNodes.length >= this.quorum;

    if (hasQuorum && withinBudget && remainingTtlMs > 0) {
      const token = this.tokenCounter.next();
      return {
        success: true,
        resource,
        clientId,
        token,
        ttlMs: remainingTtlMs,
        elapsedMs,
        acquiredNodeCount: acquiredNodes.length,
        quorum: this.quorum,
      };
    }

    const releaseNow = now + elapsedMs;
    for (const node of acquiredNodes) {
      node.release(resource, clientId, releaseNow);
    }

    let reason = 'no_quorum';
    if (!withinBudget) {
      reason = 'time_budget_exceeded';
    } else if (hasQuorum && remainingTtlMs <= 0) {
      reason = 'ttl_expired';
    }

    return {
      success: false,
      resource,
      clientId,
      reason,
      elapsedMs,
      acquiredNodeCount: acquiredNodes.length,
      quorum: this.quorum,
    };
  }

  release(resource, clientId, now) {
    let releasedCount = 0;
    for (const node of this.nodes) {
      const result = node.release(resource, clientId, now);
      if (result.success) {
        releasedCount += 1;
      }
    }
    return releasedCount;
  }
}

module.exports = { Redlock, FencingTokenCounter };
