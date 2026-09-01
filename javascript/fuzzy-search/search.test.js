'use strict';

const assert = require('assert');
const { FuzzyDictionary, FuzzyDocumentSearch, tokenize } = require('./search.js');

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

test('tokenize lowercases and splits on non-alphanumeric', () => {
  assert.deepStrictEqual(tokenize('Hello, World! It\'s Fine.'), ['hello', 'world', "it's", 'fine']);
});

test('tokenize handles empty string', () => {
  assert.deepStrictEqual(tokenize(''), []);
});

test('suggest returns exact match first for an unmodified term', () => {
  const dictionary = new FuzzyDictionary();
  dictionary.addTerm('hello');
  dictionary.addTerm('yellow');
  dictionary.addTerm('mellow');
  const suggestions = dictionary.suggest('hello', 2, 3);
  assert.strictEqual(suggestions[0].term, 'hello');
  assert.strictEqual(suggestions[0].distance, 0);
});

test('suggest finds typo within edit distance', () => {
  const dictionary = new FuzzyDictionary();
  dictionary.addTerm('receive');
  dictionary.addTerm('deceive');
  const suggestions = dictionary.suggest('recieve', 2, 5);
  const terms = suggestions.map((s) => s.term);
  assert.ok(terms.includes('receive'));
});

test('ranking is deterministic across repeated calls', () => {
  const dictionary = new FuzzyDictionary();
  dictionary.addTerm('apple');
  dictionary.addTerm('appla');
  dictionary.addTerm('appli');
  const first = dictionary.suggest('appl', 2, 10);
  const second = dictionary.suggest('appl', 2, 10);
  assert.deepStrictEqual(first, second);
});

test('ranking is stable for exact ties', () => {
  const dictionary = new FuzzyDictionary();
  dictionary.addTerm('aaa', 5);
  dictionary.addTerm('aab', 5);
  const suggestions = dictionary.suggest('aac', 1, 10);
  assert.deepStrictEqual(
    suggestions.map((s) => s.term),
    ['aaa', 'aab']
  );
});

test('empty query returns no suggestions gracefully', () => {
  const dictionary = new FuzzyDictionary();
  dictionary.addTerm('hello');
  const suggestions = dictionary.suggest('', 2, 5);
  assert.ok(Array.isArray(suggestions));
});

test('empty dictionary returns no suggestions', () => {
  const dictionary = new FuzzyDictionary();
  const suggestions = dictionary.suggest('hello', 2, 5);
  assert.deepStrictEqual(suggestions, []);
});

test('short (1-2 char) terms are matched correctly', () => {
  const dictionary = new FuzzyDictionary();
  dictionary.addTerm('a');
  dictionary.addTerm('an');
  dictionary.addTerm('at');
  const suggestions = dictionary.suggest('a', 1, 5);
  const terms = suggestions.map((s) => s.term);
  assert.ok(terms.includes('a'));
});

test('unicode terms are matched correctly', () => {
  const dictionary = new FuzzyDictionary();
  dictionary.addTerm('café');
  dictionary.addTerm('naïve');
  const suggestions = dictionary.suggest('cafe', 1, 5);
  const terms = suggestions.map((s) => s.term);
  assert.ok(terms.includes('café'));
});

test('document search finds correct document despite a typo query', () => {
  const documentSearch = new FuzzyDocumentSearch();
  documentSearch.addDocument('the quick brown fox jumps over the lazy dog');
  documentSearch.addDocument('python programming language for scripting');
  const results = documentSearch.search('pyton programing', { maxDistance: 2, topK: 3 });
  assert.ok(results.length > 0);
  assert.ok(results[0].text.includes('python programming'));
});

test('document search on empty query returns empty results', () => {
  const documentSearch = new FuzzyDocumentSearch();
  documentSearch.addDocument('some text here');
  const results = documentSearch.search('', { maxDistance: 2, topK: 3 });
  assert.deepStrictEqual(results, []);
});

test('document search on empty document set returns empty results', () => {
  const documentSearch = new FuzzyDocumentSearch();
  const results = documentSearch.search('anything', { maxDistance: 2, topK: 3 });
  assert.deepStrictEqual(results, []);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
