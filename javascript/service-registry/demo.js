'use strict';

const { ServiceRegistry } = require('./registry.js');

function makeClock(start) {
  const state = { now: start };
  const clock = () => state.now;
  clock.advance = (deltaMs) => {
    state.now += deltaMs;
    return state.now;
  };
  return clock;
}

const clock = makeClock(0);
const registry = new ServiceRegistry({ clock, failureThreshold: 3, successThreshold: 2 });

console.log('--- registering instances ---');
registry.register({ serviceName: 'orders', instanceId: 'orders-1', address: '10.0.0.1', port: 8080, ttlMs: 5000, meta: { weight: 1, version: 'v1' } });
registry.register({ serviceName: 'orders', instanceId: 'orders-2', address: '10.0.0.2', port: 8080, ttlMs: 5000, meta: { weight: 3, version: 'v1' } });
registry.register({ serviceName: 'orders', instanceId: 'orders-3', address: '10.0.0.3', port: 8080, ttlMs: 5000, meta: { weight: 1, version: 'v2' } });
registry.register({ serviceName: 'billing', instanceId: 'billing-1', address: '10.0.1.1', port: 9090, ttlMs: 5000 });

registry.watch('orders', (instances, index) => {
  const ids = instances.map((i) => i.instanceId).join(', ');
  console.log(`[watch orders#${index}] current healthy set: ${ids || '(empty)'}`);
});

console.log('\n--- round robin selection over orders ---');
for (let i = 0; i < 6; i += 1) {
  const picked = registry.select('orders');
  console.log(`pick ${i}: ${picked.instanceId}`);
}

console.log('\n--- orders-1 stops heartbeating, time advances past its ttl ---');
clock.advance(5001);
registry.sweep();
console.log('resolve(orders) after sweep:', registry.resolve('orders').map((i) => i.instanceId));

console.log('\n--- orders-2 flaps: fails below threshold then recovers ---');
registry.register({ serviceName: 'orders', instanceId: 'orders-2', address: '10.0.0.2', port: 8080, ttlMs: 60000, meta: { weight: 3, version: 'v1' } });
registry.reportCheckResult('orders', 'orders-2', false);
console.log('after 1 failure, still in rotation:', registry.resolve('orders').some((i) => i.instanceId === 'orders-2'));
registry.reportCheckResult('orders', 'orders-2', false);
registry.reportCheckResult('orders', 'orders-2', false);
console.log('after 3 failures, held out of rotation:', !registry.resolve('orders').some((i) => i.instanceId === 'orders-2'));
registry.reportCheckResult('orders', 'orders-2', true);
console.log('after 1 success, still held out:', !registry.resolve('orders').some((i) => i.instanceId === 'orders-2'));
registry.reportCheckResult('orders', 'orders-2', true);
console.log('after 2 successes, back in rotation:', registry.resolve('orders').some((i) => i.instanceId === 'orders-2'));

console.log('\n--- weighted selection over many picks ---');
registry.register({ serviceName: 'orders', instanceId: 'orders-4', address: '10.0.0.4', port: 8080, ttlMs: 60000, meta: { weight: 9 } });
const counts = {};
for (let i = 0; i < 2000; i += 1) {
  const picked = registry.select('orders', { strategy: 'weighted' });
  counts[picked.instanceId] = (counts[picked.instanceId] || 0) + 1;
}
console.log('weighted pick counts over 2000 samples:', counts);

console.log('\n--- meta-tag filtering ---');
console.log('v1-only instances:', registry.resolve('orders', { tags: { version: 'v1' } }).map((i) => i.instanceId));

console.log('\ndemo complete');
