'use strict';

class FixedWindowLimiter {
  constructor(limit, windowMs) {
    if (limit <= 0 || windowMs <= 0) {
      throw new RangeError('limit and windowMs must be positive');
    }
    this.limit = limit;
    this.windowMs = windowMs;
    this._counters = new Map();
  }

  allow(key, now = Date.now()) {
    const windowStart = Math.floor(now / this.windowMs) * this.windowMs;
    let counter = this._counters.get(key);
    if (!counter || counter.windowStart !== windowStart) {
      counter = { windowStart, count: 0 };
      this._counters.set(key, counter);
    }

    if (counter.count < this.limit) {
      counter.count += 1;
      return true;
    }
    return false;
  }

  reset() {
    this._counters.clear();
  }
}

module.exports = { FixedWindowLimiter };
