'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

class IntegrityError extends Error {
  constructor(hash) {
    super(`chunk ${hash} failed integrity verification`);
    this.name = 'IntegrityError';
    this.hash = hash;
  }
}

function sha256(buffer) {
  return crypto.createHash('sha256').update(buffer).digest('hex');
}

class ChunkStore {
  constructor(rootDir) {
    this.rootDir = rootDir;
    this.chunksDir = path.join(rootDir, 'chunks');
    fs.mkdirSync(this.chunksDir, { recursive: true });
    this.refcounts = new Map();
  }

  chunkPath(hash) {
    return path.join(this.chunksDir, hash);
  }

  put(buffer) {
    if (!Buffer.isBuffer(buffer)) {
      throw new TypeError('put requires a Buffer');
    }
    const hash = sha256(buffer);
    const filePath = this.chunkPath(hash);
    if (!this.refcounts.has(hash)) {
      if (!fs.existsSync(filePath)) {
        fs.writeFileSync(filePath, buffer);
      }
      this.refcounts.set(hash, 0);
    }
    this.refcounts.set(hash, this.refcounts.get(hash) + 1);
    return { hash, size: buffer.length };
  }

  addRef(hash) {
    if (!this.refcounts.has(hash)) {
      throw new RangeError(`unknown chunk ${hash}`);
    }
    this.refcounts.set(hash, this.refcounts.get(hash) + 1);
  }

  release(hash) {
    if (!this.refcounts.has(hash)) {
      return;
    }
    const next = this.refcounts.get(hash) - 1;
    if (next <= 0) {
      this.refcounts.delete(hash);
      const filePath = this.chunkPath(hash);
      if (fs.existsSync(filePath)) {
        fs.unlinkSync(filePath);
      }
    } else {
      this.refcounts.set(hash, next);
    }
  }

  refcount(hash) {
    return this.refcounts.get(hash) || 0;
  }

  has(hash) {
    return this.refcounts.has(hash);
  }

  size() {
    return this.refcounts.size;
  }

  get(hash) {
    const filePath = this.chunkPath(hash);
    if (!fs.existsSync(filePath)) {
      throw new RangeError(`no chunk stored for ${hash}`);
    }
    const buffer = fs.readFileSync(filePath);
    const actual = sha256(buffer);
    if (actual !== hash) {
      throw new IntegrityError(hash);
    }
    return buffer;
  }
}

module.exports = { ChunkStore, IntegrityError, sha256 };
