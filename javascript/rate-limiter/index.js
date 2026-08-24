'use strict';

const { TokenBucketLimiter } = require('./tokenBucket.js');
const { SlidingWindowLimiter } = require('./slidingWindow.js');
const { FixedWindowLimiter } = require('./fixedWindow.js');

function createLimiter(algorithm, options) {
  switch (algorithm) {
    case 'token-bucket':
      return new TokenBucketLimiter(options.capacity, options.refillRatePerSecond);
    case 'sliding-window':
      return new SlidingWindowLimiter(options.limit, options.windowMs);
    case 'fixed-window':
      return new FixedWindowLimiter(options.limit, options.windowMs);
    default:
      throw new RangeError(`unknown rate limiter algorithm: ${algorithm}`);
  }
}

module.exports = {
  TokenBucketLimiter,
  SlidingWindowLimiter,
  FixedWindowLimiter,
  createLimiter,
};
