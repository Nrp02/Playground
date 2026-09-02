'use strict';

const { CellError, isError, VALUE } = require('./errors.js');

function makeRange(values, keys) {
  return { kind: 'range', values, keys };
}

function isRange(value) {
  return value !== null && typeof value === 'object' && value.kind === 'range';
}

function toNumber(value) {
  if (isError(value)) return value;
  if (value === null || value === undefined) return 0;
  if (typeof value === 'number') return value;
  if (typeof value === 'boolean') return value ? 1 : 0;
  if (typeof value === 'string') {
    const trimmed = value.trim();
    if (trimmed === '') return new CellError(VALUE);
    const parsed = Number(trimmed);
    if (Number.isNaN(parsed)) return new CellError(VALUE);
    return parsed;
  }
  return new CellError(VALUE);
}

function toText(value) {
  if (isError(value)) return value;
  if (value === null || value === undefined) return '';
  if (typeof value === 'boolean') return value ? 'TRUE' : 'FALSE';
  if (typeof value === 'number') return formatNumber(value);
  if (typeof value === 'string') return value;
  return new CellError(VALUE);
}

function toBoolean(value) {
  if (isError(value)) return value;
  if (value === null || value === undefined) return false;
  if (typeof value === 'boolean') return value;
  if (typeof value === 'number') return value !== 0;
  if (typeof value === 'string') {
    const upper = value.trim().toUpperCase();
    if (upper === 'TRUE') return true;
    if (upper === 'FALSE') return false;
    return new CellError(VALUE);
  }
  return new CellError(VALUE);
}

function typeRank(value) {
  if (typeof value === 'number') return 0;
  if (typeof value === 'string') return 1;
  return 2;
}

function compareValues(left, right) {
  const a = left === null || left === undefined ? 0 : left;
  const b = right === null || right === undefined ? 0 : right;
  const rankA = typeRank(a);
  const rankB = typeRank(b);
  if (rankA !== rankB) return rankA < rankB ? -1 : 1;
  if (rankA === 1) {
    const upperA = a.toUpperCase();
    const upperB = b.toUpperCase();
    if (upperA === upperB) return 0;
    return upperA < upperB ? -1 : 1;
  }
  const numA = rankA === 2 ? (a ? 1 : 0) : a;
  const numB = rankB === 2 ? (b ? 1 : 0) : b;
  if (numA === numB) return 0;
  return numA < numB ? -1 : 1;
}

function formatNumber(value) {
  if (!Number.isFinite(value)) return String(value);
  if (Number.isInteger(value)) return String(value);
  const rounded = Number(value.toFixed(10));
  return String(rounded);
}

function formatValue(value) {
  if (isError(value)) return value.code;
  if (isRange(value)) return '<range>';
  if (value === null || value === undefined) return '';
  if (typeof value === 'boolean') return value ? 'TRUE' : 'FALSE';
  if (typeof value === 'number') return formatNumber(value);
  return String(value);
}

module.exports = {
  makeRange,
  isRange,
  toNumber,
  toText,
  toBoolean,
  compareValues,
  formatNumber,
  formatValue,
};
