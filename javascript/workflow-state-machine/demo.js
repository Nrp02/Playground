'use strict';

const { StateMachine, InvalidTransitionError } = require('./stateMachine.js');

function log(...args) {
  console.log(...args);
}

const orderWorkflow = {
  initial: 'pending',
  context: {
    orderId: 'ORD-1042',
    paymentVerified: false,
  },
  states: {
    pending: {
      onEnter: (ctx) => log(`[enter] pending (order ${ctx.orderId})`),
      onExit: (ctx) => log(`[exit]  pending`),
      on: {
        approve: {
          target: 'approved',
          guard: (ctx) => ctx.paymentVerified === true,
        },
        reject: 'rejected',
      },
    },
    approved: {
      onEnter: (ctx) => log(`[enter] approved`),
      onExit: (ctx) => log(`[exit]  approved`),
      on: {
        ship: 'shipped',
        cancel: 'cancelled',
      },
    },
    rejected: {
      onEnter: (ctx) => log(`[enter] rejected (terminal)`),
      on: {},
    },
    shipped: {
      onEnter: (ctx) => log(`[enter] shipped`),
      onExit: (ctx) => log(`[exit]  shipped`),
      on: {
        deliver: 'completed',
        returnToSender: 'cancelled',
      },
    },
    cancelled: {
      onEnter: (ctx) => log(`[enter] cancelled (terminal)`),
      on: {},
    },
    completed: {
      onEnter: (ctx) => log(`[enter] completed (terminal)`),
      on: {},
    },
  },
};

const machine = new StateMachine(orderWorkflow);

log(`starting state: ${machine.current}`);

log('\n--- attempting to ship before approval (should be rejected) ---');
try {
  machine.send('ship');
} catch (err) {
  if (err instanceof InvalidTransitionError) {
    log(`rejected as expected: ${err.message}`);
  } else {
    throw err;
  }
}

log('\n--- attempting to approve before payment verification (guard should block) ---');
try {
  machine.send('approve');
} catch (err) {
  if (err instanceof InvalidTransitionError) {
    log(`rejected as expected: ${err.message}`);
  } else {
    throw err;
  }
}

log('\n--- verifying payment, then approving ---');
machine.context.paymentVerified = true;
machine.send('approve');
log(`current state: ${machine.current}`);

log('\n--- shipping the order ---');
machine.send('ship');
log(`current state: ${machine.current}`);

log('\n--- delivering the order ---');
machine.send('deliver');
log(`current state: ${machine.current}`);

log('\n--- attempting an event with no transition from a terminal state ---');
try {
  machine.send('cancel');
} catch (err) {
  if (err instanceof InvalidTransitionError) {
    log(`rejected as expected: ${err.message}`);
  } else {
    throw err;
  }
}

log(`\nfull history: ${machine.history.join(' -> ')}`);
log(`is completed: ${machine.matches('completed')}`);
