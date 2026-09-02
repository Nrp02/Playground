'use strict';

const { Sheet } = require('./sheet.js');

function heading(text) {
  console.log('');
  console.log(text);
  console.log('='.repeat(text.length));
}

function buildInvoice() {
  const sheet = new Sheet();
  sheet.setCells({
    A1: 'Item',
    B1: 'Qty',
    C1: 'Unit',
    D1: 'Line Total',
    E1: 'Tier',
    A2: 'Widget',
    B2: 12,
    C2: 4.5,
    D2: '=B2*C2',
    E2: '=IF(D2>=100,"BULK","STD")',
    A3: 'Gadget',
    B3: 3,
    C3: 24,
    D3: '=B3*C3',
    E3: '=IF(D3>=100,"BULK","STD")',
    A4: 'Cable',
    B4: 40,
    C4: 1.25,
    D4: '=B4*C4',
    E4: '=IF(D4>=100,"BULK","STD")',
    A5: 'Adapter',
    B5: 6,
    C5: 8.75,
    D5: '=B5*C5',
    E5: '=IF(D5>=100,"BULK","STD")',
    C6: 'Subtotal',
    D6: '=SUM(D2:D5)',
    C7: 'Tax 8.5%',
    D7: '=ROUND(D6*0.085,2)',
    C8: 'Total',
    D8: '=D6+D7',
    C9: 'Line count',
    D9: '=COUNT(B2:B5)',
    C10: 'Average line',
    D10: '=ROUND(AVERAGE(D2:D5),2)',
    C11: 'Largest line',
    D11: '=MAX(D2:D5)',
  });
  return sheet;
}

const sheet = buildInvoice();

heading('1. An invoice sheet with formulas, a SUM over a range, tax and a grand total');
console.log(sheet.renderRegion('A1', 'E11'));
console.log('');
console.log(`D6 holds the formula   ${sheet.getFormula('D6')}`);
console.log(`D8 holds the formula   ${sheet.getFormula('D8')}`);
console.log(`D6 precedents          ${sheet.getPrecedents('D6').join(', ')}`);
console.log(`D6 dependents          ${sheet.getDependents('D6').join(', ')}`);

heading('2. Incremental recalculation: editing one input touches only its dependents');
console.log(`Cells with content in the sheet: ${sheet.usedCells().length}`);
console.log('Editing B4 (Cable quantity) from 40 to 100 ...');
sheet.setCell('B4', 100);
console.log(`Recalculated ${sheet.lastRecalculationCount()} cells, in dependency order:`);
console.log(`  ${sheet.lastRecalculatedCells().join(' -> ')}`);
console.log(`Untouched cells such as D3 (${sheet.getDisplay('D3')}) were never re-evaluated.`);
console.log('');
console.log(sheet.renderRegion('A1', 'E11'));
console.log('');
console.log(`Subtotal D6 = ${sheet.getDisplay('D6')}`);
console.log(`Tax      D7 = ${sheet.getDisplay('D7')}`);
console.log(`Total    D8 = ${sheet.getDisplay('D8')}`);
console.log(`Cable line E4 flipped to ${sheet.getDisplay('E4')} because D4 crossed 100.`);

heading('3. Error values and how they propagate into dependent formulas');
sheet.setCells({
  G1: 0,
  G2: '=D8/G1',
  G3: '=G2*2',
  G4: 'twelve',
  G5: '=G4*2',
  G6: '=TOTAL(D2:D5)',
  G7: '=G6&" units"',
  A13: 5,
  A14: '=A13*3',
  A15: '=A14+1',
});
console.log(`G2 = ${sheet.getFormula('G2')}  ->  ${sheet.getDisplay('G2')}   (G1 is zero)`);
console.log(`G3 = ${sheet.getFormula('G3')}  ->  ${sheet.getDisplay('G3')}   (error propagates)`);
console.log(`G5 = ${sheet.getFormula('G5')}  ->  ${sheet.getDisplay('G5')}   (G4 holds text)`);
console.log(`G6 = ${sheet.getFormula('G6')}  ->  ${sheet.getDisplay('G6')}   (no such function)`);
console.log(`G7 = ${sheet.getFormula('G7')}  ->  ${sheet.getDisplay('G7')}   (propagates through &)`);
console.log('');
console.log(`Before deleting row 13:  A14 = ${sheet.getDisplay('A14')}, A15 = ${sheet.getDisplay('A15')}`);
sheet.deleteRow(13);
console.log('sheet.deleteRow(13) removes the cell A14 was reading ...');
console.log(`After deleting row 13:   A14 = ${sheet.getDisplay('A14')}, A15 = ${sheet.getDisplay('A15')}`);
console.log(`Recalculated ${sheet.lastRecalculationCount()} cells: ${sheet.lastRecalculatedCells().join(', ')}`);

heading('4. Circular references are detected, not fatal');
console.log('Introducing a direct cycle with I1 = "=I1+1" ...');
sheet.setCell('I1', '=I1+1');
console.log(`  I1 = ${sheet.getDisplay('I1')}`);
console.log('');
console.log('Introducing an indirect cycle: rewriting the subtotal as D6 = "=D8+1"');
console.log('  (D8 depends on D6 and D7, D7 depends on D6 -> D6 depends on itself)');
sheet.setCell('D6', '=D8+1');
for (const ref of ['D6', 'D7', 'D8']) {
  console.log(`  ${ref} = ${sheet.getFormula(ref).padEnd(18)} -> ${sheet.getDisplay(ref)}`);
}
console.log('');
console.log('The rest of the sheet is still readable and still correct:');
console.log(`  D2 = ${sheet.getDisplay('D2')}   D3 = ${sheet.getDisplay('D3')}   D11 = ${sheet.getDisplay('D11')}`);

heading('5. Breaking the cycle restores the sheet');
console.log('Restoring D6 = "=SUM(D2:D5)" ...');
sheet.setCell('D6', '=SUM(D2:D5)');
console.log(`Recalculated ${sheet.lastRecalculationCount()} cells: ${sheet.lastRecalculatedCells().join(' -> ')}`);
for (const ref of ['D6', 'D7', 'D8']) {
  console.log(`  ${ref} = ${sheet.getFormula(ref).padEnd(18)} -> ${sheet.getDisplay(ref)}`);
}
console.log('');
console.log(sheet.renderRegion('C6', 'D11'));
console.log('');
console.log('Editing an input still recalculates only the affected cells:');
sheet.setCell('B2', 20);
console.log(`  ${sheet.lastRecalculationCount()} cells: ${sheet.lastRecalculatedCells().join(' -> ')}`);
console.log(`  new total D8 = ${sheet.getDisplay('D8')}`);
console.log('');
