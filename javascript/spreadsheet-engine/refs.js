'use strict';

const { FormulaSyntaxError } = require('./errors.js');

const REF_PATTERN = /^(\$?)([A-Za-z]+)(\$?)([0-9]+)$/;
const MAX_RANGE_CELLS = 250000;

function columnToIndex(letters) {
  let index = 0;
  const upper = letters.toUpperCase();
  for (let i = 0; i < upper.length; i++) {
    index = index * 26 + (upper.charCodeAt(i) - 64);
  }
  return index;
}

function indexToColumn(index) {
  let n = index;
  let out = '';
  while (n > 0) {
    const remainder = (n - 1) % 26;
    out = String.fromCharCode(65 + remainder) + out;
    n = Math.floor((n - 1) / 26);
  }
  return out;
}

function parseRef(text) {
  const match = REF_PATTERN.exec(String(text).trim());
  if (!match) return null;
  const col = columnToIndex(match[2]);
  const row = Number(match[4]);
  if (!Number.isFinite(col) || !Number.isFinite(row)) return null;
  return {
    col,
    row,
    absCol: match[1] === '$',
    absRow: match[3] === '$',
  };
}

function requireRef(text) {
  const parsed = parseRef(text);
  if (!parsed) {
    throw new FormulaSyntaxError(`invalid cell reference: ${text}`);
  }
  return parsed;
}

function makeKey(col, row) {
  return indexToColumn(col) + row;
}

function keyOf(text) {
  const parsed = requireRef(text);
  return makeKey(parsed.col, parsed.row);
}

function rangeBounds(start, end) {
  return {
    minCol: Math.min(start.col, end.col),
    maxCol: Math.max(start.col, end.col),
    minRow: Math.min(start.row, end.row),
    maxRow: Math.max(start.row, end.row),
  };
}

function expandRange(start, end) {
  const bounds = rangeBounds(start, end);
  const width = bounds.maxCol - bounds.minCol + 1;
  const height = bounds.maxRow - bounds.minRow + 1;
  if (width * height > MAX_RANGE_CELLS) {
    throw new FormulaSyntaxError('range is too large to expand');
  }
  const keys = [];
  for (let row = bounds.minRow; row <= bounds.maxRow; row++) {
    for (let col = bounds.minCol; col <= bounds.maxCol; col++) {
      keys.push(makeKey(col, row));
    }
  }
  return keys;
}

module.exports = {
  columnToIndex,
  indexToColumn,
  parseRef,
  requireRef,
  makeKey,
  keyOf,
  rangeBounds,
  expandRange,
  MAX_RANGE_CELLS,
};
