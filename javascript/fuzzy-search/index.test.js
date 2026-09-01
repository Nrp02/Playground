'use strict';

const assert = require('assert');
const { BKTree } = require('./bktree.js');
const { TrigramIndex, trigrams } = require('./trigramIndex.js');
const { damerauLevenshtein, boundedLevenshtein } = require('./distance.js');

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

function randomWord(rng, alphabet, minLen, maxLen) {
  const len = minLen + Math.floor(rng() * (maxLen - minLen));
  let s = '';
  for (let i = 0; i < len; i++) {
    s += alphabet[Math.floor(rng() * alphabet.length)];
  }
  return s;
}

function buildRandomDictionary(rng, size) {
  const words = new Set();
  const alphabet = 'abcdefghij';
  while (words.size < size) {
    words.add(randomWord(rng, alphabet, 3, 9));
  }
  return Array.from(words);
}

function bruteForceMatches(dictionary, query, maxDistance) {
  const matches = new Set();
  for (const word of dictionary) {
    if (damerauLevenshtein(query, word) <= maxDistance) matches.add(word);
  }
  return matches;
}

test('BK-tree search returns exactly the same set as brute force scan', () => {
  const rng = mulberry32(10);
  const dictionary = buildRandomDictionary(rng, 300);
  const tree = new BKTree((a, b) => damerauLevenshtein(a, b));
  for (const word of dictionary) tree.insert(word);

  for (let i = 0; i < 20; i++) {
    const query = randomWord(rng, 'abcdefghij', 3, 9);
    for (const maxDistance of [1, 2, 3]) {
      const expected = bruteForceMatches(dictionary, query, maxDistance);
      const { results } = tree.search(query, maxDistance);
      const actual = new Set(results.map((r) => r.word));
      assert.strictEqual(actual.size, expected.size);
      for (const word of expected) assert.ok(actual.has(word), `missing ${word} for query ${query} k=${maxDistance}`);
    }
  }
});

test('trigram candidate generation has no false negatives vs brute force', () => {
  const rng = mulberry32(20);
  const dictionary = buildRandomDictionary(rng, 400);
  const index = new TrigramIndex();
  for (const word of dictionary) index.add(word);

  for (let i = 0; i < 30; i++) {
    const query = randomWord(rng, 'abcdefghij', 4, 9);
    for (const maxDistance of [1, 2]) {
      const trueMatches = [];
      for (const word of dictionary) {
        if (boundedLevenshtein(query, word, maxDistance) <= maxDistance) trueMatches.push(word);
      }
      const minShared = TrigramIndex.minSharedForDistance(query.length, maxDistance);
      const candidates =
        minShared <= 0 ? new Set(dictionary) : new Set(index.candidates(query, minShared));
      for (const word of trueMatches) {
        assert.ok(candidates.has(word), `false negative: ${word} missing for query ${query} k=${maxDistance}`);
      }
    }
  }
});

test('trigrams function produces expected set for a simple word', () => {
  const grams = trigrams('cat');
  assert.ok(grams.has('  c'));
  assert.ok(grams.has(' ca'));
  assert.ok(grams.has('cat'));
  assert.ok(grams.has('at '));
  assert.ok(grams.has('t  '));
});

test('BK-tree handles empty tree', () => {
  const tree = new BKTree((a, b) => damerauLevenshtein(a, b));
  const { results, nodesVisited } = tree.search('anything', 2);
  assert.strictEqual(results.length, 0);
  assert.strictEqual(nodesVisited, 0);
});

test('TrigramIndex handles short terms shorter than a trigram', () => {
  const index = new TrigramIndex();
  index.add('a');
  index.add('ab');
  const minShared = TrigramIndex.minSharedForDistance(1, 1);
  const candidates = index.candidates('a', minShared);
  assert.ok(candidates.includes('a'));
});

test('BK-tree deduplicates identical words', () => {
  const tree = new BKTree((a, b) => damerauLevenshtein(a, b));
  tree.insert('hello');
  tree.insert('hello');
  tree.insert('hello');
  assert.strictEqual(tree.size, 3);
  const { results } = tree.search('hello', 0);
  assert.strictEqual(results.length, 1);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
