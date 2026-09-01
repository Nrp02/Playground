'use strict';

const { boundedLevenshtein, damerauLevenshtein } = require('./distance.js');
const { BKTree } = require('./bktree.js');
const { TrigramIndex } = require('./trigramIndex.js');

function metric(a, b, maxDistance) {
  return Math.min(boundedLevenshtein(a, b, maxDistance), damerauLevenshtein(a, b));
}

function prefixBonus(a, b) {
  const len = Math.min(a.length, b.length, 4);
  let matched = 0;
  for (let i = 0; i < len; i++) {
    if (a[i] === b[i]) matched++;
    else break;
  }
  return matched;
}

function score(query, term, distance, frequency) {
  const lengthNorm = 1 / (1 + Math.abs(query.length - term.length));
  const distancePenalty = 1 / (1 + distance);
  const prefix = prefixBonus(query, term);
  const frequencyBonus = Math.log(1 + frequency);
  return distancePenalty * 4 + prefix * 0.5 + lengthNorm * 0.5 + frequencyBonus * 0.2;
}

class FuzzyDictionary {
  constructor() {
    this.bkTree = new BKTree((a, b) => damerauLevenshtein(a, b));
    this.trigramIndex = new TrigramIndex();
    this.frequency = new Map();
    this.terms = [];
  }

  addTerm(term, count = 1) {
    if (!this.frequency.has(term)) {
      this.bkTree.insert(term);
      this.trigramIndex.add(term);
      this.terms.push(term);
      this.frequency.set(term, count);
    } else {
      this.frequency.set(term, this.frequency.get(term) + count);
    }
  }

  bruteForceCandidates(query, maxDistance) {
    const results = [];
    for (const term of this.terms) {
      const d = metric(query, term, maxDistance);
      if (d <= maxDistance) results.push({ term, distance: d });
    }
    return results;
  }

  searchBkTree(query, maxDistance) {
    const { results, nodesVisited } = this.bkTree.search(query, maxDistance);
    return {
      matches: results.map((r) => ({ term: r.word, distance: r.distance })),
      nodesVisited,
    };
  }

  searchTrigram(query, maxDistance) {
    const minShared = TrigramIndex.minSharedForDistance(query.length, maxDistance);
    if (query.length < 3 || minShared <= 0) {
      const matches = this.bruteForceCandidates(query, maxDistance);
      return { matches, candidateCount: this.terms.length };
    }
    const candidates = this.trigramIndex.candidates(query, minShared);
    const matches = [];
    for (const term of candidates) {
      const d = metric(query, term, maxDistance);
      if (d <= maxDistance) matches.push({ term, distance: d });
    }
    return { matches, candidateCount: candidates.length };
  }

  suggest(query, maxDistance, topK) {
    const { matches } = this.searchTrigram(query, maxDistance);
    const ranked = matches
      .map((m) => ({
        term: m.term,
        distance: m.distance,
        score: score(query, m.term, m.distance, this.frequency.get(m.term) || 1),
      }))
      .sort((a, b) => {
        if (b.score !== a.score) return b.score - a.score;
        return a.term < b.term ? -1 : a.term > b.term ? 1 : 0;
      });
    return ranked.slice(0, topK);
  }
}

function tokenize(text) {
  return text
    .toLowerCase()
    .split(/[^a-z0-9']+/)
    .filter((token) => token.length > 0);
}

class FuzzyDocumentSearch {
  constructor() {
    this.dictionary = new FuzzyDictionary();
    this.documents = [];
  }

  addDocument(text) {
    const docId = this.documents.length;
    const tokens = tokenize(text);
    this.documents.push({ id: docId, text, tokens });
    for (const token of tokens) this.dictionary.addTerm(token);
    return docId;
  }

  search(query, { maxDistance = 2, topK = 5, matchesPerToken = 3 } = {}) {
    const queryTokens = tokenize(query);
    if (queryTokens.length === 0) return [];
    const docScores = new Map();
    for (const token of queryTokens) {
      const suggestions = this.dictionary.suggest(token, maxDistance, matchesPerToken);
      if (suggestions.length === 0) continue;
      for (const doc of this.documents) {
        let best = 0;
        for (const suggestion of suggestions) {
          if (doc.tokens.includes(suggestion.term) && suggestion.score > best) {
            best = suggestion.score;
          }
        }
        if (best > 0) {
          docScores.set(doc.id, (docScores.get(doc.id) || 0) + best);
        }
      }
    }
    return Array.from(docScores.entries())
      .map(([docId, total]) => ({ docId, text: this.documents[docId].text, score: total }))
      .sort((a, b) => {
        if (b.score !== a.score) return b.score - a.score;
        return a.docId - b.docId;
      })
      .slice(0, topK);
  }
}

module.exports = { FuzzyDictionary, FuzzyDocumentSearch, tokenize, metric, score };
