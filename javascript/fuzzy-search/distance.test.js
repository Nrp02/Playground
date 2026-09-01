'use strict';

const assert = require('assert');
const { levenshtein, boundedLevenshtein, damerauLevenshtein } = require('./distance.js');

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

function referenceLevenshtein(a, b) {
  const m = a.length;
  const n = b.length;
  const d = [];
  for (let i = 0; i <= m; i++) d.push(new Array(n + 1).fill(0));
  for (let i = 0; i <= m; i++) d[i][0] = i;
  for (let j = 0; j <= n; j++) d[0][j] = j;
  for (let i = 1; i <= m; i++) {
    for (let j = 1; j <= n; j++) {
      const cost = a[i - 1] === b[j - 1] ? 0 : 1;
      d[i][j] = Math.min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost);
    }
  }
  return d[m][n];
}

function mulberry32(seed) {
  let a = seed;
  return function () {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function randomString(rng, alphabet, maxLen) {
  const len = Math.floor(rng() * maxLen);
  let s = '';
  for (let i = 0; i < len; i++) {
    s += alphabet[Math.floor(rng() * alphabet.length)];
  }
  return s;
}

test('levenshtein matches reference DP on empty strings', () => {
  assert.strictEqual(levenshtein('', ''), 0);
  assert.strictEqual(levenshtein('abc', ''), 3);
  assert.strictEqual(levenshtein('', 'abc'), 3);
});

test('levenshtein matches reference DP over randomized strings', () => {
  const rng = mulberry32(1);
  const alphabet = 'abcde';
  for (let i = 0; i < 300; i++) {
    const a = randomString(rng, alphabet, 10);
    const b = randomString(rng, alphabet, 10);
    assert.strictEqual(levenshtein(a, b), referenceLevenshtein(a, b));
  }
});

test('boundedLevenshtein agrees with unbounded when true distance is within k', () => {
  const rng = mulberry32(2);
  const alphabet = 'abcde';
  for (let i = 0; i < 300; i++) {
    const a = randomString(rng, alphabet, 10);
    const b = randomString(rng, alphabet, 10);
    const trueDistance = levenshtein(a, b);
    for (const k of [0, 1, 2, 3]) {
      const bounded = boundedLevenshtein(a, b, k);
      if (trueDistance <= k) {
        assert.strictEqual(bounded, trueDistance);
      } else {
        assert.strictEqual(bounded, k + 1);
      }
    }
  }
});

test('boundedLevenshtein reports over-k for clearly distant strings', () => {
  const result = boundedLevenshtein('abcdefgh', 'zzzzzzzz', 2);
  assert.strictEqual(result, 3);
});

test('damerau handles adjacent transpositions as distance 1', () => {
  assert.strictEqual(damerauLevenshtein('teh', 'the'), 1);
  assert.strictEqual(damerauLevenshtein('abcd', 'abdc'), 1);
});

test('damerau distance is 0 for identical strings, positive for different', () => {
  assert.strictEqual(damerauLevenshtein('hello', 'hello'), 0);
  assert.ok(damerauLevenshtein('hello', 'world') > 0);
});

test('metric properties: identity holds on randomized strings', () => {
  const rng = mulberry32(3);
  const alphabet = 'abcdef';
  for (let i = 0; i < 50; i++) {
    const s = randomString(rng, alphabet, 8);
    assert.strictEqual(levenshtein(s, s), 0);
    assert.strictEqual(damerauLevenshtein(s, s), 0);
  }
});

test('metric properties: symmetry holds on randomized triples', () => {
  const rng = mulberry32(4);
  const alphabet = 'abcdef';
  for (let i = 0; i < 200; i++) {
    const a = randomString(rng, alphabet, 8);
    const b = randomString(rng, alphabet, 8);
    assert.strictEqual(levenshtein(a, b), levenshtein(b, a));
    assert.strictEqual(damerauLevenshtein(a, b), damerauLevenshtein(b, a));
  }
});

test('metric properties: triangle inequality holds on randomized triples', () => {
  const rng = mulberry32(5);
  const alphabet = 'abcdef';
  for (let i = 0; i < 200; i++) {
    const a = randomString(rng, alphabet, 8);
    const b = randomString(rng, alphabet, 8);
    const c = randomString(rng, alphabet, 8);
    const ab = levenshtein(a, b);
    const bc = levenshtein(b, c);
    const ac = levenshtein(a, c);
    assert.ok(ac <= ab + bc);
  }
});

test('unicode strings are handled without throwing', () => {
  assert.strictEqual(levenshtein('café', 'cafe'), 1);
  assert.strictEqual(damerauLevenshtein('naïve', 'naive'), 1);
  assert.ok(boundedLevenshtein('日本語', '日本後', 2) <= 2);
});

test('short strings (1-2 chars) compute correctly', () => {
  assert.strictEqual(levenshtein('a', 'a'), 0);
  assert.strictEqual(levenshtein('a', 'b'), 1);
  assert.strictEqual(levenshtein('ab', 'ba'), 2);
  assert.strictEqual(damerauLevenshtein('ab', 'ba'), 1);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
