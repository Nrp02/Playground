'use strict';

const { EventStore } = require('./eventStore.js');
const { EventBus } = require('./eventBus.js');
const { CommandBus } = require('./commandHandler.js');
const { replay, LiveProjection } = require('./projections.js');

function reduceAccount(events) {
  let state = { opened: false, balance: 0 };
  for (const event of events) {
    state = applyAccountEvent(state, event);
  }
  return state;
}

function applyAccountEvent(state, event) {
  switch (event.type) {
    case 'AccountOpened':
      return { opened: true, balance: event.payload.openingBalance };
    case 'Deposited':
      return { ...state, balance: state.balance + event.payload.amount };
    case 'Withdrawn':
      return { ...state, balance: state.balance - event.payload.amount };
    case 'WithdrawalRejected':
      return state;
    default:
      return state;
  }
}

function main() {
  const eventStore = new EventStore();
  const eventBus = new EventBus();
  const commandBus = new CommandBus(eventStore, eventBus);

  commandBus.register('OpenAccount', {
    streamIdFor: (cmd) => cmd.accountId,
    reduce: reduceAccount,
    handle: (cmd, state) => {
      if (state.opened) {
        throw new Error(`account ${cmd.accountId} is already open`);
      }
      return [{ type: 'AccountOpened', payload: { openingBalance: cmd.openingBalance } }];
    },
  });

  commandBus.register('Deposit', {
    streamIdFor: (cmd) => cmd.accountId,
    reduce: reduceAccount,
    handle: (cmd) => [{ type: 'Deposited', payload: { amount: cmd.amount } }],
  });

  commandBus.register('Withdraw', {
    streamIdFor: (cmd) => cmd.accountId,
    reduce: reduceAccount,
    handle: (cmd, state) => {
      if (cmd.amount > state.balance) {
        return [{ type: 'WithdrawalRejected', payload: { amount: cmd.amount, reason: 'insufficient funds' } }];
      }
      return [{ type: 'Withdrawn', payload: { amount: cmd.amount } }];
    },
  });

  const activityLog = [];
  eventBus.subscribe('*', (event) => {
    activityLog.push(`${event.streamId} v${event.version}: ${event.type} ${JSON.stringify(event.payload)}`);
  });

  const accountId = 'acct-1';
  const balanceProjection = new LiveProjection(eventStore, eventBus, accountId, { opened: false, balance: 0 }, applyAccountEvent);

  console.log('=== dispatching commands ===');
  commandBus.dispatch({ type: 'OpenAccount', accountId, openingBalance: 100 });
  commandBus.dispatch({ type: 'Deposit', accountId, amount: 50 });
  commandBus.dispatch({ type: 'Withdraw', accountId, amount: 30 });
  commandBus.dispatch({ type: 'Withdraw', accountId, amount: 1000 });
  commandBus.dispatch({ type: 'Deposit', accountId, amount: 20 });

  console.log('\n=== event log (from the event bus) ===');
  for (const line of activityLog) {
    console.log('  ' + line);
  }

  console.log('\n=== live projection (kept up to date incrementally) ===');
  console.log(balanceProjection.state);

  const replayedState = replay(eventStore, accountId, { opened: false, balance: 0 }, applyAccountEvent);
  console.log('\n=== projection rebuilt by replaying the event store from scratch ===');
  console.log(replayedState);
}

main();
