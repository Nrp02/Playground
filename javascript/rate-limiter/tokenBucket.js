'use strict';

class TokenBucketLimiter {
  constructor(capacity, refillRatePerSecond) {
    if (capacity <= 0 || refillRatePerSecond <= 0) {
      throw new RangeError('capacity and refillRatePerSecond must be positive');
    }
    this.capacity = capacity;
    this.refillRatePerSecond = refillRatePerSecond;
    this._buckets = new Map();
  }

  _bucketFor(key, now) {
    let bucket = this._buckets.get(key);
    if (!bucket) {
      bucket = { tokens: this.capacity, lastRefill: now };
      this._buckets.set(key, bucket);
    }
    return bucket;
  }

  allow(key, now = Date.now()) {
    const bucket = this._bucketFor(key, now);
    const elapsedSeconds = Math.max(0, now - bucket.lastRefill) / 1000;
    bucket.tokens = Math.min(this.capacity, bucket.tokens + elapsedSeconds * this.refillRatePerSecond);
    bucket.lastRefill = now;

    if (bucket.tokens >= 1) {
      bucket.tokens -= 1;
      return true;
    }
    return false;
  }

  reset() {
    this._buckets.clear();
  }
}

module.exports = { TokenBucketLimiter };
