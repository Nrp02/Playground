'use strict';

const MESSAGE_TYPES = Object.freeze({
  REQUEST: 1,
  RESPONSE: 2,
  ERROR: 3,
});

const HEADER_LENGTH_FIELD_SIZE = 4;
const TYPE_FIELD_SIZE = 1;
const CORR_ID_FIELD_SIZE = 4;
const BODY_HEADER_SIZE = TYPE_FIELD_SIZE + CORR_ID_FIELD_SIZE;
const DEFAULT_MAX_FRAME_SIZE = 16 * 1024 * 1024;

function encodeFrame(type, corrId, payload) {
  const body = payload || Buffer.alloc(0);
  const header = Buffer.alloc(HEADER_LENGTH_FIELD_SIZE + BODY_HEADER_SIZE);
  header.writeUInt32BE(BODY_HEADER_SIZE + body.length, 0);
  header.writeUInt8(type, HEADER_LENGTH_FIELD_SIZE);
  header.writeUInt32BE(corrId >>> 0, HEADER_LENGTH_FIELD_SIZE + TYPE_FIELD_SIZE);
  return Buffer.concat([header, body]);
}

class FrameDecoder {
  constructor(maxFrameSize = DEFAULT_MAX_FRAME_SIZE) {
    this._maxFrameSize = maxFrameSize;
    this._chunks = [];
    this._length = 0;
  }

  append(chunk) {
    this._chunks.push(chunk);
    this._length += chunk.length;
  }

  _consolidate() {
    if (this._chunks.length > 1) {
      const combined = Buffer.concat(this._chunks, this._length);
      this._chunks = [combined];
    }
  }

  decodeNext() {
    if (this._length < HEADER_LENGTH_FIELD_SIZE) return null;
    this._consolidate();
    const buf = this._chunks[0];
    const bodyLength = buf.readUInt32BE(0);
    if (bodyLength > this._maxFrameSize) {
      throw new Error(`frame exceeds maximum size: ${bodyLength} > ${this._maxFrameSize}`);
    }
    if (bodyLength < BODY_HEADER_SIZE) {
      throw new Error(`corrupt frame: body length ${bodyLength} smaller than header`);
    }
    const totalLength = HEADER_LENGTH_FIELD_SIZE + bodyLength;
    if (this._length < totalLength) return null;
    const type = buf.readUInt8(HEADER_LENGTH_FIELD_SIZE);
    const corrId = buf.readUInt32BE(HEADER_LENGTH_FIELD_SIZE + TYPE_FIELD_SIZE);
    const payload = buf.subarray(HEADER_LENGTH_FIELD_SIZE + BODY_HEADER_SIZE, totalLength);
    const rest = buf.subarray(totalLength);
    this._chunks = rest.length ? [rest] : [];
    this._length = rest.length;
    return { type, corrId, payload };
  }
}

module.exports = {
  MESSAGE_TYPES,
  DEFAULT_MAX_FRAME_SIZE,
  encodeFrame,
  FrameDecoder,
};
