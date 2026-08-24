'use strict';

const { parseCron } = require('./cronParser.js');
const { nextFireTimes } = require('./scheduler.js');

function formatDate(date) {
  const pad = (n) => String(n).padStart(2, '0');
  const day = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'][date.getDay()];
  return `${day} ${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}`;
}

function main() {
  const now = new Date(2024, 2, 6, 14, 22);
  console.log(`reference "now": ${formatDate(now)}\n`);

  const examples = [
    ['every 15 minutes', '*/15 * * * *'],
    ['9am on weekdays', '0 9 * * 1-5'],
    ['midnight on the 1st of every month', '0 0 1 * *'],
    ['every 5 minutes between :00 and :30, at 6am', '0-30/5 6 * * *'],
    ['noon on Sundays', '0 12 * * 0'],
  ];

  for (const [label, expression] of examples) {
    const cron = parseCron(expression);
    const fireTimes = nextFireTimes(cron, now, 5);
    console.log(`${label}  (${expression})`);
    for (const fireTime of fireTimes) {
      console.log(`  -> ${formatDate(fireTime)}`);
    }
    console.log('');
  }
}

main();
