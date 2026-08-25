'use strict';

const { UrlShortener } = require('./shortener.js');

const shortener = new UrlShortener();

const codeA = shortener.shorten('https://example.com/articles/how-base62-works');
console.log(`shortened -> ${codeA}`);

const codeAAgain = shortener.shorten('https://example.com/articles/how-base62-works');
console.log(`idempotent re-shorten -> ${codeAAgain} (same code: ${codeA === codeAAgain})`);

const codeB = shortener.shorten('https://example.com/articles/why-hashing-collides');
console.log(`shortened -> ${codeB}`);

const aliasCode = shortener.shorten('https://example.com/promo/launch', { alias: 'launch2026' });
console.log(`custom alias -> ${aliasCode}`);

try {
  shortener.shorten('https://example.com/promo/other', { alias: 'launch2026' });
} catch (err) {
  console.log(`custom alias conflict rejected: ${err.message}`);
}

console.log(`resolve(${codeA}) -> ${shortener.resolve(codeA)}`);
console.log(`resolve(${codeA}) again -> ${shortener.resolve(codeA)}`);
console.log(`clicks so far -> ${shortener.stats(codeA).clicks}`);

console.log(`resolve unknown code -> ${shortener.resolve('doesNotExist')}`);

const now = Date.now();
const ttlCode = shortener.shorten('https://example.com/temporary/offer', { ttlMs: 1000, now });
console.log(`ttl code -> ${ttlCode}, resolves now -> ${shortener.resolve(ttlCode, { now })}`);
console.log(
  `ttl code after expiry -> ${shortener.resolve(ttlCode, { now: now + 1001 })}`
);

const reusedTtlCode = shortener.shorten('https://example.com/temporary/offer-v2', {
  alias: ttlCode,
  now: now + 1001,
});
console.log(`code reused after expiry via alias -> ${reusedTtlCode}`);
