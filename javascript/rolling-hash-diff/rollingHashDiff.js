'use strict';

const crypto = require('crypto');

const MOD = 65536;
const DEFAULT_BLOCK_SIZE = 64;

function mod(n, m) {
  return ((n % m) + m) % m;
}

function computeWeakChecksum(buffer, start, length) {
  let a = 0;
  let b = 0;
  for (let i = 0; i < length; i++) {
    const byte = buffer[start + i];
    a += byte;
    b += (length - i) * byte;
  }
  a = mod(a, MOD);
  b = mod(b, MOD);
  return { a, b, hash: a + b * MOD };
}

function rollWeakChecksum(prev, oldByte, newByte, windowLength) {
  const a = mod(prev.a - oldByte + newByte, MOD);
  const b = mod(prev.b - windowLength * oldByte + a, MOD);
  return { a, b, hash: a + b * MOD };
}

function strongHashHex(buffer) {
  return crypto.createHash('sha256').update(buffer).digest('hex');
}

function buildBlockTable(oldData, blockSize) {
  const blocks = [];
  for (let start = 0, index = 0; start < oldData.length; start += blockSize, index++) {
    const length = Math.min(blockSize, oldData.length - start);
    const weak = computeWeakChecksum(oldData, start, length);
    const strongHex = strongHashHex(oldData.slice(start, start + length));
    blocks.push({ index, start, length, weak, strongHex });
  }
  return blocks;
}

function computeDelta(oldData, newData, blockSize = DEFAULT_BLOCK_SIZE) {
  if (!Buffer.isBuffer(oldData) || !Buffer.isBuffer(newData)) {
    throw new TypeError('oldData and newData must be Buffers');
  }
  if (!Number.isInteger(blockSize) || blockSize <= 0) {
    throw new RangeError('blockSize must be a positive integer');
  }

  const blocks = buildBlockTable(oldData, blockSize);
  const weakTable = new Map();
  for (const block of blocks) {
    if (!weakTable.has(block.weak.hash)) {
      weakTable.set(block.weak.hash, []);
    }
    weakTable.get(block.weak.hash).push(block);
  }

  const ops = [];
  let literalStart = 0;
  let pos = 0;
  let window = null;

  while (pos < newData.length) {
    const windowLen = Math.min(blockSize, newData.length - pos);
    if (!window || window.pos !== pos || window.len !== windowLen) {
      const checksum = computeWeakChecksum(newData, pos, windowLen);
      window = { pos, len: windowLen, a: checksum.a, b: checksum.b, hash: checksum.hash };
    }

    const candidates = weakTable.get(window.hash);
    let matchedBlock = null;
    if (candidates) {
      const strongHex = strongHashHex(newData.slice(pos, pos + windowLen));
      matchedBlock = candidates.find((c) => c.length === windowLen && c.strongHex === strongHex) || null;
    }

    if (matchedBlock) {
      if (pos > literalStart) {
        ops.push({ type: 'literal', data: newData.slice(literalStart, pos) });
      }
      ops.push({
        type: 'copy',
        index: matchedBlock.index,
        start: matchedBlock.start,
        length: matchedBlock.length,
      });
      pos += windowLen;
      literalStart = pos;
      window = null;
    } else {
      const canRollForward = pos + windowLen < newData.length;
      if (canRollForward) {
        const oldByte = newData[pos];
        const newByte = newData[pos + windowLen];
        const rolled = rollWeakChecksum(window, oldByte, newByte, windowLen);
        window = { pos: pos + 1, len: windowLen, a: rolled.a, b: rolled.b, hash: rolled.hash };
      } else {
        window = null;
      }
      pos += 1;
    }
  }

  if (newData.length > literalStart) {
    ops.push({ type: 'literal', data: newData.slice(literalStart, newData.length) });
  }

  return { blockSize, oldLength: oldData.length, newLength: newData.length, ops };
}

function applyDelta(oldData, delta) {
  if (!Buffer.isBuffer(oldData)) {
    throw new TypeError('oldData must be a Buffer');
  }
  const parts = [];
  for (const op of delta.ops) {
    if (op.type === 'copy') {
      parts.push(oldData.slice(op.start, op.start + op.length));
    } else if (op.type === 'literal') {
      parts.push(op.data);
    } else {
      throw new TypeError(`unknown delta op type: ${op.type}`);
    }
  }
  return Buffer.concat(parts);
}

function estimateDeltaSize(delta) {
  let total = 0;
  for (const op of delta.ops) {
    if (op.type === 'copy') {
      total += 9;
    } else {
      total += 5 + op.data.length;
    }
  }
  return total;
}

module.exports = {
  DEFAULT_BLOCK_SIZE,
  computeWeakChecksum,
  rollWeakChecksum,
  strongHashHex,
  buildBlockTable,
  computeDelta,
  applyDelta,
  estimateDeltaSize,
};
