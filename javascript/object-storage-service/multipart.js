'use strict';

const crypto = require('crypto');

const MIN_PART_SIZE = 5 * 1024;

class MultipartManager {
  constructor(storage) {
    this.storage = storage;
    this.uploads = new Map();
  }

  createMultipartUpload(bucket, key, opts = {}) {
    this.storage._requireBucket(bucket);
    const uploadId = crypto.randomBytes(16).toString('hex');
    this.uploads.set(uploadId, {
      bucket,
      key,
      contentType: opts.contentType || null,
      metadata: opts.metadata || {},
      parts: new Map(),
      createdAt: this.storage.clock(),
    });
    return uploadId;
  }

  _requireUpload(uploadId) {
    const upload = this.uploads.get(uploadId);
    if (!upload) {
      throw new RangeError(`no such multipart upload: ${uploadId}`);
    }
    return upload;
  }

  uploadPart(uploadId, partNumber, buffer) {
    if (!Number.isInteger(partNumber) || partNumber < 1) {
      throw new RangeError('partNumber must be a positive integer');
    }
    if (!Buffer.isBuffer(buffer)) {
      throw new TypeError('uploadPart requires a Buffer');
    }
    const upload = this._requireUpload(uploadId);
    const { hash, size } = this.storage.chunkStore.put(buffer);
    const existing = upload.parts.get(partNumber);
    if (existing) {
      this.storage.chunkStore.release(existing.hash);
    }
    upload.parts.set(partNumber, { hash, size, etag: hash });
    return { partNumber, etag: hash, size };
  }

  abortMultipartUpload(uploadId) {
    const upload = this._requireUpload(uploadId);
    for (const part of upload.parts.values()) {
      this.storage.chunkStore.release(part.hash);
    }
    this.uploads.delete(uploadId);
  }

  completeMultipartUpload(uploadId, parts) {
    const upload = this._requireUpload(uploadId);
    if (!Array.isArray(parts) || parts.length === 0) {
      throw new RangeError('parts must be a non-empty array');
    }
    const sorted = [...parts].sort((a, b) => a.partNumber - b.partNumber);
    const maxPartNumber = Math.max(...upload.parts.keys());

    for (const { partNumber, etag } of sorted) {
      const stored = upload.parts.get(partNumber);
      if (!stored) {
        throw new RangeError(`missing uploaded part: ${partNumber}`);
      }
      if (stored.etag !== etag) {
        throw new RangeError(`etag mismatch for part ${partNumber}`);
      }
      const isLast = partNumber === maxPartNumber;
      if (!isLast && stored.size < MIN_PART_SIZE) {
        throw new RangeError(`part ${partNumber} is smaller than the minimum part size`);
      }
    }

    const chunkHashes = sorted.map((p) => upload.parts.get(p.partNumber).hash);
    const totalSize = sorted.reduce((sum, p) => sum + upload.parts.get(p.partNumber).size, 0);
    const combinedEtags = sorted.map((p) => upload.parts.get(p.partNumber).etag).join('');
    const etag = `${crypto.createHash('sha256').update(combinedEtags).digest('hex')}-${sorted.length}`;

    const version = this.storage._commitVersion(
      upload.bucket,
      upload.key,
      chunkHashes,
      totalSize,
      etag,
      upload.contentType,
      upload.metadata,
    );

    this.uploads.delete(uploadId);
    return version;
  }
}

module.exports = { MultipartManager, MIN_PART_SIZE };
