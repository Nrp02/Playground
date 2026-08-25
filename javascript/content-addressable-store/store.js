'use strict';

const crypto = require('crypto');

function hashOf(buffer) {
  return crypto.createHash('sha256').update(buffer).digest('hex');
}

function toBuffer(data) {
  if (Buffer.isBuffer(data)) {
    return data;
  }
  if (typeof data === 'string') {
    return Buffer.from(data, 'utf8');
  }
  throw new TypeError('data must be a Buffer or a string');
}

class IntegrityError extends Error {
  constructor(hash) {
    super(`stored object ${hash} failed integrity verification`);
    this.name = 'IntegrityError';
    this.hash = hash;
  }
}

class ContentAddressableStore {
  constructor() {
    this._objects = new Map();
  }

  put(data) {
    const buffer = toBuffer(data);
    const hash = hashOf(buffer);
    if (!this._objects.has(hash)) {
      this._objects.set(hash, Buffer.from(buffer));
    }
    return hash;
  }

  has(hash) {
    return this._objects.has(hash);
  }

  size() {
    return this._objects.size;
  }

  hashes() {
    return Array.from(this._objects.keys());
  }

  get(hash) {
    if (typeof hash !== 'string' || hash.length === 0) {
      throw new TypeError('hash must be a non-empty string');
    }
    if (!this._objects.has(hash)) {
      throw new RangeError(`no object stored for hash ${hash}`);
    }
    const buffer = this._objects.get(hash);
    const actualHash = hashOf(buffer);
    if (actualHash !== hash) {
      throw new IntegrityError(hash);
    }
    return Buffer.from(buffer);
  }

  _normalizeTreeEntry(entry) {
    if (!entry || typeof entry.name !== 'string' || entry.name.length === 0) {
      throw new TypeError('each tree entry needs a non-empty name');
    }
    if (typeof entry.hash !== 'string' || entry.hash.length === 0) {
      throw new TypeError('each tree entry needs a hash');
    }
    const type = entry.type === 'tree' ? 'tree' : 'blob';
    return { name: entry.name, hash: entry.hash, type };
  }

  putTree(entries) {
    if (!Array.isArray(entries)) {
      throw new TypeError('entries must be an array');
    }
    const normalized = entries
      .map((entry) => this._normalizeTreeEntry(entry))
      .sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
    const names = new Set();
    for (const entry of normalized) {
      if (names.has(entry.name)) {
        throw new TypeError(`duplicate entry name in tree: ${entry.name}`);
      }
      names.add(entry.name);
    }
    const serialized = JSON.stringify(normalized);
    return this.put(serialized);
  }

  getTree(hash) {
    const buffer = this.get(hash);
    let parsed;
    try {
      parsed = JSON.parse(buffer.toString('utf8'));
    } catch (err) {
      throw new IntegrityError(hash);
    }
    if (!Array.isArray(parsed)) {
      throw new IntegrityError(hash);
    }
    return parsed.map((entry) => this._normalizeTreeEntry(entry));
  }

  walkTree(hash) {
    const entries = this.getTree(hash);
    const result = {};
    for (const entry of entries) {
      if (entry.type === 'tree') {
        result[entry.name] = this.walkTree(entry.hash);
      } else {
        result[entry.name] = this.get(entry.hash).toString('utf8');
      }
    }
    return result;
  }

  resolvePath(hash, path) {
    const parts = path.split('/').filter((part) => part.length > 0);
    let currentHash = hash;
    for (let i = 0; i < parts.length; i++) {
      const part = parts[i];
      const entries = this.getTree(currentHash);
      const match = entries.find((entry) => entry.name === part);
      if (!match) {
        throw new RangeError(`path segment not found: ${part}`);
      }
      const isLast = i === parts.length - 1;
      if (isLast) {
        return match.type === 'tree' ? this.walkTree(match.hash) : this.get(match.hash).toString('utf8');
      }
      if (match.type !== 'tree') {
        throw new RangeError(`path segment ${part} is a blob, not a tree`);
      }
      currentHash = match.hash;
    }
    return this.walkTree(currentHash);
  }
}

module.exports = { ContentAddressableStore, IntegrityError, hashOf };
