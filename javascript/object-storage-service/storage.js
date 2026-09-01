'use strict';

const crypto = require('crypto');
const { ChunkStore } = require('./chunkStore.js');

class NoSuchBucketError extends RangeError {
  constructor(name) {
    super(`no such bucket: ${name}`);
    this.name = 'NoSuchBucketError';
  }
}

class NoSuchKeyError extends RangeError {
  constructor(key) {
    super(`no such object: ${key}`);
    this.name = 'NoSuchKeyError';
  }
}

class QuotaExceededError extends RangeError {
  constructor(bucket) {
    super(`quota exceeded for bucket ${bucket}`);
    this.name = 'QuotaExceededError';
  }
}

class ObjectStorage {
  constructor(rootDir, opts = {}) {
    this.rootDir = rootDir;
    this.chunkStore = new ChunkStore(rootDir);
    this.clock = opts.clock || (() => Date.now());
    this.buckets = new Map();
    this._versionCounter = 0;
  }

  _requireBucket(name) {
    const b = this.buckets.get(name);
    if (!b) {
      throw new NoSuchBucketError(name);
    }
    return b;
  }

  _nextVersionId() {
    this._versionCounter += 1;
    return `v${this._versionCounter}-${crypto.randomBytes(4).toString('hex')}`;
  }

  createBucket(name, opts = {}) {
    if (this.buckets.has(name)) {
      throw new Error(`bucket already exists: ${name}`);
    }
    this.buckets.set(name, {
      name,
      versioning: !!opts.versioning,
      quotaBytes: typeof opts.quotaBytes === 'number' ? opts.quotaBytes : null,
      lifecycleRules: Array.isArray(opts.lifecycleRules) ? opts.lifecycleRules : [],
      usedBytes: 0,
      objects: new Map(),
    });
  }

  configureBucket(name, opts = {}) {
    const b = this._requireBucket(name);
    if (typeof opts.versioning === 'boolean') {
      b.versioning = opts.versioning;
    }
    if (typeof opts.quotaBytes === 'number' || opts.quotaBytes === null) {
      b.quotaBytes = opts.quotaBytes;
    }
    if (Array.isArray(opts.lifecycleRules)) {
      b.lifecycleRules = opts.lifecycleRules;
    }
  }

  deleteBucket(name, opts = {}) {
    const b = this._requireBucket(name);
    const nonEmpty = Array.from(b.objects.values()).some((versions) => versions.length > 0);
    if (nonEmpty && !opts.force) {
      throw new Error(`bucket not empty: ${name}`);
    }
    if (nonEmpty) {
      for (const versions of b.objects.values()) {
        for (const v of versions) {
          if (!v.deleteMarker) {
            for (const h of v.chunks) {
              this.chunkStore.release(h);
            }
          }
        }
      }
    }
    this.buckets.delete(name);
  }

  listBuckets() {
    return Array.from(this.buckets.values()).map((b) => ({
      name: b.name,
      versioning: b.versioning,
      quotaBytes: b.quotaBytes,
      usedBytes: b.usedBytes,
      objectCount: b.objects.size,
    }));
  }

  _commitVersion(bucketName, key, chunkHashes, size, etag, contentType, metadata) {
    const b = this._requireBucket(bucketName);
    const existing = b.objects.get(key) || [];
    const isOverwrite = !b.versioning && existing.length > 0 && !existing[0].deleteMarker;
    const freedBytes = isOverwrite ? existing[0].size : 0;
    const projectedUsed = b.usedBytes - freedBytes + size;
    if (b.quotaBytes !== null && projectedUsed > b.quotaBytes) {
      throw new QuotaExceededError(bucketName);
    }
    const version = {
      versionId: this._nextVersionId(),
      chunks: chunkHashes,
      size,
      etag,
      contentType: contentType || null,
      metadata: metadata || {},
      createdAt: this.clock(),
      deleteMarker: false,
    };
    if (b.versioning) {
      existing.unshift(version);
    } else {
      if (existing.length > 0) {
        const old = existing[0];
        if (!old.deleteMarker) {
          for (const h of old.chunks) {
            this.chunkStore.release(h);
          }
        }
      }
      existing.length = 0;
      existing.push(version);
    }
    b.objects.set(key, existing);
    b.usedBytes = projectedUsed;
    return version;
  }

  putObject(bucket, key, buffer, opts = {}) {
    this._requireBucket(bucket);
    if (!Buffer.isBuffer(buffer)) {
      throw new TypeError('putObject requires a Buffer');
    }
    const { hash, size } = this.chunkStore.put(buffer);
    try {
      return this._commitVersion(bucket, key, [hash], size, hash, opts.contentType, opts.metadata);
    } catch (err) {
      this.chunkStore.release(hash);
      throw err;
    }
  }

  _resolveVersion(bucket, key, opts = {}) {
    const b = this._requireBucket(bucket);
    const versions = b.objects.get(key);
    if (!versions || versions.length === 0) {
      throw new NoSuchKeyError(key);
    }
    if (opts.versionId) {
      const found = versions.find((v) => v.versionId === opts.versionId);
      if (!found) {
        throw new NoSuchKeyError(`${key}@${opts.versionId}`);
      }
      return found;
    }
    const latest = versions[0];
    if (latest.deleteMarker) {
      throw new NoSuchKeyError(key);
    }
    return latest;
  }

  getObject(bucket, key, opts = {}) {
    const version = this._resolveVersion(bucket, key, opts);
    if (version.deleteMarker) {
      throw new NoSuchKeyError(`${key}@${version.versionId}`);
    }
    let body = Buffer.concat(version.chunks.map((h) => this.chunkStore.get(h)));
    if (opts.range) {
      const [start, end] = opts.range;
      const clampedEnd = Math.min(end, body.length - 1);
      body = body.subarray(start, clampedEnd + 1);
    }
    return {
      body,
      etag: version.etag,
      contentType: version.contentType,
      metadata: version.metadata,
      versionId: version.versionId,
      size: version.size,
      createdAt: version.createdAt,
    };
  }

  headObject(bucket, key, opts = {}) {
    const version = this._resolveVersion(bucket, key, opts);
    if (version.deleteMarker) {
      throw new NoSuchKeyError(`${key}@${version.versionId}`);
    }
    return {
      etag: version.etag,
      contentType: version.contentType,
      metadata: version.metadata,
      versionId: version.versionId,
      size: version.size,
      createdAt: version.createdAt,
    };
  }

  deleteObject(bucket, key, opts = {}) {
    const b = this._requireBucket(bucket);
    const versions = b.objects.get(key);
    if (!versions || versions.length === 0) {
      throw new NoSuchKeyError(key);
    }
    if (b.versioning) {
      if (opts.versionId) {
        const idx = versions.findIndex((v) => v.versionId === opts.versionId);
        if (idx === -1) {
          throw new NoSuchKeyError(`${key}@${opts.versionId}`);
        }
        const [removed] = versions.splice(idx, 1);
        if (!removed.deleteMarker) {
          for (const h of removed.chunks) {
            this.chunkStore.release(h);
          }
          b.usedBytes -= removed.size;
        }
        if (versions.length === 0) {
          b.objects.delete(key);
        }
        return { versionId: removed.versionId, deleteMarker: false };
      }
      const marker = {
        versionId: this._nextVersionId(),
        chunks: [],
        size: 0,
        etag: null,
        contentType: null,
        metadata: {},
        createdAt: this.clock(),
        deleteMarker: true,
      };
      versions.unshift(marker);
      return { versionId: marker.versionId, deleteMarker: true };
    }
    const current = versions[0];
    if (!current.deleteMarker) {
      for (const h of current.chunks) {
        this.chunkStore.release(h);
      }
      b.usedBytes -= current.size;
    }
    b.objects.delete(key);
    return { versionId: current.versionId, deleteMarker: false };
  }

  listObjects(bucket, opts = {}) {
    const b = this._requireBucket(bucket);
    const prefix = opts.prefix || '';
    const delimiter = opts.delimiter || null;
    const maxKeys = opts.maxKeys || 1000;
    const keys = Array.from(b.objects.keys())
      .filter((k) => {
        const versions = b.objects.get(k);
        return versions.length > 0 && !versions[0].deleteMarker && k.startsWith(prefix);
      })
      .sort();

    const entries = [];
    const seenPrefixes = new Set();
    for (const key of keys) {
      if (delimiter) {
        const rest = key.slice(prefix.length);
        const idx = rest.indexOf(delimiter);
        if (idx !== -1) {
          const commonPrefix = prefix + rest.slice(0, idx + delimiter.length);
          if (!seenPrefixes.has(commonPrefix)) {
            seenPrefixes.add(commonPrefix);
            entries.push({ type: 'prefix', value: commonPrefix });
          }
          continue;
        }
      }
      entries.push({ type: 'key', value: key });
    }

    let startIndex = 0;
    if (opts.continuationToken) {
      const idx = entries.findIndex((e) => e.value === opts.continuationToken);
      startIndex = idx === -1 ? 0 : idx + 1;
    }
    const page = entries.slice(startIndex, startIndex + maxKeys);
    const isTruncated = startIndex + maxKeys < entries.length;
    const nextContinuationToken = isTruncated ? page[page.length - 1].value : null;

    return {
      keys: page.filter((e) => e.type === 'key').map((e) => e.value),
      commonPrefixes: page.filter((e) => e.type === 'prefix').map((e) => e.value),
      isTruncated,
      nextContinuationToken,
    };
  }

  listObjectVersions(bucket, opts = {}) {
    const b = this._requireBucket(bucket);
    const prefix = opts.prefix || '';
    const keys = Array.from(b.objects.keys())
      .filter((k) => k.startsWith(prefix))
      .sort();
    const result = [];
    for (const key of keys) {
      const versions = b.objects.get(key);
      versions.forEach((v, idx) => {
        result.push({
          key,
          versionId: v.versionId,
          isLatest: idx === 0,
          deleteMarker: v.deleteMarker,
          size: v.size,
          etag: v.etag,
          createdAt: v.createdAt,
        });
      });
    }
    return result;
  }

  sweepExpired() {
    const now = this.clock();
    let swept = 0;
    for (const b of this.buckets.values()) {
      if (!b.lifecycleRules || b.lifecycleRules.length === 0) {
        continue;
      }
      for (const key of Array.from(b.objects.keys())) {
        const rule = b.lifecycleRules.find((r) => key.startsWith(r.prefix));
        if (!rule) {
          continue;
        }
        const versions = b.objects.get(key);
        const kept = [];
        for (const v of versions) {
          const age = now - v.createdAt;
          if (age >= rule.maxAgeMs) {
            if (!v.deleteMarker) {
              for (const h of v.chunks) {
                this.chunkStore.release(h);
              }
              b.usedBytes -= v.size;
            }
            swept += 1;
          } else {
            kept.push(v);
          }
        }
        if (kept.length === 0) {
          b.objects.delete(key);
        } else {
          b.objects.set(key, kept);
        }
      }
    }
    return swept;
  }
}

module.exports = { ObjectStorage, NoSuchBucketError, NoSuchKeyError, QuotaExceededError };
