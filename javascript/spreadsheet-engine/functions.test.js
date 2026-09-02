'use strict';

const assert = require('assert');
const { Sheet } = require('./sheet.js');
const { define, isDefined, functionNames } = require('./functions.js');

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

function sheetWith(data) {
  const sheet = new Sheet();
  if (data) sheet.setCells(data);
  return sheet;
}

function evalExpr(source, data) {
  const sheet = sheetWith(data);
  sheet.setCell('Z900', `=${source}`);
  return sheet.getDisplay('Z900');
}

const NUMBERS = { A1: 4, A2: 10, A3: 2, A4: 8 };
const MIXED = { A1: 4, A3: 'text', A4: 8 };

test('SUM adds scalars, ranges and mixtures of both', () => {
  assert.strictEqual(evalExpr('SUM(1,2,3)'), '6');
  assert.strictEqual(evalExpr('SUM(A1:A4)', NUMBERS), '24');
  assert.strictEqual(evalExpr('SUM(A1:A4, 6, A1)', NUMBERS), '34');
});

test('SUM skips empty cells and text inside a range', () => {
  assert.strictEqual(evalExpr('SUM(A1:A4)', MIXED), '12');
  assert.strictEqual(evalExpr('SUM(A1:A4)', {}), '0');
});

test('AVERAGE ignores blanks and errors on an all-empty range', () => {
  assert.strictEqual(evalExpr('AVERAGE(A1:A4)', NUMBERS), '6');
  assert.strictEqual(evalExpr('AVERAGE(A1:A4)', { A1: 1, A3: 3 }), '2');
  assert.strictEqual(evalExpr('AVERAGE(A1:A4)', {}), '#DIV/0!');
});

test('MIN and MAX work over ranges, scalars and empty input', () => {
  assert.strictEqual(evalExpr('MIN(A1:A4)', NUMBERS), '2');
  assert.strictEqual(evalExpr('MAX(A1:A4)', NUMBERS), '10');
  assert.strictEqual(evalExpr('MIN(5,-2,3)'), '-2');
  assert.strictEqual(evalExpr('MAX(A1:A4)', {}), '0');
  assert.strictEqual(evalExpr('MIN(A1:A4)', {}), '0');
});

test('COUNT counts only numbers while COUNTA counts anything present', () => {
  assert.strictEqual(evalExpr('COUNT(A1:A4)', MIXED), '2');
  assert.strictEqual(evalExpr('COUNTA(A1:A4)', MIXED), '3');
  assert.strictEqual(evalExpr('COUNT(A1:A4)', {}), '0');
  assert.strictEqual(evalExpr('COUNT(A1:A4, 7)', MIXED), '3');
});

test('IF selects a branch and evaluates the taken branch lazily', () => {
  assert.strictEqual(evalExpr('IF(TRUE,"yes","no")'), 'yes');
  assert.strictEqual(evalExpr('IF(FALSE,"yes","no")'), 'no');
  assert.strictEqual(evalExpr('IF(A1>5,"big","small")', { A1: 9 }), 'big');
  assert.strictEqual(evalExpr('IF(TRUE,1,1/0)'), '1');
  assert.strictEqual(evalExpr('IF(FALSE,1/0,2)'), '2');
  assert.strictEqual(evalExpr('IF(FALSE,1)'), 'FALSE');
});

test('IF propagates an error in its condition', () => {
  assert.strictEqual(evalExpr('IF(1/0,"yes","no")'), '#DIV/0!');
  assert.strictEqual(evalExpr('IF("text","yes","no")'), '#VALUE!');
});

test('AND, OR and NOT combine truthiness and range contents', () => {
  assert.strictEqual(evalExpr('AND(TRUE,TRUE)'), 'TRUE');
  assert.strictEqual(evalExpr('AND(TRUE,FALSE)'), 'FALSE');
  assert.strictEqual(evalExpr('OR(FALSE,FALSE,TRUE)'), 'TRUE');
  assert.strictEqual(evalExpr('OR(FALSE,FALSE)'), 'FALSE');
  assert.strictEqual(evalExpr('NOT(FALSE)'), 'TRUE');
  assert.strictEqual(evalExpr('AND(A1:A4)', { A1: 1, A2: 1 }), 'TRUE');
  assert.strictEqual(evalExpr('AND(A1:A4)', { A1: 1, A2: 0 }), 'FALSE');
  assert.strictEqual(evalExpr('AND(A1>0, A2>0)', NUMBERS), 'TRUE');
});

test('ABS, ROUND, SQRT and LEN handle scalars', () => {
  assert.strictEqual(evalExpr('ABS(-7.5)'), '7.5');
  assert.strictEqual(evalExpr('ABS(A1)', { A1: -3 }), '3');
  assert.strictEqual(evalExpr('ROUND(3.14159,2)'), '3.14');
  assert.strictEqual(evalExpr('ROUND(2.5)'), '3');
  assert.strictEqual(evalExpr('ROUND(-2.5)'), '-3');
  assert.strictEqual(evalExpr('ROUND(1.005,2)'), '1.01');
  assert.strictEqual(evalExpr('ROUND(1234.5678,-2)'), '1200');
  assert.strictEqual(evalExpr('SQRT(16)'), '4');
  assert.strictEqual(evalExpr('SQRT(-1)'), '#NUM!');
  assert.strictEqual(evalExpr('LEN("hello")'), '5');
  assert.strictEqual(evalExpr('CONCAT("a",1,TRUE)'), 'a1TRUE');
});

test('an empty cell argument reads as zero, blank text and an ignored range slot', () => {
  assert.strictEqual(evalExpr('SUM(A1,B9)', { A1: 5 }), '5');
  assert.strictEqual(evalExpr('ABS(B9)'), '0');
  assert.strictEqual(evalExpr('LEN(B9)'), '0');
  assert.strictEqual(evalExpr('COUNT(B9)'), '0');
  assert.strictEqual(evalExpr('AVERAGE(A1,B9)', { A1: 5 }), '5');
});

test('an unknown function is a #NAME? error', () => {
  assert.strictEqual(evalExpr('TOTAL(1,2)'), '#NAME?');
  assert.strictEqual(evalExpr('SUM(1,NOPE(2))'), '#NAME?');
});

test('wrong argument counts produce a #VALUE! error', () => {
  assert.strictEqual(evalExpr('ABS(1,2)'), '#VALUE!');
  assert.strictEqual(evalExpr('IF(TRUE)'), '#VALUE!');
  assert.strictEqual(evalExpr('IF(TRUE,1,2,3)'), '#VALUE!');
});

test('a range handed to a scalar-only function is a #VALUE! error', () => {
  assert.strictEqual(evalExpr('ABS(A1:A4)', NUMBERS), '#VALUE!');
  assert.strictEqual(evalExpr('A1:A4+1', NUMBERS), '#VALUE!');
});

test('errors inside a range propagate out of aggregate functions', () => {
  assert.strictEqual(evalExpr('SUM(A1:A4)', { A1: 1, A2: '=1/0' }), '#DIV/0!');
  assert.strictEqual(evalExpr('COUNT(A1:A4)', { A1: 1, A2: '=1/0' }), '#DIV/0!');
  assert.strictEqual(evalExpr('MAX(A1:A4)', { A1: 1, A2: '=NOPE()' }), '#NAME?');
});

test('the registry is extensible with a single table entry', () => {
  assert.strictEqual(isDefined('MEDIAN'), false);
  define('MEDIAN', {
    minArgs: 1,
    maxArgs: Infinity,
    apply(args) {
      const numbers = [];
      for (const arg of args) {
        if (arg && arg.kind === 'range') {
          for (const value of arg.values) {
            if (typeof value === 'number') numbers.push(value);
          }
        } else if (typeof arg === 'number') {
          numbers.push(arg);
        }
      }
      numbers.sort((a, b) => a - b);
      const middle = Math.floor(numbers.length / 2);
      if (numbers.length % 2 === 1) return numbers[middle];
      return (numbers[middle - 1] + numbers[middle]) / 2;
    },
  });
  assert.strictEqual(isDefined('MEDIAN'), true);
  assert.strictEqual(evalExpr('MEDIAN(A1:A4)', NUMBERS), '6');
  assert.strictEqual(evalExpr('MEDIAN(1,3,100)'), '3');
  assert.ok(functionNames().indexOf('SUM') !== -1);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
