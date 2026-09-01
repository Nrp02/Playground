'use strict';

const { FuzzyDictionary, FuzzyDocumentSearch } = require('./search.js');

const SYLLABLES = [
  'ab', 'bra', 'cad', 'dex', 'el', 'fon', 'gra', 'hil', 'in', 'jor',
  'kel', 'lom', 'mar', 'nix', 'or', 'pel', 'qua', 'rex', 'sol', 'tan',
  'un', 'vor', 'wex', 'xan', 'yol', 'zed', 'tion', 'ing', 'er', 'ly',
];

function randomWord(rng) {
  const parts = 2 + Math.floor(rng() * 3);
  let word = '';
  for (let i = 0; i < parts; i++) {
    word += SYLLABLES[Math.floor(rng() * SYLLABLES.length)];
  }
  return word;
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

function buildDictionary(size, rng) {
  const dictionary = new FuzzyDictionary();
  const seen = new Set();
  while (seen.size < size) {
    const word = randomWord(rng);
    if (seen.has(word)) continue;
    seen.add(word);
    const frequency = 1 + Math.floor(rng() * 50);
    dictionary.addTerm(word, frequency);
  }
  return dictionary;
}

function misspell(word, rng) {
  if (word.length < 2) return word;
  const idx = Math.floor(rng() * (word.length - 1));
  const chars = word.split('');
  const tmp = chars[idx];
  chars[idx] = chars[idx + 1];
  chars[idx + 1] = tmp;
  return chars.join('');
}

function main() {
  const rng = mulberry32(42);
  const dictSize = 30000;
  console.log(`building dictionary of ${dictSize} terms...`);
  const dictionary = buildDictionary(dictSize, rng);
  console.log(`dictionary built, ${dictionary.terms.length} terms\n`);

  const sampleTerms = [];
  for (let i = 0; i < 6; i++) {
    sampleTerms.push(dictionary.terms[Math.floor(rng() * dictionary.terms.length)]);
  }

  console.log('=== fuzzy suggestions for misspelled queries ===');
  for (const term of sampleTerms) {
    const query = misspell(term, rng);
    const suggestions = dictionary.suggest(query, 2, 5);
    console.log(`\nquery: ${query}  (from: ${term})`);
    for (const suggestion of suggestions) {
      console.log(
        `  ${suggestion.term.padEnd(20)} distance=${suggestion.distance}  score=${suggestion.score.toFixed(3)}`
      );
    }
  }

  console.log('\n=== pruning comparison: BK-tree vs trigram vs brute force ===');
  console.log(`dictionary size: ${dictionary.terms.length}`);
  for (const term of sampleTerms.slice(0, 3)) {
    const query = misspell(term, rng);
    const bk = dictionary.searchBkTree(query, 2);
    const trigram = dictionary.searchTrigram(query, 2);
    const bruteForceCount = dictionary.terms.length;
    console.log(`\nquery: ${query}`);
    console.log(`  brute force scan:  ${bruteForceCount} terms compared`);
    console.log(`  bk-tree:           ${bk.nodesVisited} nodes visited, ${bk.matches.length} matches`);
    console.log(`  trigram index:     ${trigram.candidateCount} candidates verified, ${trigram.matches.length} matches`);
  }

  console.log('\n=== document search with typo tolerance ===');
  const documentSearch = new FuzzyDocumentSearch();
  documentSearch.addDocument('the quick brown fox jumps over the lazy dog');
  documentSearch.addDocument('python programming language for data science and scripting');
  documentSearch.addDocument('a red fox ran quickly across the frozen river');
  documentSearch.addDocument('javascript is a popular scripting language for the web');
  documentSearch.addDocument('the lazy cat slept all afternoon in the warm sun');

  const queries = ['pyton programing langauge', 'qwick fox', 'lazi dog'];
  for (const query of queries) {
    console.log(`\nquery: "${query}"`);
    const results = documentSearch.search(query, { maxDistance: 2, topK: 3 });
    if (results.length === 0) {
      console.log('  no matches');
      continue;
    }
    for (const result of results) {
      console.log(`  score=${result.score.toFixed(3)}  ${result.text}`);
    }
  }
}

main();
