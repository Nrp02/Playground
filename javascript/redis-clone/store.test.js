'use strict';

const assert = require('assert');
const { RedisStore, CommandHandler } = require('./store.js');

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

test('set and get round-trip a value', () => {
  const store = new RedisStore();
  store.set('k', 'v');
  assert.strictEqual(store.get('k'), 'v');
});

test('get on a missing key returns null', () => {
  const store = new RedisStore();
  assert.strictEqual(store.get('missing'), null);
});

test('del removes a key and reports whether it existed', () => {
  const store = new RedisStore();
  store.set('k', 'v');
  assert.strictEqual(store.del('k'), 1);
  assert.strictEqual(store.del('k'), 0);
  assert.strictEqual(store.get('k'), null);
});

test('expire sets a ttl that lazily evicts on get', () => {
  let now = 0;
  const store = new RedisStore(() => now);
  store.set('k', 'v');
  assert.strictEqual(store.expire('k', 10), 1);
  now = 5000;
  assert.strictEqual(store.get('k'), 'v');
  now = 11000;
  assert.strictEqual(store.get('k'), null);
});

test('expire on a missing key returns 0', () => {
  const store = new RedisStore();
  assert.strictEqual(store.expire('missing', 10), 0);
});

test('ttl reports -2 for missing keys and -1 for keys without expiry', () => {
  const store = new RedisStore();
  assert.strictEqual(store.ttl('missing'), -2);
  store.set('k', 'v');
  assert.strictEqual(store.ttl('k'), -1);
});

test('ttl reports remaining seconds after expire', () => {
  let now = 0;
  const store = new RedisStore(() => now);
  store.set('k', 'v');
  store.expire('k', 10);
  now = 4000;
  assert.strictEqual(store.ttl('k'), 6);
});

test('set clears any previously configured expiry', () => {
  let now = 0;
  const store = new RedisStore(() => now);
  store.set('k', 'v1');
  store.expire('k', 10);
  store.set('k', 'v2');
  assert.strictEqual(store.ttl('k'), -1);
  now = 100000;
  assert.strictEqual(store.get('k'), 'v2');
});

test('command handler PING replies PONG with no arguments', () => {
  const handler = new CommandHandler();
  assert.deepStrictEqual(handler.execute(['PING']), { type: 'simple', value: 'PONG' });
});

test('command handler PING echoes its argument', () => {
  const handler = new CommandHandler();
  assert.deepStrictEqual(handler.execute(['PING', 'hello']), { type: 'bulk', value: 'hello' });
});

test('command handler SET then GET returns the stored value', () => {
  const handler = new CommandHandler();
  assert.deepStrictEqual(handler.execute(['SET', 'k', 'v']), { type: 'simple', value: 'OK' });
  assert.deepStrictEqual(handler.execute(['GET', 'k']), { type: 'bulk', value: 'v' });
});

test('command handler GET on a missing key returns a null bulk string', () => {
  const handler = new CommandHandler();
  assert.deepStrictEqual(handler.execute(['GET', 'missing']), { type: 'bulk', value: null });
});

test('command handler DEL returns count of removed keys', () => {
  const handler = new CommandHandler();
  handler.execute(['SET', 'a', '1']);
  handler.execute(['SET', 'b', '2']);
  assert.deepStrictEqual(handler.execute(['DEL', 'a', 'b', 'c']), { type: 'integer', value: 2 });
});

test('command handler EXPIRE and TTL reflect real expiry semantics', () => {
  let now = 0;
  const handler = new CommandHandler(new RedisStore(() => now));
  handler.execute(['SET', 'k', 'v']);
  assert.deepStrictEqual(handler.execute(['EXPIRE', 'k', '10']), { type: 'integer', value: 1 });
  now = 5000;
  assert.deepStrictEqual(handler.execute(['TTL', 'k']), { type: 'integer', value: 5 });
  now = 11000;
  assert.deepStrictEqual(handler.execute(['GET', 'k']), { type: 'bulk', value: null });
  assert.deepStrictEqual(handler.execute(['TTL', 'k']), { type: 'integer', value: -2 });
});

test('command handler rejects unknown commands with an error', () => {
  const handler = new CommandHandler();
  const result = handler.execute(['FROBNICATE']);
  assert.strictEqual(result.type, 'error');
});

test('command handler rejects wrong arity with an error', () => {
  const handler = new CommandHandler();
  assert.strictEqual(handler.execute(['SET', 'onlykey']).type, 'error');
  assert.strictEqual(handler.execute(['GET']).type, 'error');
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
