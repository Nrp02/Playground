'use strict';

const { createLimiter } = require('./index.js');

function buildBurstyTimeline() {
  const timestamps = [];
  for (let i = 0; i < 15; i += 1) {
    timestamps.push(i * 10);
  }
  for (let i = 0; i < 15; i += 1) {
    timestamps.push(1200 + i * 200);
  }
  for (let i = 0; i < 15; i += 1) {
    timestamps.push(2500 + i * 10);
  }
  return timestamps;
}

function replay(limiter, timestamps) {
  const decisions = timestamps.map((now) => limiter.allow('client-a', now));
  const allowed = decisions.filter(Boolean).length;
  return { decisions, allowed, denied: decisions.length - allowed };
}

function summarize(name, result) {
  const pattern = result.decisions.map((allowed) => (allowed ? 'A' : 'D')).join('');
  console.log(`${name.padEnd(16)} allowed=${result.allowed} denied=${result.denied}  pattern=${pattern}`);
}

function main() {
  const timestamps = buildBurstyTimeline();
  console.log(`simulating ${timestamps.length} requests: a fast burst, a paced trickle, then another fast burst\n`);

  const limiters = [
    ['token-bucket', createLimiter('token-bucket', { capacity: 10, refillRatePerSecond: 5 })],
    ['sliding-window', createLimiter('sliding-window', { limit: 10, windowMs: 1000 })],
    ['fixed-window', createLimiter('fixed-window', { limit: 10, windowMs: 1000 })],
  ];

  for (const [name, limiter] of limiters) {
    summarize(name, replay(limiter, timestamps));
  }
}

main();
