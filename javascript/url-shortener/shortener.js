'use strict';

const crypto = require('crypto');

const BASE62_ALPHABET = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ';
const DEFAULT_CODE_LENGTH = 7;
const MAX_COLLISION_ATTEMPTS = 50;

function encodeBase62(buffer, length) {
  let value = BigInt('0x' + buffer.toString('hex'));
  const base = BigInt(62);
  let out = '';
  while (value > 0n && out.length < length) {
    const digit = Number(value % base);
    out = BASE62_ALPHABET[digit] + out;
    value = value / base;
  }
  while (out.length < length) {
    out = BASE62_ALPHABET[0] + out;
  }
  return out;
}

function defaultHashFn(longUrl, attempt) {
  return crypto.createHash('sha256').update(`${longUrl}::${attempt}`).digest();
}

class UrlShortener {
  constructor(options = {}) {
    this.codeLength = options.codeLength || DEFAULT_CODE_LENGTH;
    this.hashFn = options.hashFn || defaultHashFn;
    this._codeToRecord = new Map();
    this._urlToCode = new Map();
  }

  _isExpired(record, now) {
    return record.expiresAt !== null && record.expiresAt <= now;
  }

  _evictIfExpired(code, now) {
    const record = this._codeToRecord.get(code);
    if (record && this._isExpired(record, now)) {
      this._codeToRecord.delete(code);
      if (this._urlToCode.get(record.longUrl) === code) {
        this._urlToCode.delete(record.longUrl);
      }
      return true;
    }
    return false;
  }

  _deriveCode(longUrl, attempt, hashFn) {
    const digest = hashFn(longUrl, attempt);
    return encodeBase62(digest, this.codeLength);
  }

  shorten(longUrl, options = {}) {
    const now = options.now !== undefined ? options.now : Date.now();
    const ttlMs = options.ttlMs;
    const expiresAt = ttlMs !== undefined && ttlMs !== null ? now + ttlMs : null;
    const hashFn = options.hashFn || this.hashFn;

    if (options.alias) {
      const alias = options.alias;
      this._evictIfExpired(alias, now);
      const existing = this._codeToRecord.get(alias);
      if (existing) {
        if (existing.longUrl === longUrl) {
          return alias;
        }
        throw new Error(`alias '${alias}' is already taken`);
      }
      this._createRecord(alias, longUrl, now, expiresAt);
      return alias;
    }

    const existingCode = this._urlToCode.get(longUrl);
    if (existingCode !== undefined) {
      this._evictIfExpired(existingCode, now);
      const record = this._codeToRecord.get(existingCode);
      if (record && record.longUrl === longUrl) {
        return existingCode;
      }
    }

    let attempt = 0;
    while (attempt < MAX_COLLISION_ATTEMPTS) {
      const code = this._deriveCode(longUrl, attempt, hashFn);
      this._evictIfExpired(code, now);
      const record = this._codeToRecord.get(code);
      if (!record) {
        this._createRecord(code, longUrl, now, expiresAt);
        return code;
      }
      if (record.longUrl === longUrl) {
        return code;
      }
      attempt += 1;
    }

    throw new Error(`could not derive a free code for '${longUrl}' after ${MAX_COLLISION_ATTEMPTS} attempts`);
  }

  _createRecord(code, longUrl, now, expiresAt) {
    const record = {
      longUrl,
      code,
      createdAt: now,
      expiresAt,
      clicks: 0,
      lastAccessedAt: null,
    };
    this._codeToRecord.set(code, record);
    this._urlToCode.set(longUrl, code);
  }

  resolve(code, options = {}) {
    const now = options.now !== undefined ? options.now : Date.now();
    if (this._evictIfExpired(code, now)) {
      return null;
    }
    const record = this._codeToRecord.get(code);
    if (!record) {
      return null;
    }
    record.clicks += 1;
    record.lastAccessedAt = now;
    return record.longUrl;
  }

  stats(code, options = {}) {
    const now = options.now !== undefined ? options.now : Date.now();
    if (this._evictIfExpired(code, now)) {
      return null;
    }
    const record = this._codeToRecord.get(code);
    if (!record) {
      return null;
    }
    return {
      longUrl: record.longUrl,
      clicks: record.clicks,
      createdAt: record.createdAt,
      expiresAt: record.expiresAt,
      lastAccessedAt: record.lastAccessedAt,
    };
  }

  has(code, options = {}) {
    const now = options.now !== undefined ? options.now : Date.now();
    this._evictIfExpired(code, now);
    return this._codeToRecord.has(code);
  }
}

module.exports = { UrlShortener, encodeBase62, BASE62_ALPHABET };
