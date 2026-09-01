'use strict';

class RoundRobinSelector {
  constructor() {
    this._counters = new Map();
  }

  pick(key, instances) {
    if (!instances || instances.length === 0) {
      return null;
    }
    const count = this._counters.get(key) || 0;
    const index = count % instances.length;
    this._counters.set(key, count + 1);
    return instances[index];
  }

  reset(key) {
    this._counters.delete(key);
  }
}

function pickRandom(instances, rng) {
  if (!instances || instances.length === 0) {
    return null;
  }
  const random = typeof rng === 'function' ? rng : Math.random;
  const index = Math.floor(random() * instances.length);
  const bounded = index >= instances.length ? instances.length - 1 : index;
  return instances[bounded];
}

function pickWeighted(instances, options = {}) {
  if (!instances || instances.length === 0) {
    return null;
  }
  const weightKey = options.weightKey || 'weight';
  const random = typeof options.rng === 'function' ? options.rng : Math.random;
  const weights = instances.map((instance) => {
    const raw = instance.meta ? instance.meta[weightKey] : undefined;
    const value = Number.isFinite(raw) && raw > 0 ? raw : 1;
    return value;
  });
  const total = weights.reduce((sum, w) => sum + w, 0);
  let target = random() * total;
  for (let i = 0; i < instances.length; i += 1) {
    target -= weights[i];
    if (target <= 0) {
      return instances[i];
    }
  }
  return instances[instances.length - 1];
}

function filterByTags(instances, tags) {
  const keys = Object.keys(tags || {});
  if (keys.length === 0) {
    return instances;
  }
  return instances.filter((instance) => {
    return keys.every((key) => instance.meta && instance.meta[key] === tags[key]);
  });
}

module.exports = { RoundRobinSelector, pickRandom, pickWeighted, filterByTags };
