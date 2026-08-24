'use strict';

const assert = require('assert');
const { EventStore } = require('./eventStore.js');
const { EventBus } = require('./eventBus.js');
const { CommandBus } = require('./commandHandler.js');

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

function reduceBalance(events) {
  return events.reduce((balance, event) => {
    if (event.type === 'Deposited') return balance + event.payload.amount;
    if (event.type === 'Withdrawn') return balance - event.payload.amount;
    return balance;
  }, 0);
}

function buildCommandBus() {
  const eventStore = new EventStore();
  const eventBus = new EventBus();
  const commandBus = new CommandBus(eventStore, eventBus);

  commandBus.register('Deposit', {
    streamIdFor: (cmd) => cmd.accountId,
    reduce: reduceBalance,
    handle: (cmd) => [{ type: 'Deposited', payload: { amount: cmd.amount } }],
  });

  commandBus.register('Withdraw', {
    streamIdFor: (cmd) => cmd.accountId,
    reduce: reduceBalance,
    handle: (cmd, balance) => {
      if (cmd.amount > balance) {
        return [{ type: 'WithdrawalRejected', payload: { amount: cmd.amount } }];
      }
      return [{ type: 'Withdrawn', payload: { amount: cmd.amount } }];
    },
  });

  return { eventStore, eventBus, commandBus };
}

test('dispatch appends events produced by the handler', () => {
  const { eventStore, commandBus } = buildCommandBus();
  commandBus.dispatch({ type: 'Deposit', accountId: 'a1', amount: 100 });
  assert.deepStrictEqual(eventStore.getStream('a1').map((e) => e.type), ['Deposited']);
});

test('dispatch passes reduced state into the handler for business rules', () => {
  const { commandBus } = buildCommandBus();
  commandBus.dispatch({ type: 'Deposit', accountId: 'a1', amount: 10 });
  const [event] = commandBus.dispatch({ type: 'Withdraw', accountId: 'a1', amount: 100 });
  assert.strictEqual(event.type, 'WithdrawalRejected');
});

test('dispatch publishes appended events onto the event bus', () => {
  const { eventBus, commandBus } = buildCommandBus();
  const received = [];
  eventBus.subscribe('Deposited', (event) => received.push(event));
  commandBus.dispatch({ type: 'Deposit', accountId: 'a1', amount: 5 });
  assert.strictEqual(received.length, 1);
  assert.strictEqual(received[0].payload.amount, 5);
});

test('dispatch throws for an unregistered command type', () => {
  const { commandBus } = buildCommandBus();
  assert.throws(() => commandBus.dispatch({ type: 'Unknown' }), /no handler registered/);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
