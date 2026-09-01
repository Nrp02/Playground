'use strict';

const assert = require('assert');
const { RoundRobinSelector, pickRandom, pickWeighted, filterByTags } = require('./selection.js');

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

test('RoundRobinSelector cycles through instances in order', () => {
  const selector = new RoundRobinSelector();
  const instances = [{ instanceId: 'a' }, { instanceId: 'b' }, { instanceId: 'c' }];
  const picks = [
    selector.pick('svc', instances).instanceId,
    selector.pick('svc', instances).instanceId,
    selector.pick('svc', instances).instanceId,
    selector.pick('svc', instances).instanceId,
  ];
  assert.deepStrictEqual(picks, ['a', 'b', 'c', 'a']);
});

test('RoundRobinSelector tracks separate counters per key', () => {
  const selector = new RoundRobinSelector();
  const instances = [{ instanceId: 'a' }, { instanceId: 'b' }];
  assert.strictEqual(selector.pick('svc1', instances).instanceId, 'a');
  assert.strictEqual(selector.pick('svc2', instances).instanceId, 'a');
  assert.strictEqual(selector.pick('svc1', instances).instanceId, 'b');
});

test('RoundRobinSelector returns null for an empty list', () => {
  const selector = new RoundRobinSelector();
  assert.strictEqual(selector.pick('svc', []), null);
});

test('RoundRobinSelector reset clears the counter for a key', () => {
  const selector = new RoundRobinSelector();
  const instances = [{ instanceId: 'a' }, { instanceId: 'b' }];
  selector.pick('svc', instances);
  selector.reset('svc');
  assert.strictEqual(selector.pick('svc', instances).instanceId, 'a');
});

test('pickRandom returns null for an empty list', () => {
  assert.strictEqual(pickRandom([], () => 0.5), null);
});

test('pickRandom respects an injected rng deterministically', () => {
  const instances = [{ instanceId: 'a' }, { instanceId: 'b' }, { instanceId: 'c' }];
  assert.strictEqual(pickRandom(instances, () => 0).instanceId, 'a');
  assert.strictEqual(pickRandom(instances, () => 0.99).instanceId, 'c');
});

test('pickWeighted defaults missing/invalid weight to 1', () => {
  const instances = [{ instanceId: 'a', meta: {} }, { instanceId: 'b', meta: { weight: -5 } }];
  const picked = pickWeighted(instances, { rng: () => 0.99 });
  assert.strictEqual(picked.instanceId, 'b');
});

test('pickWeighted distribution favors the heavier instance', () => {
  const instances = [{ instanceId: 'a', meta: { weight: 1 } }, { instanceId: 'b', meta: { weight: 9 } }];
  const counts = { a: 0, b: 0 };
  let seed = 42;
  const rng = () => {
    seed = (seed * 9301 + 49297) % 233280;
    return seed / 233280;
  };
  for (let i = 0; i < 4000; i += 1) {
    counts[pickWeighted(instances, { rng }).instanceId] += 1;
  }
  assert.ok(counts.b > counts.a * 5, `expected b to dominate, got a=${counts.a} b=${counts.b}`);
});

test('pickWeighted returns null for an empty list', () => {
  assert.strictEqual(pickWeighted([]), null);
});

test('filterByTags matches on all provided keys', () => {
  const instances = [
    { instanceId: 'a', meta: { version: 'v1', zone: 'us' } },
    { instanceId: 'b', meta: { version: 'v1', zone: 'eu' } },
    { instanceId: 'c', meta: { version: 'v2', zone: 'us' } },
  ];
  const result = filterByTags(instances, { version: 'v1', zone: 'us' });
  assert.strictEqual(result.length, 1);
  assert.strictEqual(result[0].instanceId, 'a');
});

test('filterByTags with no tags returns the original list', () => {
  const instances = [{ instanceId: 'a', meta: {} }];
  assert.strictEqual(filterByTags(instances, {}), instances);
  assert.strictEqual(filterByTags(instances, null), instances);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
