'use strict';

const assert = require('assert');
const { Sheet } = require('./sheet.js');
const { FormulaSyntaxError, isError } = require('./errors.js');

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

function recalculated(sheet) {
  return sheet.lastRecalculatedCells().slice().sort();
}

function diamondSheet() {
  const sheet = new Sheet();
  sheet.setCells({
    A1: 1,
    B1: '=A1+1',
    C1: '=A1*2',
    D1: '=B1+C1',
    F1: 100,
    G1: '=F1+1',
  });
  return sheet;
}

test('stores literal numbers, text, booleans and blanks', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 42, A2: '3.5', A3: 'hello', A4: 'TRUE', A5: '', A6: "'0042" });
  assert.strictEqual(sheet.getValue('A1'), 42);
  assert.strictEqual(sheet.getValue('A2'), 3.5);
  assert.strictEqual(sheet.getValue('A3'), 'hello');
  assert.strictEqual(sheet.getValue('A4'), true);
  assert.strictEqual(sheet.getValue('A5'), null);
  assert.strictEqual(sheet.getValue('A6'), '0042');
  assert.strictEqual(sheet.getValue('Z50'), null);
});

test('getFormula returns the raw input and getValue the computed value', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 6, A2: 7, A3: '=A1*A2' });
  assert.strictEqual(sheet.getFormula('A3'), '=A1*A2');
  assert.strictEqual(sheet.getValue('A3'), 42);
  assert.strictEqual(sheet.getFormula('A1'), '6');
  assert.strictEqual(sheet.getDisplay('A3'), '42');
  assert.strictEqual(sheet.getFormula('Q9'), '');
});

test('formulas read multi-letter columns and ranges over them', () => {
  const sheet = new Sheet();
  sheet.setCells({ AA3: 5, AB3: 6, AC3: 7, A1: '=SUM(AA3:AC3)' });
  assert.strictEqual(sheet.getValue('A1'), 18);
  assert.deepStrictEqual(sheet.getPrecedents('A1'), ['AA3', 'AB3', 'AC3']);
});

test('the dependency graph records precedents and dependents both ways', () => {
  const sheet = diamondSheet();
  assert.deepStrictEqual(sheet.getPrecedents('D1'), ['B1', 'C1']);
  assert.deepStrictEqual(sheet.getDependents('A1'), ['B1', 'C1']);
  assert.deepStrictEqual(sheet.getDependents('D1'), []);
  assert.deepStrictEqual(sheet.getPrecedents('A1'), []);
});

test('a range formula registers every cell of the rectangle as a precedent', () => {
  const sheet = new Sheet();
  sheet.setCell('D1', '=SUM(A1:B2)');
  assert.deepStrictEqual(sheet.getPrecedents('D1'), ['A1', 'A2', 'B1', 'B2']);
  assert.deepStrictEqual(sheet.getDependents('B2'), ['D1']);
  sheet.setCell('B2', 9);
  assert.strictEqual(sheet.getValue('D1'), 9);
  assert.deepStrictEqual(recalculated(sheet), ['B2', 'D1']);
});

test('setting a cell recalculates only it and its transitive dependents', () => {
  const sheet = diamondSheet();
  sheet.setCell('A1', 10);
  assert.deepStrictEqual(recalculated(sheet), ['A1', 'B1', 'C1', 'D1']);
  assert.strictEqual(sheet.lastRecalculationCount(), 4);
  assert.strictEqual(sheet.getValue('B1'), 11);
  assert.strictEqual(sheet.getValue('C1'), 20);
  assert.strictEqual(sheet.getValue('D1'), 31);
  assert.strictEqual(sheet.getValue('G1'), 101);
});

test('an edit to an unrelated branch never touches the other branch', () => {
  const sheet = diamondSheet();
  sheet.setCell('F1', 200);
  assert.deepStrictEqual(recalculated(sheet), ['F1', 'G1']);
  assert.strictEqual(sheet.getValue('G1'), 201);
});

test('a leaf cell that nothing depends on recalculates alone', () => {
  const sheet = diamondSheet();
  sheet.setCell('D1', '=B1');
  assert.deepStrictEqual(recalculated(sheet), ['D1']);
  assert.strictEqual(sheet.lastRecalculationCount(), 1);
});

test('recalculation visits cells in topological order on a diamond', () => {
  const sheet = diamondSheet();
  sheet.setCell('A1', 3);
  const order = sheet.lastRecalculatedCells();
  const at = (key) => order.indexOf(key);
  assert.strictEqual(at('A1'), 0);
  assert.ok(at('B1') > at('A1'));
  assert.ok(at('C1') > at('A1'));
  assert.ok(at('D1') > at('B1'));
  assert.ok(at('D1') > at('C1'));
  assert.strictEqual(at('D1'), order.length - 1);
});

test('a long chain recalculates strictly downstream and in order', () => {
  const sheet = new Sheet();
  sheet.setCell('A1', 1);
  for (let row = 2; row <= 12; row++) {
    sheet.setCell(`A${row}`, `=A${row - 1}+1`);
  }
  sheet.setCell('A6', 100);
  const order = sheet.lastRecalculatedCells();
  assert.deepStrictEqual(order, ['A6', 'A7', 'A8', 'A9', 'A10', 'A11', 'A12']);
  assert.strictEqual(sheet.getValue('A12'), 106);
  assert.strictEqual(sheet.getValue('A5'), 5);
});

test('editing a formula to drop a reference unregisters it from the graph', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 1, B1: 2, C1: '=A1+B1' });
  assert.deepStrictEqual(sheet.getDependents('A1'), ['C1']);
  assert.deepStrictEqual(sheet.getPrecedents('C1'), ['A1', 'B1']);

  sheet.setCell('C1', '=B1*10');
  assert.deepStrictEqual(sheet.getDependents('A1'), []);
  assert.deepStrictEqual(sheet.getPrecedents('C1'), ['B1']);
  assert.strictEqual(sheet.getValue('C1'), 20);

  sheet.setCell('A1', 999);
  assert.deepStrictEqual(recalculated(sheet), ['A1']);
  assert.strictEqual(sheet.getValue('C1'), 20);

  sheet.setCell('B1', 5);
  assert.deepStrictEqual(recalculated(sheet), ['B1', 'C1']);
  assert.strictEqual(sheet.getValue('C1'), 50);
});

test('replacing a formula with a literal detaches it from all precedents', () => {
  const sheet = diamondSheet();
  sheet.setCell('D1', 7);
  assert.deepStrictEqual(sheet.getPrecedents('D1'), []);
  assert.deepStrictEqual(sheet.getDependents('B1'), []);
  sheet.setCell('A1', 4);
  assert.deepStrictEqual(recalculated(sheet), ['A1', 'B1', 'C1']);
  assert.strictEqual(sheet.getValue('D1'), 7);
});

test('shrinking a range narrows the recalculation set', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 1, A2: 2, A3: 3, A4: 4, C1: '=SUM(A1:A4)' });
  assert.strictEqual(sheet.getValue('C1'), 10);
  sheet.setCell('C1', '=SUM(A1:A2)');
  assert.deepStrictEqual(sheet.getPrecedents('C1'), ['A1', 'A2']);
  sheet.setCell('A4', 40);
  assert.deepStrictEqual(recalculated(sheet), ['A4']);
  assert.strictEqual(sheet.getValue('C1'), 3);
});

test('a direct self reference is reported as #CIRC! and not a stack overflow', () => {
  const sheet = new Sheet();
  sheet.setCell('A1', '=A1+1');
  assert.strictEqual(sheet.getDisplay('A1'), '#CIRC!');
  sheet.setCell('B1', 5);
  assert.strictEqual(sheet.getValue('B1'), 5);
});

test('an indirect cycle marks every cell on the cycle', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: '=B1+1', B1: '=C1+1', C1: '=A1+1' });
  assert.strictEqual(sheet.getDisplay('A1'), '#CIRC!');
  assert.strictEqual(sheet.getDisplay('B1'), '#CIRC!');
  assert.strictEqual(sheet.getDisplay('C1'), '#CIRC!');
});

test('breaking a cycle restores correct values everywhere', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: '=B1+1', B1: '=C1+1', C1: '=A1+1' });
  sheet.setCell('C1', 10);
  assert.strictEqual(sheet.getValue('B1'), 11);
  assert.strictEqual(sheet.getValue('A1'), 12);
  assert.strictEqual(sheet.getDisplay('A1'), '12');
});

test('a cycle introduced by editing a healthy sheet is caught and recovered from', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 2, B1: '=A1*3', C1: '=B1+4', D1: '=C1*2' });
  assert.strictEqual(sheet.getValue('D1'), 20);

  sheet.setCell('A1', '=D1');
  assert.strictEqual(sheet.getDisplay('A1'), '#CIRC!');
  assert.strictEqual(sheet.getDisplay('B1'), '#CIRC!');
  assert.strictEqual(sheet.getDisplay('C1'), '#CIRC!');
  assert.strictEqual(sheet.getDisplay('D1'), '#CIRC!');

  sheet.setCell('A1', 5);
  assert.strictEqual(sheet.getValue('B1'), 15);
  assert.strictEqual(sheet.getValue('C1'), 19);
  assert.strictEqual(sheet.getValue('D1'), 38);
});

test('cells downstream of a cycle are marked while independent cells stay valid', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: '=B1', B1: '=A1', C1: '=A1+1', E1: 7, F1: '=E1*2' });
  assert.strictEqual(sheet.getDisplay('A1'), '#CIRC!');
  assert.strictEqual(sheet.getDisplay('C1'), '#CIRC!');
  assert.strictEqual(sheet.getValue('F1'), 14);
  sheet.setCell('B1', 3);
  assert.strictEqual(sheet.getValue('A1'), 3);
  assert.strictEqual(sheet.getValue('C1'), 4);
});

test('a self reference inside a range is caught as a cycle', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 1, A2: 2, A3: '=SUM(A1:A3)' });
  assert.strictEqual(sheet.getDisplay('A3'), '#CIRC!');
  sheet.setCell('A3', '=SUM(A1:A2)');
  assert.strictEqual(sheet.getValue('A3'), 3);
});

test('#DIV/0! is produced and propagates into dependent formulas', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 10, A2: 0, B1: '=A1/A2', C1: '=B1+1', D1: '=C1&"!"' });
  assert.strictEqual(sheet.getDisplay('B1'), '#DIV/0!');
  assert.strictEqual(sheet.getDisplay('C1'), '#DIV/0!');
  assert.strictEqual(sheet.getDisplay('D1'), '#DIV/0!');
  sheet.setCell('A2', 5);
  assert.strictEqual(sheet.getValue('B1'), 2);
  assert.strictEqual(sheet.getValue('C1'), 3);
  assert.strictEqual(sheet.getValue('D1'), '3!');
});

test('#VALUE! is produced by arithmetic on text and propagates', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 'twelve', B1: '=A1*2', C1: '=SUM(B1,1)' });
  assert.strictEqual(sheet.getDisplay('B1'), '#VALUE!');
  assert.strictEqual(sheet.getDisplay('C1'), '#VALUE!');
  assert.ok(isError(sheet.getValue('B1')));
  assert.strictEqual(sheet.getValue('B1').code, '#VALUE!');
});

test('#NAME? is produced by an unknown function and by a bare name', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: '=NOSUCH(1)', B1: '=A1+1', C1: '=HELLO' });
  assert.strictEqual(sheet.getDisplay('A1'), '#NAME?');
  assert.strictEqual(sheet.getDisplay('B1'), '#NAME?');
  assert.strictEqual(sheet.getDisplay('C1'), '#NAME?');
});

test('#REF! is produced by a reference outside the sheet bounds', () => {
  const sheet = new Sheet({ maxRow: 10, maxCol: 5 });
  sheet.setCells({ A1: '=A11', A2: '=F1', A3: '=A1+1' });
  assert.strictEqual(sheet.getDisplay('A1'), '#REF!');
  assert.strictEqual(sheet.getDisplay('A2'), '#REF!');
  assert.strictEqual(sheet.getDisplay('A3'), '#REF!');
  assert.strictEqual(sheet.getDisplay('A11'), '#REF!');
  assert.throws(() => sheet.setCell('A11', 1), RangeError);
});

test('#REF! appears when a referenced row is deleted, and propagates', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 5, A2: '=A1*3', A3: '=A2+1', B1: 'kept', B2: '=B1&"!"' });
  assert.strictEqual(sheet.getValue('A2'), 15);
  sheet.deleteRow(1);
  assert.strictEqual(sheet.getDisplay('A2'), '#REF!');
  assert.strictEqual(sheet.getDisplay('A3'), '#REF!');
  assert.strictEqual(sheet.getDisplay('B2'), '#REF!');
  assert.strictEqual(sheet.getDisplay('A1'), '#REF!');
  assert.deepStrictEqual(recalculated(sheet), ['A2', 'A3', 'B2']);
  assert.throws(() => sheet.setCell('A1', 1), RangeError);
});

test('a #REF! error literal in a formula behaves like the error value', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: '=#REF!', B1: '=A1+1' });
  assert.strictEqual(sheet.getDisplay('A1'), '#REF!');
  assert.strictEqual(sheet.getDisplay('B1'), '#REF!');
});

test('an error only reaches cells that actually read it', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 1, A2: 0, B1: '=A1/A2', C1: '=A1+1' });
  assert.strictEqual(sheet.getDisplay('B1'), '#DIV/0!');
  assert.strictEqual(sheet.getValue('C1'), 2);
});

test('a formula returning a bare range collapses to #VALUE!', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 1, A2: 2, C1: '=A1:A2' });
  assert.strictEqual(sheet.getDisplay('C1'), '#VALUE!');
});

test('invalid references and malformed formulas are rejected without corrupting the sheet', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 3, B1: '=A1*2' });
  assert.throws(() => sheet.setCell('not-a-ref', 1), FormulaSyntaxError);
  assert.throws(() => sheet.setCell('B1', '=A1+'), FormulaSyntaxError);
  assert.strictEqual(sheet.getFormula('B1'), '=A1*2');
  assert.strictEqual(sheet.getValue('B1'), 6);
  sheet.setCell('A1', 4);
  assert.strictEqual(sheet.getValue('B1'), 8);
});

test('usedCells lists only cells with content and getRegion dumps a rectangle', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 'x', B1: 2, A2: '=B1*2', B2: '=1/0' });
  assert.deepStrictEqual(sheet.usedCells(), ['A1', 'A2', 'B1', 'B2']);
  assert.deepStrictEqual(sheet.getRegion('A1', 'C2'), [
    ['x', '2', ''],
    ['4', '#DIV/0!', ''],
  ]);
  const rendered = sheet.renderRegion('A1', 'B2');
  const lines = rendered.split('\n');
  assert.strictEqual(lines.length, 4);
  assert.ok(lines[0].indexOf('A') !== -1 && lines[0].indexOf('B') !== -1);
  assert.ok(lines[3].indexOf('#DIV/0!') !== -1);
});

test('clearCell empties a cell and refreshes everything reading it', () => {
  const sheet = new Sheet();
  sheet.setCells({ A1: 5, B1: '=A1+1' });
  sheet.clearCell('A1');
  assert.strictEqual(sheet.getValue('A1'), null);
  assert.strictEqual(sheet.getValue('B1'), 1);
  assert.deepStrictEqual(recalculated(sheet), ['A1', 'B1']);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
