'use strict';

const assert = require('assert');
const crypto = require('crypto');
const { UrlShortener, encodeBase62, BASE62_ALPHABET } = require('./shortener.js');

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

function forcedCollisionHashFn(longUrl, attempt) {
  if (attempt === 0) {
    return crypto.createHash('sha256').update('forced-collision-seed').digest();
  }
  return crypto.createHash('sha256').update(`${longUrl}::${attempt}`).digest();
}

test('shorten produces a base62 code of the configured length', () => {
  const shortener = new UrlShortener({ codeLength: 8 });
  const code = shortener.shorten('https://example.com/a');
  assert.strictEqual(code.length, 8);
  for (const ch of code) {
    assert.ok(BASE62_ALPHABET.includes(ch), `unexpected char ${ch}`);
  }
});

test('re-shortening the same url is idempotent and returns the same code', () => {
  const shortener = new UrlShortener();
  const codeA = shortener.shorten('https://example.com/same');
  const codeB = shortener.shorten('https://example.com/same');
  assert.strictEqual(codeA, codeB);
});

test('custom alias is used verbatim and rejects conflicting reuse', () => {
  const shortener = new UrlShortener();
  const code = shortener.shorten('https://example.com/one', { alias: 'myalias' });
  assert.strictEqual(code, 'myalias');
  assert.throws(() => shortener.shorten('https://example.com/two', { alias: 'myalias' }), /already taken/);
});

test('custom alias re-requested for the same url is idempotent', () => {
  const shortener = new UrlShortener();
  const code = shortener.shorten('https://example.com/one', { alias: 'myalias' });
  const codeAgain = shortener.shorten('https://example.com/one', { alias: 'myalias' });
  assert.strictEqual(code, codeAgain);
});

test('resolve of unknown code returns null', () => {
  const shortener = new UrlShortener();
  assert.strictEqual(shortener.resolve('nope'), null);
});

test('resolve increments click count and updates last accessed time', () => {
  const shortener = new UrlShortener();
  const code = shortener.shorten('https://example.com/track');
  assert.strictEqual(shortener.stats(code).clicks, 0);
  shortener.resolve(code, { now: 1000 });
  shortener.resolve(code, { now: 2000 });
  const stats = shortener.stats(code);
  assert.strictEqual(stats.clicks, 2);
  assert.strictEqual(stats.lastAccessedAt, 2000);
});

test('ttl expiry makes resolve return null after expiry', () => {
  const shortener = new UrlShortener();
  const code = shortener.shorten('https://example.com/expiring', { ttlMs: 1000, now: 0 });
  assert.strictEqual(shortener.resolve(code, { now: 500 }), 'https://example.com/expiring');
  assert.strictEqual(shortener.resolve(code, { now: 1500 }), null);
});

test('expired code becomes reusable via a new alias', () => {
  const shortener = new UrlShortener();
  const code = shortener.shorten('https://example.com/expiring', { ttlMs: 1000, now: 0 });
  assert.strictEqual(shortener.has(code, { now: 1500 }), false);
  const reused = shortener.shorten('https://example.com/replacement', { alias: code, now: 1500 });
  assert.strictEqual(reused, code);
  assert.strictEqual(shortener.resolve(code, { now: 1600 }), 'https://example.com/replacement');
});

test('forced collision: two different urls get distinct codes via retry', () => {
  const shortener = new UrlShortener({ hashFn: forcedCollisionHashFn });
  const codeA = shortener.shorten('https://example.com/collide-a');
  const codeB = shortener.shorten('https://example.com/collide-b');
  assert.notStrictEqual(codeA, codeB);
  assert.strictEqual(shortener.resolve(codeA), 'https://example.com/collide-a');
  assert.strictEqual(shortener.resolve(codeB), 'https://example.com/collide-b');
});

test('encodeBase62 is deterministic and pads to the requested length', () => {
  const digest = crypto.createHash('sha256').update('sample').digest();
  const encoded = encodeBase62(digest, 10);
  assert.strictEqual(encoded.length, 10);
  assert.strictEqual(encoded, encodeBase62(digest, 10));
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
