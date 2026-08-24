'use strict';

class SlidingWindowLimiter {
  constructor(limit, windowMs) {
    if (limit <= 0 || windowMs <= 0) {
      throw new RangeError('limit and windowMs must be positive');
    }
    this.limit = limit;
    this.windowMs = windowMs;
    this._log = new Map();
  }

  allow(key, now = Date.now()) {
    let timestamps = this._log.get(key);
    if (!timestamps) {
      timestamps = [];
      this._log.set(key, timestamps);
    }

    const cutoff = now - this.windowMs;
    while (timestamps.length > 0 && timestamps[0] <= cutoff) {
      timestamps.shift();
    }

    if (timestamps.length < this.limit) {
      timestamps.push(now);
      return true;
    }
    return false;
  }

  reset() {
    this._log.clear();
  }
}

module.exports = { SlidingWindowLimiter };
