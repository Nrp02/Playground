'use strict';

const assert = require('assert');
const { tokenize } = require('./tokenizer.js');
const { parse } = require('./parser.js');
const { FormulaSyntaxError } = require('./errors.js');
const { columnToIndex, indexToColumn, parseRef, expandRange } = require('./refs.js');
const { Sheet } = require('./sheet.js');

let passCount = 0;
let failCount = 0;

function test(name, fn) {
  try {
    fn();
    passCount++;
    console.log(`  PASS  ${name}`);
  } catch (err) {
    failCount++;
    console.log(`  FAIL  ${name}`);
    console.log(`        ${err.message}`);
  }
}

function evalExpr(source, data) {
  const sheet = new Sheet();
  if (data) sheet.setCells(data);
  sheet.setCell('Z900', `=${source}`);
  return sheet.getDisplay('Z900');
}

test('tokenizes numbers, names, operators and punctuation', () => {
  const kinds = tokenize('SUM(A1:B2, -3.5) >= 2').map((t) => t.type);
  assert.deepStrictEqual(kinds, [
    'name',
    'lparen',
    'name',
    'colon',
    'name',
    'comma',
    'operator',
    'number',
    'rparen',
    'operator',
    'number',
    'eof',
  ]);
});

test('tokenizes string literals with doubled-quote escapes', () => {
  const tokens = tokenize('"say ""hi"" now"');
  assert.strictEqual(tokens[0].type, 'string');
  assert.strictEqual(tokens[0].value, 'say "hi" now');
});

test('tokenizes scientific and leading-dot number literals', () => {
  assert.strictEqual(tokenize('1e3')[0].value, 1000);
  assert.strictEqual(tokenize('.5')[0].value, 0.5);
  assert.strictEqual(tokenize('1.5e-2')[0].value, 0.015);
});

test('tokenizes error literals', () => {
  const tokens = tokenize('#DIV/0!');
  assert.strictEqual(tokens[0].type, 'error');
  assert.strictEqual(tokens[0].value, '#DIV/0!');
});

test('multiplication binds tighter than addition', () => {
  assert.strictEqual(evalExpr('2+3*4'), '14');
  assert.strictEqual(evalExpr('(2+3)*4'), '20');
});

test('subtraction and division are left associative', () => {
  assert.strictEqual(evalExpr('10-2-3'), '5');
  assert.strictEqual(evalExpr('100/10/2'), '5');
});

test('exponentiation is right associative: 2^3^2 is 512', () => {
  assert.strictEqual(evalExpr('2^3^2'), '512');
  const ast = parse('2^3^2');
  assert.strictEqual(ast.op, '^');
  assert.strictEqual(ast.left.value, 2);
  assert.strictEqual(ast.right.op, '^');
});

test('exponentiation binds tighter than multiplication', () => {
  assert.strictEqual(evalExpr('2*3^2'), '18');
});

test('unary minus binds tighter than ^ (spreadsheet semantics): -2^2 is 4', () => {
  assert.strictEqual(evalExpr('-2^2'), '4');
  assert.strictEqual(evalExpr('0-2^2'), '-4');
  assert.strictEqual(evalExpr('2^-3'), '0.125');
});

test('stacked unary minus and unary on references', () => {
  assert.strictEqual(evalExpr('--5'), '5');
  assert.strictEqual(evalExpr('-3+5'), '2');
  assert.strictEqual(evalExpr('-A1', { A1: 7 }), '-7');
});

test('postfix percent divides by one hundred', () => {
  assert.strictEqual(evalExpr('50%'), '0.5');
  assert.strictEqual(evalExpr('200*8.5%'), '17');
});

test('comparison operators sit below arithmetic and concatenation', () => {
  assert.strictEqual(evalExpr('1+2<4'), 'TRUE');
  assert.strictEqual(evalExpr('2*3=6'), 'TRUE');
  assert.strictEqual(evalExpr('5<>4'), 'TRUE');
  assert.strictEqual(evalExpr('3>=4'), 'FALSE');
  assert.strictEqual(evalExpr('"a"&"b"="ab"'), 'TRUE');
});

test('string concatenation is left associative and coerces values', () => {
  assert.strictEqual(evalExpr('"a"&"b"&"c"'), 'abc');
  assert.strictEqual(evalExpr('1&2'), '12');
  assert.strictEqual(evalExpr('"n="&A1', { A1: 4 }), 'n=4');
  assert.strictEqual(evalExpr('"x"&A1', {}), 'x');
});

test('string comparison is case insensitive', () => {
  assert.strictEqual(evalExpr('"ABC"="abc"'), 'TRUE');
  assert.strictEqual(evalExpr('"apple"<"banana"'), 'TRUE');
});

test('column letters convert in both directions, including multi-letter columns', () => {
  assert.strictEqual(columnToIndex('A'), 1);
  assert.strictEqual(columnToIndex('Z'), 26);
  assert.strictEqual(columnToIndex('AA'), 27);
  assert.strictEqual(columnToIndex('AZ'), 52);
  assert.strictEqual(columnToIndex('BA'), 53);
  assert.strictEqual(columnToIndex('ZZ'), 702);
  assert.strictEqual(columnToIndex('AAA'), 703);
  for (const index of [1, 26, 27, 52, 702, 703, 16384]) {
    assert.strictEqual(columnToIndex(indexToColumn(index)), index);
  }
});

test('parses multi-letter A1 references', () => {
  assert.deepStrictEqual(parseRef('AA3'), { col: 27, row: 3, absCol: false, absRow: false });
  assert.deepStrictEqual(parseRef('B12'), { col: 2, row: 12, absCol: false, absRow: false });
  assert.strictEqual(parseRef('12A'), null);
  assert.strictEqual(parseRef('A'), null);
  assert.strictEqual(parseRef('SUM'), null);
});

test('parses absolute markers and treats them as the same cell', () => {
  assert.deepStrictEqual(parseRef('$AA$3'), { col: 27, row: 3, absCol: true, absRow: true });
  assert.deepStrictEqual(parseRef('$B12'), { col: 2, row: 12, absCol: true, absRow: false });
  const sheet = new Sheet();
  sheet.setCell('AA3', 9);
  sheet.setCell('B1', '=$AA$3+AA3+$AA3+AA$3');
  assert.strictEqual(sheet.getDisplay('B1'), '36');
  assert.deepStrictEqual(sheet.getPrecedents('B1'), ['AA3']);
});

test('range expansion covers the enclosing rectangle in row-major order', () => {
  const keys = expandRange({ col: 1, row: 1 }, { col: 2, row: 3 });
  assert.deepStrictEqual(keys, ['A1', 'B1', 'A2', 'B2', 'A3', 'B3']);
  const reversed = expandRange({ col: 2, row: 3 }, { col: 1, row: 1 });
  assert.deepStrictEqual(reversed, keys);
});

test('range expansion works across multi-letter columns', () => {
  const keys = expandRange({ col: 26, row: 1 }, { col: 28, row: 1 });
  assert.deepStrictEqual(keys, ['Z1', 'AA1', 'AB1']);
});

test('parses a call node with a range argument and a scalar argument', () => {
  const ast = parse('SUM(A1:B2, 3)');
  assert.strictEqual(ast.type, 'call');
  assert.strictEqual(ast.name, 'SUM');
  assert.strictEqual(ast.args.length, 2);
  assert.strictEqual(ast.args[0].type, 'range');
  assert.deepStrictEqual(ast.args[0].start, { col: 1, row: 1, absCol: false, absRow: false });
  assert.deepStrictEqual(ast.args[0].end, { col: 2, row: 2, absCol: false, absRow: false });
  assert.strictEqual(ast.args[1].type, 'number');
});

test('parses booleans and nested calls', () => {
  assert.strictEqual(parse('TRUE').type, 'boolean');
  assert.strictEqual(parse('false').value, false);
  const ast = parse('IF(AND(TRUE,FALSE),1,MAX(1,2))');
  assert.strictEqual(ast.args[0].name, 'AND');
  assert.strictEqual(ast.args[2].name, 'MAX');
});

test('function names are case insensitive and only a following paren makes a call', () => {
  assert.strictEqual(evalExpr('sum(1,2)'), '3');
  assert.strictEqual(evalExpr('Sum(1,2)'), '3');
  assert.strictEqual(parse('LOG10(1)').type, 'call');
  const bare = parse('LOG10');
  assert.strictEqual(bare.type, 'ref');
  assert.strictEqual(bare.col, columnToIndex('LOG'));
  assert.strictEqual(bare.row, 10);
  assert.strictEqual(parse('HELLO').type, 'name');
});

test('rejects malformed formulas with a syntax error', () => {
  assert.throws(() => parse('1+'), FormulaSyntaxError);
  assert.throws(() => parse('SUM(1,)'), FormulaSyntaxError);
  assert.throws(() => parse('(1'), FormulaSyntaxError);
  assert.throws(() => parse('"unterminated'), FormulaSyntaxError);
  assert.throws(() => parse('1 2'), FormulaSyntaxError);
  assert.throws(() => parse('A1:'), FormulaSyntaxError);
  assert.throws(() => parse('@A1'), FormulaSyntaxError);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
