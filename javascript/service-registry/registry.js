'use strict';

const { RoundRobinSelector, pickRandom, pickWeighted, filterByTags } = require('./selection.js');

class ServiceRegistry {
  constructor(options = {}) {
    this._clock = typeof options.clock === 'function' ? options.clock : () => Date.now();
    this._failureThreshold = options.failureThreshold || 3;
    this._successThreshold = options.successThreshold || 2;
    this._services = new Map();
    this._leases = new Map();
    this._watchers = new Map();
    this._indices = new Map();
    this._leaseCounter = 0;
    this._roundRobin = new RoundRobinSelector();
  }

  register(opts) {
    const { serviceName, instanceId, address, port, meta, ttlMs, check } = opts || {};
    if (!serviceName || typeof serviceName !== 'string') {
      throw new TypeError('serviceName is required');
    }
    if (!instanceId || typeof instanceId !== 'string') {
      throw new TypeError('instanceId is required');
    }
    if (!address || typeof address !== 'string') {
      throw new TypeError('address is required');
    }
    if (!Number.isFinite(port) || port <= 0) {
      throw new RangeError('port must be a positive number');
    }
    if (!Number.isFinite(ttlMs) || ttlMs <= 0) {
      throw new RangeError('ttlMs must be a positive number');
    }

    const now = this._clock();
    let svcMap = this._services.get(serviceName);
    if (!svcMap) {
      svcMap = new Map();
      this._services.set(serviceName, svcMap);
    }

    const existing = svcMap.get(instanceId);
    if (existing) {
      this._leases.delete(existing.leaseId);
    }

    this._leaseCounter += 1;
    const leaseId = `lease-${this._leaseCounter}`;
    const instance = {
      serviceName,
      instanceId,
      address,
      port,
      meta: meta ? Object.assign({}, meta) : {},
      ttlMs,
      expiresAt: now + ttlMs,
      leaseId,
      health: 'passing',
      consecutiveFailures: 0,
      consecutiveSuccesses: 0,
      check: typeof check === 'function' ? check : null,
    };

    svcMap.set(instanceId, instance);
    this._leases.set(leaseId, { serviceName, instanceId });
    this._notify(serviceName);
    return { leaseId, serviceName, instanceId };
  }

  renew(leaseId) {
    const ref = this._leases.get(leaseId);
    if (!ref) {
      return false;
    }
    const svcMap = this._services.get(ref.serviceName);
    const instance = svcMap ? svcMap.get(ref.instanceId) : null;
    if (!instance) {
      this._leases.delete(leaseId);
      return false;
    }
    const now = this._clock();
    if (instance.expiresAt <= now) {
      return false;
    }
    instance.expiresAt = now + instance.ttlMs;
    return true;
  }

  deregister(serviceName, instanceId) {
    const svcMap = this._services.get(serviceName);
    if (!svcMap || !svcMap.has(instanceId)) {
      return false;
    }
    const instance = svcMap.get(instanceId);
    svcMap.delete(instanceId);
    this._leases.delete(instance.leaseId);
    this._notify(serviceName);
    return true;
  }

  sweep() {
    return this.tick(this._clock());
  }

  tick(nowMs) {
    const now = Number.isFinite(nowMs) ? nowMs : this._clock();
    for (const [serviceName, svcMap] of this._services) {
      let changed = false;
      for (const [instanceId, instance] of svcMap) {
        if (instance.expiresAt <= now) {
          svcMap.delete(instanceId);
          this._leases.delete(instance.leaseId);
          changed = true;
        }
      }
      if (changed) {
        this._notify(serviceName);
      }
    }
  }

  reportCheckResult(serviceName, instanceId, ok) {
    const svcMap = this._services.get(serviceName);
    const instance = svcMap ? svcMap.get(instanceId) : null;
    if (!instance) {
      return false;
    }
    const previousHealth = instance.health;
    if (ok) {
      instance.consecutiveFailures = 0;
      instance.consecutiveSuccesses += 1;
      if (instance.health === 'critical') {
        if (instance.consecutiveSuccesses >= this._successThreshold) {
          instance.health = 'passing';
          instance.consecutiveSuccesses = 0;
        }
      } else {
        instance.health = 'passing';
        instance.consecutiveSuccesses = 0;
      }
    } else {
      instance.consecutiveSuccesses = 0;
      instance.consecutiveFailures += 1;
      if (instance.health !== 'critical') {
        if (instance.consecutiveFailures >= this._failureThreshold) {
          instance.health = 'critical';
        } else {
          instance.health = 'warning';
        }
      }
    }
    if (instance.health !== previousHealth) {
      this._notify(serviceName);
    }
    return true;
  }

  runHealthChecks() {
    for (const [serviceName, svcMap] of this._services) {
      for (const instance of svcMap.values()) {
        if (!instance.check) {
          continue;
        }
        let ok;
        try {
          ok = !!instance.check(this._snapshot(instance));
        } catch (err) {
          ok = false;
        }
        this.reportCheckResult(serviceName, instance.instanceId, ok);
      }
    }
  }

  watch(serviceName, cb) {
    let set = this._watchers.get(serviceName);
    if (!set) {
      set = new Set();
      this._watchers.set(serviceName, set);
    }
    set.add(cb);
    return () => {
      set.delete(cb);
    };
  }

  getInstances(serviceName) {
    const svcMap = this._services.get(serviceName);
    if (!svcMap) {
      return [];
    }
    return Array.from(svcMap.values()).map((instance) => this._snapshot(instance));
  }

  resolve(serviceName, options = {}) {
    const now = this._clock();
    const svcMap = this._services.get(serviceName);
    if (!svcMap) {
      return [];
    }
    let instances = Array.from(svcMap.values())
      .filter((instance) => instance.expiresAt > now && instance.health !== 'critical')
      .map((instance) => this._snapshot(instance));
    if (options.tags) {
      instances = filterByTags(instances, options.tags);
    }
    return instances;
  }

  select(serviceName, options = {}) {
    const instances = this.resolve(serviceName, { tags: options.tags });
    if (instances.length === 0) {
      return null;
    }
    const strategy = options.strategy || 'roundRobin';
    if (strategy === 'random') {
      return pickRandom(instances, options.rng);
    }
    if (strategy === 'weighted') {
      return pickWeighted(instances, { weightKey: options.weightKey, rng: options.rng });
    }
    return this._roundRobin.pick(serviceName, instances);
  }

  getIndex(serviceName) {
    return this._indices.get(serviceName) || 0;
  }

  _snapshot(instance) {
    return {
      serviceName: instance.serviceName,
      instanceId: instance.instanceId,
      address: instance.address,
      port: instance.port,
      meta: Object.assign({}, instance.meta),
      leaseId: instance.leaseId,
      health: instance.health,
      expiresAt: instance.expiresAt,
    };
  }

  _notify(serviceName) {
    const idx = (this._indices.get(serviceName) || 0) + 1;
    this._indices.set(serviceName, idx);
    const set = this._watchers.get(serviceName);
    if (!set || set.size === 0) {
      return;
    }
    const instances = this.getInstances(serviceName);
    for (const cb of Array.from(set)) {
      try {
        cb(instances, idx);
      } catch (err) {
        continue;
      }
    }
  }
}

module.exports = { ServiceRegistry };
