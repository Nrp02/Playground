'use strict';

const { FormulaSyntaxError, ERROR_CODES } = require('./errors.js');

const TWO_CHAR_OPERATORS = ['<=', '>=', '<>'];
const ONE_CHAR_OPERATORS = ['+', '-', '*', '/', '^', '&', '=', '<', '>', '%'];

function isDigit(ch) {
  return ch >= '0' && ch <= '9';
}

function isLetter(ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

function isNameStart(ch) {
  return isLetter(ch) || ch === '_' || ch === '$';
}

function isNamePart(ch) {
  return isLetter(ch) || isDigit(ch) || ch === '_' || ch === '$' || ch === '.';
}

function readString(input, start) {
  let i = start + 1;
  let out = '';
  while (i < input.length) {
    const ch = input[i];
    if (ch === '"') {
      if (input[i + 1] === '"') {
        out += '"';
        i += 2;
        continue;
      }
      return { value: out, next: i + 1 };
    }
    out += ch;
    i++;
  }
  throw new FormulaSyntaxError('unterminated string literal', start);
}

function readNumber(input, start) {
  let i = start;
  while (i < input.length && isDigit(input[i])) i++;
  if (input[i] === '.') {
    i++;
    while (i < input.length && isDigit(input[i])) i++;
  }
  if (input[i] === 'e' || input[i] === 'E') {
    let j = i + 1;
    if (input[j] === '+' || input[j] === '-') j++;
    if (isDigit(input[j])) {
      j++;
      while (j < input.length && isDigit(input[j])) j++;
      i = j;
    }
  }
  const text = input.slice(start, i);
  const value = Number(text);
  if (Number.isNaN(value)) {
    throw new FormulaSyntaxError(`invalid number literal: ${text}`, start);
  }
  return { value, next: i };
}

function readErrorLiteral(input, start) {
  for (const code of ERROR_CODES) {
    if (input.startsWith(code, start)) {
      return { value: code, next: start + code.length };
    }
  }
  throw new FormulaSyntaxError('unknown error literal', start);
}

function tokenize(input) {
  const tokens = [];
  let i = 0;
  while (i < input.length) {
    const ch = input[i];
    if (ch === ' ' || ch === '\t' || ch === '\n' || ch === '\r') {
      i++;
      continue;
    }
    if (ch === '"') {
      const read = readString(input, i);
      tokens.push({ type: 'string', value: read.value, position: i });
      i = read.next;
      continue;
    }
    if (isDigit(ch) || (ch === '.' && isDigit(input[i + 1]))) {
      const read = readNumber(input, i);
      tokens.push({ type: 'number', value: read.value, position: i });
      i = read.next;
      continue;
    }
    if (ch === '#') {
      const read = readErrorLiteral(input, i);
      tokens.push({ type: 'error', value: read.value, position: i });
      i = read.next;
      continue;
    }
    if (isNameStart(ch)) {
      let j = i;
      while (j < input.length && isNamePart(input[j])) j++;
      tokens.push({ type: 'name', value: input.slice(i, j), position: i });
      i = j;
      continue;
    }
    if (ch === '(') {
      tokens.push({ type: 'lparen', value: ch, position: i });
      i++;
      continue;
    }
    if (ch === ')') {
      tokens.push({ type: 'rparen', value: ch, position: i });
      i++;
      continue;
    }
    if (ch === ',' || ch === ';') {
      tokens.push({ type: 'comma', value: ',', position: i });
      i++;
      continue;
    }
    if (ch === ':') {
      tokens.push({ type: 'colon', value: ch, position: i });
      i++;
      continue;
    }
    const pair = input.slice(i, i + 2);
    if (TWO_CHAR_OPERATORS.indexOf(pair) !== -1) {
      tokens.push({ type: 'operator', value: pair, position: i });
      i += 2;
      continue;
    }
    if (ONE_CHAR_OPERATORS.indexOf(ch) !== -1) {
      tokens.push({ type: 'operator', value: ch, position: i });
      i++;
      continue;
    }
    throw new FormulaSyntaxError(`unexpected character: ${ch}`, i);
  }
  tokens.push({ type: 'eof', value: null, position: input.length });
  return tokens;
}

module.exports = { tokenize };
