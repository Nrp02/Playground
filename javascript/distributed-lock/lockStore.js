'use strict';

class LockStoreNode {
  constructor(id, options = {}) {
    this.id = id;
    this.locks = new Map();
    this.latencyMs = options.latencyMs || 0;
    this.failureRate = options.failureRate || 0;
    this.down = options.down || false;
    this.rng = options.rng || Math.random;
  }

  setLatency(latencyMs) {
    this.latencyMs = latencyMs;
  }

  setFailureRate(failureRate) {
    this.failureRate = failureRate;
  }

  setDown(down) {
    this.down = down;
  }

  _isUnavailable() {
    if (this.down) {
      return true;
    }
    if (this.failureRate > 0 && this.rng() < this.failureRate) {
      return true;
    }
    return false;
  }

  acquire(resource, clientId, ttlMs, now) {
    const latencyMs = this.latencyMs;
    if (this._isUnavailable()) {
      return { success: false, reason: 'unavailable', latencyMs };
    }

    const existing = this.locks.get(resource);
    const isHeldByOther = existing && existing.expiresAt > now && existing.clientId !== clientId;
    if (isHeldByOther) {
      return { success: false, reason: 'held', latencyMs };
    }

    this.locks.set(resource, { clientId, expiresAt: now + ttlMs });
    return { success: true, latencyMs };
  }

  release(resource, clientId, now) {
    const latencyMs = this.latencyMs;
    if (this._isUnavailable()) {
      return { success: false, reason: 'unavailable', latencyMs };
    }

    const existing = this.locks.get(resource);
    const isHeldByClient = existing && existing.clientId === clientId && existing.expiresAt > now;
    if (!isHeldByClient) {
      return { success: false, reason: 'not_held', latencyMs };
    }

    this.locks.delete(resource);
    return { success: true, latencyMs };
  }

  isLocked(resource, now) {
    const existing = this.locks.get(resource);
    return Boolean(existing) && existing.expiresAt > now;
  }
}

module.exports = { LockStoreNode };
