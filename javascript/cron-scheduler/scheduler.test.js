'use strict';

const assert = require('assert');
const { parseCron } = require('./cronParser.js');
const { nextFireTimes } = require('./scheduler.js');

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

test('every-15-minutes expression fires at the next quarter-hour boundaries', () => {
  const cron = parseCron('*/15 * * * *');
  const now = new Date(2024, 0, 1, 10, 5);
  const fireTimes = nextFireTimes(cron, now, 4);
  const minutes = fireTimes.map((d) => d.getMinutes());
  assert.deepStrictEqual(minutes, [15, 30, 45, 0]);
  assert.strictEqual(fireTimes[3].getHours(), 11);
});

test('fixed daily time fires once per day at the same clock time', () => {
  const cron = parseCron('0 9 * * *');
  const now = new Date(2024, 0, 1, 12, 0);
  const fireTimes = nextFireTimes(cron, now, 3);
  assert.strictEqual(fireTimes[0].getDate(), 2);
  assert.strictEqual(fireTimes[1].getDate(), 3);
  assert.strictEqual(fireTimes[2].getDate(), 4);
  for (const d of fireTimes) {
    assert.strictEqual(d.getHours(), 9);
    assert.strictEqual(d.getMinutes(), 0);
  }
});

test('weekday-only expression skips weekends', () => {
  const cron = parseCron('0 9 * * 1-5');
  const friday = new Date(2024, 0, 5, 10, 0);
  const fireTimes = nextFireTimes(cron, friday, 1);
  assert.strictEqual(fireTimes[0].getDay(), 1);
  assert.strictEqual(fireTimes[0].getDate(), 8);
});

test('monthly expression skips to the first of the next month', () => {
  const cron = parseCron('0 0 1 * *');
  const now = new Date(2024, 1, 15, 8, 30);
  const fireTimes = nextFireTimes(cron, now, 2);
  assert.strictEqual(fireTimes[0].getMonth(), 2);
  assert.strictEqual(fireTimes[0].getDate(), 1);
  assert.strictEqual(fireTimes[1].getMonth(), 3);
  assert.strictEqual(fireTimes[1].getDate(), 1);
});

test('search starts strictly after the reference time, never returning it', () => {
  const cron = parseCron('30 10 * * *');
  const now = new Date(2024, 0, 1, 10, 30);
  const fireTimes = nextFireTimes(cron, now, 1);
  assert.strictEqual(fireTimes[0].getDate(), 2);
});

test('requesting zero fire times returns an empty array', () => {
  const cron = parseCron('* * * * *');
  const fireTimes = nextFireTimes(cron, new Date(2024, 0, 1, 0, 0), 0);
  assert.deepStrictEqual(fireTimes, []);
});

test('returned fire times are strictly increasing', () => {
  const cron = parseCron('1-30/5 * * * *');
  const fireTimes = nextFireTimes(cron, new Date(2024, 0, 1, 0, 0), 10);
  for (let i = 1; i < fireTimes.length; i += 1) {
    assert.ok(fireTimes[i].getTime() > fireTimes[i - 1].getTime());
  }
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
