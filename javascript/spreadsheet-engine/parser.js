'use strict';

const { tokenize } = require('./tokenizer.js');
const { FormulaSyntaxError } = require('./errors.js');
const { parseRef } = require('./refs.js');

const COMPARISON_OPERATORS = ['=', '<>', '<', '<=', '>', '>='];

class Parser {
  constructor(tokens) {
    this.tokens = tokens;
    this.index = 0;
  }

  peek(offset) {
    return this.tokens[this.index + (offset || 0)];
  }

  next() {
    return this.tokens[this.index++];
  }

  atOperator(values) {
    const token = this.peek();
    return token.type === 'operator' && values.indexOf(token.value) !== -1;
  }

  expect(type, description) {
    const token = this.peek();
    if (token.type !== type) {
      throw new FormulaSyntaxError(
        `expected ${description} but found ${token.value === null ? 'end of formula' : token.value}`,
        token.position
      );
    }
    return this.next();
  }

  parseFormula() {
    const node = this.parseExpression();
    const token = this.peek();
    if (token.type !== 'eof') {
      throw new FormulaSyntaxError(`unexpected trailing input: ${token.value}`, token.position);
    }
    return node;
  }

  parseExpression() {
    return this.parseComparison();
  }

  parseComparison() {
    let left = this.parseConcat();
    while (this.atOperator(COMPARISON_OPERATORS)) {
      const op = this.next().value;
      const right = this.parseConcat();
      left = { type: 'binary', op, left, right };
    }
    return left;
  }

  parseConcat() {
    let left = this.parseAdditive();
    while (this.atOperator(['&'])) {
      const op = this.next().value;
      const right = this.parseAdditive();
      left = { type: 'binary', op, left, right };
    }
    return left;
  }

  parseAdditive() {
    let left = this.parseMultiplicative();
    while (this.atOperator(['+', '-'])) {
      const op = this.next().value;
      const right = this.parseMultiplicative();
      left = { type: 'binary', op, left, right };
    }
    return left;
  }

  parseMultiplicative() {
    let left = this.parsePower();
    while (this.atOperator(['*', '/'])) {
      const op = this.next().value;
      const right = this.parsePower();
      left = { type: 'binary', op, left, right };
    }
    return left;
  }

  parsePower() {
    const left = this.parseUnary();
    if (this.atOperator(['^'])) {
      this.next();
      const right = this.parsePower();
      return { type: 'binary', op: '^', left, right };
    }
    return left;
  }

  parseUnary() {
    if (this.atOperator(['-', '+'])) {
      const op = this.next().value;
      const operand = this.parseUnary();
      if (op === '+') return operand;
      return { type: 'unary', op, operand };
    }
    return this.parsePostfix();
  }

  parsePostfix() {
    let node = this.parsePrimary();
    while (this.atOperator(['%'])) {
      this.next();
      node = { type: 'unary', op: '%', operand: node };
    }
    return node;
  }

  parsePrimary() {
    const token = this.peek();
    if (token.type === 'number') {
      this.next();
      return { type: 'number', value: token.value };
    }
    if (token.type === 'string') {
      this.next();
      return { type: 'string', value: token.value };
    }
    if (token.type === 'error') {
      this.next();
      return { type: 'error', code: token.value };
    }
    if (token.type === 'lparen') {
      this.next();
      const inner = this.parseExpression();
      this.expect('rparen', "')'");
      return inner;
    }
    if (token.type === 'name') {
      return this.parseName();
    }
    throw new FormulaSyntaxError(
      `unexpected ${token.value === null ? 'end of formula' : token.value}`,
      token.position
    );
  }

  parseName() {
    const token = this.next();
    if (this.peek().type === 'lparen') {
      return this.parseCall(token);
    }
    const upper = token.value.toUpperCase();
    if (upper === 'TRUE' || upper === 'FALSE') {
      return { type: 'boolean', value: upper === 'TRUE' };
    }
    const ref = parseRef(token.value);
    if (ref) {
      if (this.peek().type === 'colon') {
        this.next();
        const endToken = this.expect('name', 'a cell reference');
        const endRef = parseRef(endToken.value);
        if (!endRef) {
          throw new FormulaSyntaxError(
            `invalid range end: ${endToken.value}`,
            endToken.position
          );
        }
        return { type: 'range', start: ref, end: endRef };
      }
      return { type: 'ref', col: ref.col, row: ref.row, absCol: ref.absCol, absRow: ref.absRow };
    }
    if (this.peek().type === 'colon') {
      throw new FormulaSyntaxError(`invalid range start: ${token.value}`, token.position);
    }
    return { type: 'name', name: upper };
  }

  parseCall(token) {
    this.expect('lparen', "'('");
    const args = [];
    if (this.peek().type !== 'rparen') {
      args.push(this.parseExpression());
      while (this.peek().type === 'comma') {
        this.next();
        args.push(this.parseExpression());
      }
    }
    this.expect('rparen', "')'");
    return { type: 'call', name: token.value.toUpperCase(), args };
  }
}

function parse(source) {
  const parser = new Parser(tokenize(source));
  return parser.parseFormula();
}

module.exports = { parse, Parser };
