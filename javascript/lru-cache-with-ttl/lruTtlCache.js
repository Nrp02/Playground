'use strict';

class LruTtlCache {
  constructor(capacity, defaultTtlMs = Infinity) {
    if (!Number.isInteger(capacity) || capacity <= 0) {
      throw new RangeError('capacity must be a positive integer');
    }
    if (typeof defaultTtlMs !== 'number' || defaultTtlMs <= 0 || Number.isNaN(defaultTtlMs)) {
      throw new RangeError('defaultTtlMs must be a positive number or Infinity');
    }
    this.capacity = capacity;
    this.defaultTtlMs = defaultTtlMs;
    this._entries = new Map();
  }

  _isExpired(entry, now) {
    return entry.expiresAt !== Infinity && now >= entry.expiresAt;
  }

  _touch(key, entry) {
    this._entries.delete(key);
    this._entries.set(key, entry);
  }

  has(key, now = Date.now()) {
    const entry = this._entries.get(key);
    if (entry === undefined) {
      return false;
    }
    if (this._isExpired(entry, now)) {
      this._entries.delete(key);
      return false;
    }
    return true;
  }

  get(key, now = Date.now()) {
    const entry = this._entries.get(key);
    if (entry === undefined) {
      return undefined;
    }
    if (this._isExpired(entry, now)) {
      this._entries.delete(key);
      return undefined;
    }
    this._touch(key, entry);
    return entry.value;
  }

  peek(key, now = Date.now()) {
    const entry = this._entries.get(key);
    if (entry === undefined) {
      return undefined;
    }
    if (this._isExpired(entry, now)) {
      return undefined;
    }
    return entry.value;
  }

  set(key, value, ttlMs = this.defaultTtlMs, now = Date.now()) {
    if (typeof ttlMs !== 'number' || ttlMs <= 0 || Number.isNaN(ttlMs)) {
      throw new RangeError('ttlMs must be a positive number or Infinity');
    }
    const expiresAt = ttlMs === Infinity ? Infinity : now + ttlMs;
    const existing = this._entries.get(key);
    if (existing !== undefined) {
      this._entries.delete(key);
    } else if (this._entries.size >= this.capacity) {
      this._evictOldest();
    }
    this._entries.set(key, { value, expiresAt });
  }

  _evictOldest() {
    const oldestKey = this._entries.keys().next().value;
    if (oldestKey !== undefined) {
      this._entries.delete(oldestKey);
    }
    return oldestKey;
  }

  delete(key) {
    return this._entries.delete(key);
  }

  clear() {
    this._entries.clear();
  }

  get size() {
    return this._entries.size;
  }

  expiryOf(key) {
    const entry = this._entries.get(key);
    return entry === undefined ? undefined : entry.expiresAt;
  }

  keys(now = Date.now()) {
    const result = [];
    for (const [key, entry] of this._entries) {
      if (!this._isExpired(entry, now)) {
        result.push(key);
      }
    }
    return result;
  }

  entries(now = Date.now()) {
    const result = [];
    for (const [key, entry] of this._entries) {
      if (!this._isExpired(entry, now)) {
        result.push([key, entry.value]);
      }
    }
    return result;
  }

  purgeExpired(now = Date.now()) {
    const expiredKeys = [];
    for (const [key, entry] of this._entries) {
      if (this._isExpired(entry, now)) {
        expiredKeys.push(key);
      }
    }
    for (const key of expiredKeys) {
      this._entries.delete(key);
    }
    return expiredKeys;
  }
}

module.exports = { LruTtlCache };
