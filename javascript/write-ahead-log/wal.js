'use strict';

const fs = require('fs');
const crypto = require('crypto');
const path = require('path');

const LENGTH_BYTES = 4;
const CHECKSUM_BYTES = 4;
const HEADER_BYTES = LENGTH_BYTES + CHECKSUM_BYTES;

function checksumOf(payload) {
  return crypto.createHash('sha256').update(payload).digest().subarray(0, CHECKSUM_BYTES);
}

function encodeRecord(record) {
  const payload = Buffer.from(JSON.stringify(record), 'utf8');
  const header = Buffer.alloc(HEADER_BYTES);
  header.writeUInt32LE(payload.length, 0);
  checksumOf(payload).copy(header, LENGTH_BYTES);
  return Buffer.concat([header, payload]);
}

class WriteAheadLog {
  constructor(filePath) {
    this.filePath = filePath;
    if (!fs.existsSync(this.filePath)) {
      fs.writeFileSync(this.filePath, Buffer.alloc(0));
    }
  }

  append(record) {
    const encoded = encodeRecord(record);
    const fd = fs.openSync(this.filePath, 'a');
    try {
      fs.writeSync(fd, encoded);
      fs.fsyncSync(fd);
    } finally {
      fs.closeSync(fd);
    }
    return encoded.length;
  }

  replay() {
    const buffer = fs.readFileSync(this.filePath);
    const records = [];
    let offset = 0;
    let corruptedAt = -1;

    while (offset < buffer.length) {
      const remaining = buffer.length - offset;
      if (remaining < HEADER_BYTES) {
        corruptedAt = offset;
        break;
      }

      const length = buffer.readUInt32LE(offset);
      const storedChecksum = buffer.subarray(offset + LENGTH_BYTES, offset + HEADER_BYTES);
      const payloadStart = offset + HEADER_BYTES;
      const payloadEnd = payloadStart + length;

      if (payloadEnd > buffer.length) {
        corruptedAt = offset;
        break;
      }

      const payload = buffer.subarray(payloadStart, payloadEnd);
      const actualChecksum = checksumOf(payload);
      if (!actualChecksum.equals(storedChecksum)) {
        corruptedAt = offset;
        break;
      }

      let record;
      try {
        record = JSON.parse(payload.toString('utf8'));
      } catch (err) {
        corruptedAt = offset;
        break;
      }

      records.push(record);
      offset = payloadEnd;
    }

    return {
      records,
      validBytes: offset,
      corrupted: corruptedAt !== -1,
      corruptedAtOffset: corruptedAt,
    };
  }

  compact(keepFromIndex) {
    const { records } = this.replay();
    const kept = records.slice(keepFromIndex);
    const tmpPath = path.join(
      path.dirname(this.filePath),
      `.${path.basename(this.filePath)}.compact.tmp`,
    );

    const chunks = kept.map((record) => encodeRecord(record));
    fs.writeFileSync(tmpPath, Buffer.concat(chunks));
    fs.renameSync(tmpPath, this.filePath);

    return kept.length;
  }

  size() {
    return fs.statSync(this.filePath).size;
  }
}

module.exports = { WriteAheadLog, encodeRecord, HEADER_BYTES, LENGTH_BYTES, CHECKSUM_BYTES };
