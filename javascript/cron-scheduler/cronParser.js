'use strict';

const FIELD_BOUNDS = [
  ['minute', 0, 59],
  ['hour', 0, 23],
  ['dayOfMonth', 1, 31],
  ['month', 1, 12],
  ['dayOfWeek', 0, 6],
];

function parsePart(part, min, max) {
  const [rangePart, stepPart] = part.split('/');
  let step = 1;
  if (stepPart !== undefined) {
    step = Number(stepPart);
    if (!Number.isInteger(step) || step <= 0) {
      throw new SyntaxError(`invalid step value: ${part}`);
    }
  }

  let start;
  let end;
  if (rangePart === '*') {
    start = min;
    end = max;
  } else if (rangePart.includes('-')) {
    const bounds = rangePart.split('-');
    if (bounds.length !== 2) {
      throw new SyntaxError(`invalid range: ${part}`);
    }
    start = Number(bounds[0]);
    end = Number(bounds[1]);
  } else {
    start = Number(rangePart);
    end = stepPart !== undefined ? max : start;
  }

  if (!Number.isInteger(start) || !Number.isInteger(end)) {
    throw new SyntaxError(`invalid numeric value: ${part}`);
  }
  if (start < min || end > max || start > end) {
    throw new RangeError(`value out of bounds [${min}, ${max}]: ${part}`);
  }

  const values = [];
  for (let v = start; v <= end; v += step) {
    values.push(v);
  }
  return values;
}

function parseField(fieldText, min, max) {
  const values = new Set();
  for (const part of fieldText.split(',')) {
    if (part.length === 0) {
      throw new SyntaxError(`empty field segment in: ${fieldText}`);
    }
    for (const value of parsePart(part, min, max)) {
      values.add(value);
    }
  }
  return { values, isWildcard: fieldText === '*' };
}

class CronExpression {
  constructor(fields) {
    this.minute = fields.minute;
    this.hour = fields.hour;
    this.dayOfMonth = fields.dayOfMonth;
    this.month = fields.month;
    this.dayOfWeek = fields.dayOfWeek;
  }

  matches(date) {
    if (!this.minute.values.has(date.getMinutes())) return false;
    if (!this.hour.values.has(date.getHours())) return false;
    if (!this.month.values.has(date.getMonth() + 1)) return false;

    const domHit = this.dayOfMonth.values.has(date.getDate());
    const dowHit = this.dayOfWeek.values.has(date.getDay());

    if (this.dayOfMonth.isWildcard && this.dayOfWeek.isWildcard) return true;
    if (this.dayOfMonth.isWildcard) return dowHit;
    if (this.dayOfWeek.isWildcard) return domHit;
    return domHit || dowHit;
  }
}

function parseCron(expression) {
  if (typeof expression !== 'string') {
    throw new TypeError('cron expression must be a string');
  }
  const parts = expression.trim().split(/\s+/);
  if (parts.length !== 5) {
    throw new SyntaxError(`expected 5 fields, got ${parts.length}: ${expression}`);
  }

  const fields = {};
  parts.forEach((part, index) => {
    const [name, min, max] = FIELD_BOUNDS[index];
    fields[name] = parseField(part, min, max);
  });

  return new CronExpression(fields);
}

module.exports = { parseCron, CronExpression };
