'use strict';

const CRLF = '\r\n';

function encodeSimpleString(value) {
  return `+${value}${CRLF}`;
}

function encodeError(message) {
  return `-${message}${CRLF}`;
}

function encodeInteger(value) {
  return `:${value}${CRLF}`;
}

function encodeBulkString(value) {
  if (value === null || value === undefined) {
    return `$-1${CRLF}`;
  }
  const str = String(value);
  const byteLength = Buffer.byteLength(str, 'utf8');
  return `$${byteLength}${CRLF}${str}${CRLF}`;
}

function encodeArray(items) {
  if (items === null || items === undefined) {
    return `*-1${CRLF}`;
  }
  let out = `*${items.length}${CRLF}`;
  for (const item of items) {
    out += encodeValue(item);
  }
  return out;
}

function encodeValue(item) {
  if (item === null || item === undefined) {
    return encodeBulkString(null);
  }
  if (typeof item === 'object' && item.type) {
    switch (item.type) {
      case 'simple':
        return encodeSimpleString(item.value);
      case 'error':
        return encodeError(item.value);
      case 'integer':
        return encodeInteger(item.value);
      case 'bulk':
        return encodeBulkString(item.value);
      case 'array':
        return encodeArray(item.value);
      default:
        throw new Error(`unknown RESP type tag: ${item.type}`);
    }
  }
  if (typeof item === 'number' && Number.isInteger(item)) {
    return encodeInteger(item);
  }
  return encodeBulkString(item);
}

class RespDecoder {
  constructor() {
    this._buffer = Buffer.alloc(0);
  }

  append(chunk) {
    this._buffer = Buffer.concat([this._buffer, chunk]);
  }

  decodeNext() {
    const result = this._tryParse(this._buffer, 0);
    if (result === null) {
      return null;
    }
    this._buffer = this._buffer.subarray(result.next);
    return result.value;
  }

  _indexOfCRLF(buf, start) {
    const idx = buf.indexOf('\r\n', start, 'latin1');
    return idx;
  }

  _tryParse(buf, offset) {
    if (offset >= buf.length) {
      return null;
    }
    const type = String.fromCharCode(buf[offset]);
    const lineEnd = this._indexOfCRLF(buf, offset + 1);
    if (lineEnd === -1) {
      return null;
    }
    const line = buf.toString('latin1', offset + 1, lineEnd);
    const afterLine = lineEnd + 2;

    switch (type) {
      case '+':
        return { value: { type: 'simple', value: line }, next: afterLine };
      case '-':
        return { value: { type: 'error', value: line }, next: afterLine };
      case ':':
        return { value: { type: 'integer', value: parseInt(line, 10) }, next: afterLine };
      case '$': {
        const length = parseInt(line, 10);
        if (length === -1) {
          return { value: { type: 'bulk', value: null }, next: afterLine };
        }
        if (buf.length < afterLine + length + 2) {
          return null;
        }
        const strValue = buf.toString('utf8', afterLine, afterLine + length);
        return { value: { type: 'bulk', value: strValue }, next: afterLine + length + 2 };
      }
      case '*': {
        const count = parseInt(line, 10);
        if (count === -1) {
          return { value: { type: 'array', value: null }, next: afterLine };
        }
        let cursor = afterLine;
        const items = [];
        for (let i = 0; i < count; i++) {
          const parsed = this._tryParse(buf, cursor);
          if (parsed === null) {
            return null;
          }
          items.push(parsed.value);
          cursor = parsed.next;
        }
        return { value: { type: 'array', value: items }, next: cursor };
      }
      default:
        throw new Error(`unknown RESP type byte: ${type}`);
    }
  }
}

module.exports = {
  encodeSimpleString,
  encodeError,
  encodeInteger,
  encodeBulkString,
  encodeArray,
  encodeValue,
  RespDecoder,
};
