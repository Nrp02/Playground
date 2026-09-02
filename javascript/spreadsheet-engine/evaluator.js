'use strict';

const { CellError, isError, DIV0, VALUE, NAME, NUM } = require('./errors.js');
const { makeRange, isRange, toNumber, toText, compareValues } = require('./values.js');
const { callFunction } = require('./functions.js');
const { rangeBounds, makeKey, expandRange } = require('./refs.js');

function readRange(node, ctx) {
  const bounds = rangeBounds(node.start, node.end);
  const values = [];
  const keys = [];
  for (let row = bounds.minRow; row <= bounds.maxRow; row++) {
    for (let col = bounds.minCol; col <= bounds.maxCol; col++) {
      const value = ctx.readCell(col, row);
      if (isError(value) && value.code === '#REF!') return value;
      values.push(value);
      keys.push(makeKey(col, row));
    }
  }
  return makeRange(values, keys);
}

function applyArithmetic(op, left, right) {
  const a = toNumber(left);
  if (isError(a)) return a;
  const b = toNumber(right);
  if (isError(b)) return b;
  let result;
  switch (op) {
    case '+':
      result = a + b;
      break;
    case '-':
      result = a - b;
      break;
    case '*':
      result = a * b;
      break;
    case '/':
      if (b === 0) return new CellError(DIV0);
      result = a / b;
      break;
    case '^':
      if (a === 0 && b < 0) return new CellError(DIV0);
      result = Math.pow(a, b);
      break;
    default:
      return new CellError(VALUE);
  }
  if (Number.isNaN(result)) return new CellError(NUM);
  if (!Number.isFinite(result)) return new CellError(NUM);
  return result;
}

function applyComparison(op, left, right) {
  const order = compareValues(left, right);
  switch (op) {
    case '=':
      return order === 0;
    case '<>':
      return order !== 0;
    case '<':
      return order < 0;
    case '<=':
      return order <= 0;
    case '>':
      return order > 0;
    case '>=':
      return order >= 0;
    default:
      return new CellError(VALUE);
  }
}

function evaluate(node, ctx) {
  switch (node.type) {
    case 'number':
      return node.value;
    case 'string':
      return node.value;
    case 'boolean':
      return node.value;
    case 'error':
      return new CellError(node.code);
    case 'name':
      return new CellError(NAME);
    case 'ref':
      return ctx.readCell(node.col, node.row);
    case 'range':
      return readRange(node, ctx);
    case 'unary':
      return evaluateUnary(node, ctx);
    case 'binary':
      return evaluateBinary(node, ctx);
    case 'call':
      return callFunction(node.name, node.args, ctx, evaluate);
    default:
      return new CellError(VALUE);
  }
}

function evaluateUnary(node, ctx) {
  const operand = evaluate(node.operand, ctx);
  if (isError(operand)) return operand;
  if (isRange(operand)) return new CellError(VALUE);
  const num = toNumber(operand);
  if (isError(num)) return num;
  if (node.op === '-') return -num;
  if (node.op === '%') return num / 100;
  return num;
}

function evaluateBinary(node, ctx) {
  const left = evaluate(node.left, ctx);
  if (isError(left)) return left;
  const right = evaluate(node.right, ctx);
  if (isError(right)) return right;
  if (isRange(left) || isRange(right)) return new CellError(VALUE);
  if (node.op === '&') {
    const a = toText(left);
    if (isError(a)) return a;
    const b = toText(right);
    if (isError(b)) return b;
    return a + b;
  }
  if (['=', '<>', '<', '<=', '>', '>='].indexOf(node.op) !== -1) {
    return applyComparison(node.op, left, right);
  }
  return applyArithmetic(node.op, left, right);
}

function collectPrecedents(node, sink) {
  if (!node || typeof node !== 'object') return sink;
  switch (node.type) {
    case 'ref':
      sink.add(makeKey(node.col, node.row));
      break;
    case 'range':
      for (const key of expandRange(node.start, node.end)) sink.add(key);
      break;
    case 'unary':
      collectPrecedents(node.operand, sink);
      break;
    case 'binary':
      collectPrecedents(node.left, sink);
      collectPrecedents(node.right, sink);
      break;
    case 'call':
      for (const arg of node.args) collectPrecedents(arg, sink);
      break;
    default:
      break;
  }
  return sink;
}

module.exports = { evaluate, collectPrecedents, applyArithmetic, applyComparison };
