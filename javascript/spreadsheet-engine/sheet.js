'use strict';

const { CellError, isError, REF, VALUE, CIRC } = require('./errors.js');
const { parse } = require('./parser.js');
const { evaluate, collectPrecedents } = require('./evaluator.js');
const { isRange, formatValue } = require('./values.js');
const { requireRef, makeKey, indexToColumn, rangeBounds } = require('./refs.js');

const NUMBER_LITERAL = /^[+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?$/;

function parseInput(input) {
  if (typeof input === 'number') return { ast: null, literal: input };
  if (typeof input === 'boolean') return { ast: null, literal: input };
  if (input === null || input === undefined) return { ast: null, literal: null };
  const text = String(input);
  if (text.startsWith('=')) return { ast: parse(text.slice(1)), literal: null };
  if (text.startsWith("'")) return { ast: null, literal: text.slice(1) };
  const trimmed = text.trim();
  if (trimmed === '') return { ast: null, literal: null };
  const upper = trimmed.toUpperCase();
  if (upper === 'TRUE' || upper === 'FALSE') return { ast: null, literal: upper === 'TRUE' };
  if (NUMBER_LITERAL.test(trimmed)) return { ast: null, literal: Number(trimmed) };
  return { ast: null, literal: text };
}

class Sheet {
  constructor(options) {
    const opts = options || {};
    this.maxRow = opts.maxRow || 1048576;
    this.maxCol = opts.maxCol || 16384;
    this.cells = new Map();
    this.deletedRows = new Set();
    this.lastRecalculated = [];
  }

  locate(ref) {
    const parsed = requireRef(ref);
    if (parsed.row < 1 || parsed.row > this.maxRow || parsed.col < 1 || parsed.col > this.maxCol) {
      return { parsed, key: makeKey(parsed.col, parsed.row), outOfBounds: true };
    }
    return { parsed, key: makeKey(parsed.col, parsed.row), outOfBounds: false };
  }

  ensureCell(key) {
    let cell = this.cells.get(key);
    if (cell) return cell;
    const parsed = requireRef(key);
    cell = {
      key,
      col: parsed.col,
      row: parsed.row,
      raw: '',
      ast: null,
      literal: null,
      value: null,
      precedents: new Set(),
      dependents: new Set(),
    };
    this.cells.set(key, cell);
    return cell;
  }

  setCell(ref, input) {
    const located = this.locate(ref);
    if (located.outOfBounds) {
      throw new RangeError(`cell ${ref} is outside the sheet bounds`);
    }
    if (this.deletedRows.has(located.parsed.row)) {
      throw new RangeError(`row ${located.parsed.row} has been deleted`);
    }
    const compiled = parseInput(input);
    const precedents = compiled.ast ? collectPrecedents(compiled.ast, new Set()) : new Set();
    const cell = this.ensureCell(located.key);
    cell.raw = typeof input === 'string' ? input : String(input === null || input === undefined ? '' : input);
    cell.ast = compiled.ast;
    cell.literal = compiled.literal;
    this.rewireEdges(cell, precedents);
    this.recalculate([cell.key]);
    return this;
  }

  setCells(entries) {
    for (const ref of Object.keys(entries)) {
      this.setCell(ref, entries[ref]);
    }
    return this;
  }

  clearCell(ref) {
    return this.setCell(ref, '');
  }

  rewireEdges(cell, precedents) {
    for (const key of cell.precedents) {
      if (!precedents.has(key)) {
        const other = this.cells.get(key);
        if (other) other.dependents.delete(cell.key);
      }
    }
    for (const key of precedents) {
      if (!cell.precedents.has(key)) {
        this.ensureCell(key).dependents.add(cell.key);
      }
    }
    cell.precedents = precedents;
  }

  collectDirty(seeds) {
    const dirty = new Set();
    const stack = seeds.slice();
    while (stack.length > 0) {
      const key = stack.pop();
      if (dirty.has(key)) continue;
      dirty.add(key);
      const cell = this.cells.get(key);
      if (!cell) continue;
      for (const dependent of cell.dependents) {
        if (!dirty.has(dependent)) stack.push(dependent);
      }
    }
    return dirty;
  }

  topologicalOrder(dirty) {
    const indegree = new Map();
    for (const key of dirty) {
      const cell = this.cells.get(key);
      let count = 0;
      if (cell) {
        for (const precedent of cell.precedents) {
          if (dirty.has(precedent)) count++;
        }
      }
      indegree.set(key, count);
    }
    const queue = [];
    for (const [key, count] of indegree) {
      if (count === 0) queue.push(key);
    }
    const order = [];
    const ordered = new Set();
    while (queue.length > 0) {
      const key = queue.shift();
      order.push(key);
      ordered.add(key);
      const cell = this.cells.get(key);
      if (!cell) continue;
      for (const dependent of cell.dependents) {
        if (!indegree.has(dependent)) continue;
        const remaining = indegree.get(dependent) - 1;
        indegree.set(dependent, remaining);
        if (remaining === 0) queue.push(dependent);
      }
    }
    const cyclic = [];
    for (const key of dirty) {
      if (!ordered.has(key)) cyclic.push(key);
    }
    return { order, cyclic };
  }

  recalculate(seeds) {
    const dirty = this.collectDirty(seeds);
    const sorted = this.topologicalOrder(dirty);
    for (const key of sorted.order) {
      this.computeCell(this.cells.get(key));
    }
    for (const key of sorted.cyclic) {
      const cell = this.cells.get(key);
      if (cell) cell.value = new CellError(CIRC);
    }
    this.lastRecalculated = sorted.order.concat(sorted.cyclic);
    return this.lastRecalculated;
  }

  computeCell(cell) {
    if (!cell) return;
    if (!cell.ast) {
      cell.value = cell.literal;
      return;
    }
    const value = evaluate(cell.ast, this);
    cell.value = isRange(value) ? new CellError(VALUE) : value;
  }

  readCell(col, row) {
    if (row < 1 || col < 1 || row > this.maxRow || col > this.maxCol) {
      return new CellError(REF);
    }
    if (this.deletedRows.has(row)) return new CellError(REF);
    const cell = this.cells.get(makeKey(col, row));
    if (!cell) return null;
    return cell.value === undefined ? null : cell.value;
  }

  getValue(ref) {
    const located = this.locate(ref);
    if (located.outOfBounds) return new CellError(REF);
    return this.readCell(located.parsed.col, located.parsed.row);
  }

  getDisplay(ref) {
    return formatValue(this.getValue(ref));
  }

  getFormula(ref) {
    const located = this.locate(ref);
    const cell = this.cells.get(located.key);
    return cell ? cell.raw : '';
  }

  getPrecedents(ref) {
    const cell = this.cells.get(this.locate(ref).key);
    return cell ? Array.from(cell.precedents).sort() : [];
  }

  getDependents(ref) {
    const cell = this.cells.get(this.locate(ref).key);
    return cell ? Array.from(cell.dependents).sort() : [];
  }

  lastRecalculatedCells() {
    return this.lastRecalculated.slice();
  }

  lastRecalculationCount() {
    return this.lastRecalculated.length;
  }

  usedCells() {
    const keys = [];
    for (const cell of this.cells.values()) {
      if (cell.raw !== '') keys.push(cell.key);
    }
    return keys.sort();
  }

  deleteRow(row) {
    if (this.deletedRows.has(row)) return this;
    this.deletedRows.add(row);
    const seeds = [];
    for (const cell of this.cells.values()) {
      if (cell.row !== row) continue;
      for (const dependent of cell.dependents) seeds.push(dependent);
      this.rewireEdges(cell, new Set());
      cell.raw = '';
      cell.ast = null;
      cell.literal = null;
      cell.value = new CellError(REF);
    }
    this.recalculate(seeds);
    return this;
  }

  getRegion(startRef, endRef) {
    const start = requireRef(startRef);
    const end = requireRef(endRef);
    const bounds = rangeBounds(start, end);
    const rows = [];
    for (let row = bounds.minRow; row <= bounds.maxRow; row++) {
      const line = [];
      for (let col = bounds.minCol; col <= bounds.maxCol; col++) {
        line.push(formatValue(this.readCell(col, row)));
      }
      rows.push(line);
    }
    return rows;
  }

  renderRegion(startRef, endRef) {
    const start = requireRef(startRef);
    const end = requireRef(endRef);
    const bounds = rangeBounds(start, end);
    const grid = this.getRegion(startRef, endRef);
    const headers = [];
    for (let col = bounds.minCol; col <= bounds.maxCol; col++) {
      headers.push(indexToColumn(col));
    }
    const widths = headers.map((header, index) => {
      let width = header.length;
      for (const line of grid) {
        if (line[index].length > width) width = line[index].length;
      }
      return width;
    });
    const rowLabelWidth = String(bounds.maxRow).length + 1;
    const lines = [];
    lines.push(
      ' '.repeat(rowLabelWidth) +
        ' | ' +
        headers.map((header, i) => header.padEnd(widths[i])).join(' | ')
    );
    lines.push(
      '-'.repeat(rowLabelWidth) +
        '-+-' +
        widths.map((width) => '-'.repeat(width)).join('-+-')
    );
    grid.forEach((line, index) => {
      const label = String(bounds.minRow + index).padStart(rowLabelWidth);
      lines.push(label + ' | ' + line.map((cellText, i) => cellText.padEnd(widths[i])).join(' | '));
    });
    return lines.join('\n');
  }
}

module.exports = { Sheet, parseInput, isError };
