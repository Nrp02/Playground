'use strict';

const assert = require('assert');
const { parseCron } = require('./cronParser.js');

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

test('wildcard expression matches every minute', () => {
  const cron = parseCron('* * * * *');
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 13, 27)), true);
  assert.strictEqual(cron.matches(new Date(2024, 5, 15, 0, 0)), true);
});

test('exact minute and hour must both match', () => {
  const cron = parseCron('30 9 * * *');
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 9, 30)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 9, 31)), false);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 10, 30)), false);
});

test('list field matches any listed value', () => {
  const cron = parseCron('1,15,30 * * * *');
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 5, 1)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 5, 15)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 5, 30)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 5, 2)), false);
});

test('range field matches values within the range only', () => {
  const cron = parseCron('* 1-5 * * *');
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 1, 0)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 5, 0)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 6, 0)), false);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 0, 0)), false);
});

test('wildcard step matches every Nth value starting at the field minimum', () => {
  const cron = parseCron('*/15 * * * *');
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 0, 0)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 0, 15)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 0, 30)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 0, 45)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 0, 20)), false);
});

test('range step matches every Nth value within a bounded range', () => {
  const cron = parseCron('1-30/5 * * * *');
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 0, 1)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 0, 6)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 0, 26)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 0, 31)), false);
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 0, 5)), false);
});

test('day-of-week field restricts to weekdays', () => {
  const cron = parseCron('0 9 * * 1-5');
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 9, 0)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 6, 9, 0)), false);
  assert.strictEqual(cron.matches(new Date(2024, 0, 7, 9, 0)), false);
});

test('month field restricts to listed months', () => {
  const cron = parseCron('0 0 1 1,7 *');
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 0, 0)), true);
  assert.strictEqual(cron.matches(new Date(2024, 6, 1, 0, 0)), true);
  assert.strictEqual(cron.matches(new Date(2024, 3, 1, 0, 0)), false);
});

test('day-of-month and day-of-week combine with OR when both restricted', () => {
  const cron = parseCron('0 0 1 * 1');
  assert.strictEqual(cron.matches(new Date(2024, 0, 1, 0, 0)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 8, 0, 0)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 3, 0, 0)), false);
});

test('day-of-month applies alone when day-of-week is wildcard', () => {
  const cron = parseCron('0 0 15 * *');
  assert.strictEqual(cron.matches(new Date(2024, 2, 15, 0, 0)), true);
  assert.strictEqual(cron.matches(new Date(2024, 2, 16, 0, 0)), false);
});

test('day-of-week applies alone when day-of-month is wildcard', () => {
  const cron = parseCron('0 0 * * 0');
  assert.strictEqual(cron.matches(new Date(2024, 0, 7, 0, 0)), true);
  assert.strictEqual(cron.matches(new Date(2024, 0, 8, 0, 0)), false);
});

test('rejects an expression without exactly five fields', () => {
  assert.throws(() => parseCron('* * * *'), SyntaxError);
  assert.throws(() => parseCron('* * * * * *'), SyntaxError);
});

test('rejects a field value outside its bounds', () => {
  assert.throws(() => parseCron('60 * * * *'), RangeError);
  assert.throws(() => parseCron('* 24 * * *'), RangeError);
  assert.throws(() => parseCron('* * 32 * *'), RangeError);
  assert.throws(() => parseCron('* * * 13 *'), RangeError);
  assert.throws(() => parseCron('* * * * 7'), RangeError);
});

test('rejects a malformed numeric field', () => {
  assert.throws(() => parseCron('abc * * * *'), SyntaxError);
});

test('rejects a step of zero or a reversed range', () => {
  assert.throws(() => parseCron('*/0 * * * *'), SyntaxError);
  assert.throws(() => parseCron('30-10 * * * *'), RangeError);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
