'use strict';

const MAX_MINUTES_SEARCHED = 4 * 366 * 24 * 60;

function nextFireTimes(cronExpression, from, count) {
  if (count <= 0) {
    return [];
  }

  const cursor = new Date(from.getTime());
  cursor.setSeconds(0, 0);
  cursor.setMinutes(cursor.getMinutes() + 1);

  const results = [];
  let minutesSearched = 0;

  while (results.length < count) {
    if (cronExpression.matches(cursor)) {
      results.push(new Date(cursor.getTime()));
    }
    cursor.setMinutes(cursor.getMinutes() + 1);
    minutesSearched += 1;
    if (minutesSearched > MAX_MINUTES_SEARCHED) {
      throw new Error('no matching fire time found within search bound');
    }
  }

  return results;
}

module.exports = { nextFireTimes };
