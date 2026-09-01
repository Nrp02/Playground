'use strict';

const assert = require('assert');
const { ServiceRegistry } = require('./registry.js');

let passCount = 0;
let failCount = 0;

function test(name, fn) {
  try {
    fn();
    passCount++;
    console.log(`  PASS  ${name}`);
  } catch (err) {
    failCount++;
    console.log(`  FAIL  ${name}`);
    console.log(`        ${err.message}`);
  }
}

function makeClock(start) {
  const state = { now: start };
  const clock = () => state.now;
  clock.set = (value) => {
    state.now = value;
  };
  return clock;
}

test('register returns a lease and instance is resolvable', () => {
  const clock = makeClock(0);
  const registry = new ServiceRegistry({ clock });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000 });
  const instances = registry.resolve('web');
  assert.strictEqual(instances.length, 1);
  assert.strictEqual(instances[0].instanceId, 'a');
});

test('lease expires at exactly the right tick, not before, not after', () => {
  const clock = makeClock(0);
  const registry = new ServiceRegistry({ clock });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000 });
  clock.set(999);
  registry.tick(999);
  assert.strictEqual(registry.resolve('web').length, 1, 'should still be present at 999');
  clock.set(1000);
  registry.tick(1000);
  assert.strictEqual(registry.resolve('web').length, 0, 'should be expired at exactly 1000');
});

test('renew extends the lease past its original expiry', () => {
  const clock = makeClock(0);
  const registry = new ServiceRegistry({ clock });
  const lease = registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000 });
  clock.set(900);
  assert.strictEqual(registry.renew(lease.leaseId), true);
  clock.set(1000);
  registry.tick(1000);
  assert.strictEqual(registry.resolve('web').length, 1, 'renewed instance should survive original expiry');
  clock.set(1899);
  registry.tick(1899);
  assert.strictEqual(registry.resolve('web').length, 1, 'renewed instance should still be present just before its new expiry');
});

test('renew of an unknown lease fails', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0) });
  assert.strictEqual(registry.renew('lease-nope'), false);
});

test('renew of an expired lease fails', () => {
  const clock = makeClock(0);
  const registry = new ServiceRegistry({ clock });
  const lease = registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000 });
  clock.set(1000);
  registry.tick(1000);
  assert.strictEqual(registry.renew(lease.leaseId), false);
});

test('deregister removes the instance and returns true, false when absent', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0) });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000 });
  assert.strictEqual(registry.deregister('web', 'a'), true);
  assert.strictEqual(registry.resolve('web').length, 0);
  assert.strictEqual(registry.deregister('web', 'a'), false);
});

test('health hysteresis: takes threshold consecutive failures to go critical', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0), failureThreshold: 3, successThreshold: 2 });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 100000 });
  registry.reportCheckResult('web', 'a', false);
  assert.strictEqual(registry.resolve('web').length, 1, 'one failure should not evict yet');
  registry.reportCheckResult('web', 'a', false);
  assert.strictEqual(registry.resolve('web').length, 1, 'two failures should not evict yet');
  registry.reportCheckResult('web', 'a', false);
  assert.strictEqual(registry.resolve('web').length, 0, 'three failures should evict');
});

test('health hysteresis: takes threshold consecutive successes to return to passing', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0), failureThreshold: 2, successThreshold: 2 });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 100000 });
  registry.reportCheckResult('web', 'a', false);
  registry.reportCheckResult('web', 'a', false);
  assert.strictEqual(registry.resolve('web').length, 0);
  registry.reportCheckResult('web', 'a', true);
  assert.strictEqual(registry.resolve('web').length, 0, 'one success should not restore yet');
  registry.reportCheckResult('web', 'a', true);
  assert.strictEqual(registry.resolve('web').length, 1, 'two successes should restore');
});

test('watch fires exactly once per real change and not on no-op updates', () => {
  const clock = makeClock(0);
  const registry = new ServiceRegistry({ clock, failureThreshold: 2 });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 100000 });
  let fireCount = 0;
  registry.watch('web', () => {
    fireCount += 1;
  });
  assert.strictEqual(fireCount, 0);
  registry.register({ serviceName: 'web', instanceId: 'b', address: '10.0.0.2', port: 80, ttlMs: 100000 });
  assert.strictEqual(fireCount, 1);
  registry.reportCheckResult('web', 'a', true);
  assert.strictEqual(fireCount, 1, 'success while already passing is a no-op');
  registry.reportCheckResult('web', 'a', false);
  assert.strictEqual(fireCount, 2, 'first failure moves to warning, a real change');
  registry.reportCheckResult('web', 'a', false);
  assert.strictEqual(fireCount, 3, 'second failure moves to critical');
  registry.deregister('web', 'b');
  assert.strictEqual(fireCount, 4);
});

test('unsubscribe stops delivery', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0) });
  let fireCount = 0;
  const unsubscribe = registry.watch('web', () => {
    fireCount += 1;
  });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000 });
  assert.strictEqual(fireCount, 1);
  unsubscribe();
  registry.register({ serviceName: 'web', instanceId: 'b', address: '10.0.0.2', port: 80, ttlMs: 1000 });
  assert.strictEqual(fireCount, 1);
});

test('a throwing watcher does not block delivery to others', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0) });
  let secondFired = false;
  registry.watch('web', () => {
    throw new Error('boom');
  });
  registry.watch('web', () => {
    secondFired = true;
  });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000 });
  assert.strictEqual(secondFired, true);
});

test('watch fires on expiry via sweep', () => {
  const clock = makeClock(0);
  const registry = new ServiceRegistry({ clock });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000 });
  let fireCount = 0;
  registry.watch('web', () => {
    fireCount += 1;
  });
  clock.set(500);
  registry.sweep();
  assert.strictEqual(fireCount, 0, 'not expired yet, sweep is a no-op');
  clock.set(1000);
  registry.sweep();
  assert.strictEqual(fireCount, 1);
});

test('watch index is monotonically increasing per service', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0) });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000 });
  assert.strictEqual(registry.getIndex('web'), 1);
  registry.register({ serviceName: 'web', instanceId: 'b', address: '10.0.0.2', port: 80, ttlMs: 1000 });
  assert.strictEqual(registry.getIndex('web'), 2);
});

test('resolve excludes unhealthy and expired instances', () => {
  const clock = makeClock(0);
  const registry = new ServiceRegistry({ clock, failureThreshold: 1 });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000 });
  registry.register({ serviceName: 'web', instanceId: 'b', address: '10.0.0.2', port: 80, ttlMs: 1000 });
  registry.reportCheckResult('web', 'a', false);
  clock.set(1000);
  registry.tick(1000);
  assert.strictEqual(registry.resolve('web').length, 0);
});

test('meta-tag filtering', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0) });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000, meta: { version: 'v1', zone: 'us' } });
  registry.register({ serviceName: 'web', instanceId: 'b', address: '10.0.0.2', port: 80, ttlMs: 1000, meta: { version: 'v2', zone: 'us' } });
  const v1 = registry.resolve('web', { tags: { version: 'v1' } });
  assert.strictEqual(v1.length, 1);
  assert.strictEqual(v1[0].instanceId, 'a');
  const usOnly = registry.resolve('web', { tags: { zone: 'us' } });
  assert.strictEqual(usOnly.length, 2);
  const none = registry.resolve('web', { tags: { zone: 'eu' } });
  assert.strictEqual(none.length, 0);
});

test('empty-service resolve returns an empty array', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0) });
  assert.deepStrictEqual(registry.resolve('missing'), []);
});

test('all-unhealthy service resolves to empty array', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0), failureThreshold: 1 });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000 });
  registry.reportCheckResult('web', 'a', false);
  assert.deepStrictEqual(registry.resolve('web'), []);
  assert.strictEqual(registry.select('web'), null);
});

test('round robin is fair and stable across instance-set changes', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0) });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000 });
  registry.register({ serviceName: 'web', instanceId: 'b', address: '10.0.0.2', port: 80, ttlMs: 1000 });
  const first = registry.select('web').instanceId;
  const second = registry.select('web').instanceId;
  assert.notStrictEqual(first, second);
  const third = registry.select('web').instanceId;
  assert.strictEqual(third, first);
  registry.register({ serviceName: 'web', instanceId: 'c', address: '10.0.0.3', port: 80, ttlMs: 1000 });
  const picks = [registry.select('web').instanceId, registry.select('web').instanceId, registry.select('web').instanceId];
  assert.ok(picks.includes('c'));
});

test('weighted distribution is roughly proportional to weight over many samples', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0) });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000, meta: { weight: 1 } });
  registry.register({ serviceName: 'web', instanceId: 'b', address: '10.0.0.2', port: 80, ttlMs: 1000, meta: { weight: 4 } });
  const counts = { a: 0, b: 0 };
  let seed = 1;
  const rng = () => {
    seed = (seed * 9301 + 49297) % 233280;
    return seed / 233280;
  };
  for (let i = 0; i < 5000; i += 1) {
    const picked = registry.select('web', { strategy: 'weighted', rng });
    counts[picked.instanceId] += 1;
  }
  const ratio = counts.b / counts.a;
  assert.ok(ratio > 2.5 && ratio < 6, `expected roughly 4:1 skew toward b, got ratio ${ratio}`);
});

test('random strategy always returns one of the healthy instances', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0) });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 1000 });
  registry.register({ serviceName: 'web', instanceId: 'b', address: '10.0.0.2', port: 80, ttlMs: 1000 });
  for (let i = 0; i < 20; i += 1) {
    const picked = registry.select('web', { strategy: 'random' });
    assert.ok(['a', 'b'].includes(picked.instanceId));
  }
});

test('runHealthChecks drives health state from a pluggable check function', () => {
  const clock = makeClock(0);
  const registry = new ServiceRegistry({ clock, failureThreshold: 2 });
  let healthy = true;
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 100000, check: () => healthy });
  registry.runHealthChecks();
  assert.strictEqual(registry.resolve('web').length, 1);
  healthy = false;
  registry.runHealthChecks();
  registry.runHealthChecks();
  assert.strictEqual(registry.resolve('web').length, 0);
});

test('a throwing check function is treated as a failure, not a crash', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0), failureThreshold: 1 });
  registry.register({ serviceName: 'web', instanceId: 'a', address: '10.0.0.1', port: 80, ttlMs: 100000, check: () => { throw new Error('boom'); } });
  registry.runHealthChecks();
  assert.strictEqual(registry.resolve('web').length, 0);
});

test('register validates required fields', () => {
  const registry = new ServiceRegistry({ clock: makeClock(0) });
  assert.throws(() => registry.register({ instanceId: 'a', address: 'x', port: 1, ttlMs: 1 }), TypeError);
  assert.throws(() => registry.register({ serviceName: 's', instanceId: 'a', address: 'x', port: 0, ttlMs: 1 }), RangeError);
  assert.throws(() => registry.register({ serviceName: 's', instanceId: 'a', address: 'x', port: 1, ttlMs: 0 }), RangeError);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
