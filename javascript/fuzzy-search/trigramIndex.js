'use strict';

function trigrams(word) {
  const padded = `  ${word}  `;
  const grams = new Set();
  for (let i = 0; i <= padded.length - 3; i++) {
    grams.add(padded.slice(i, i + 3));
  }
  return grams;
}

class TrigramIndex {
  constructor() {
    this.postings = new Map();
    this.words = new Set();
  }

  add(word) {
    if (this.words.has(word)) return;
    this.words.add(word);
    for (const gram of trigrams(word)) {
      let bucket = this.postings.get(gram);
      if (!bucket) {
        bucket = new Set();
        this.postings.set(gram, bucket);
      }
      bucket.add(word);
    }
  }

  candidates(query, minShared) {
    const queryGrams = trigrams(query);
    const counts = new Map();
    for (const gram of queryGrams) {
      const bucket = this.postings.get(gram);
      if (!bucket) continue;
      for (const word of bucket) {
        counts.set(word, (counts.get(word) || 0) + 1);
      }
    }
    const result = [];
    for (const [word, count] of counts) {
      if (count >= minShared) result.push(word);
    }
    return result;
  }

  static minSharedForDistance(queryLength, maxDistance) {
    const queryGramCount = queryLength + 2;
    return queryGramCount - maxDistance * 3;
  }
}

module.exports = { TrigramIndex, trigrams };
