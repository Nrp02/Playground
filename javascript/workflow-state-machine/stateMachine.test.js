'use strict';

const assert = require('assert');
const { StateMachine, InvalidTransitionError } = require('./stateMachine.js');

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

function buildBasicConfig() {
  return {
    initial: 'pending',
    states: {
      pending: {
        on: {
          approve: 'approved',
          reject: 'rejected',
        },
      },
      approved: {
        on: {
          complete: 'completed',
        },
      },
      rejected: {
        on: {},
      },
      completed: {
        on: {},
      },
    },
  };
}

test('constructor requires a known initial state', () => {
  assert.throws(() => new StateMachine({ initial: 'nope', states: { a: { on: {} } } }), RangeError);
});

test('starts in the configured initial state', () => {
  const machine = new StateMachine(buildBasicConfig());
  assert.strictEqual(machine.current, 'pending');
  assert.deepStrictEqual(machine.history, ['pending']);
});

test('valid transition moves to the target state and records history', () => {
  const machine = new StateMachine(buildBasicConfig());
  machine.send('approve');
  assert.strictEqual(machine.current, 'approved');
  machine.send('complete');
  assert.strictEqual(machine.current, 'completed');
  assert.deepStrictEqual(machine.history, ['pending', 'approved', 'completed']);
});

test('invalid transition (event not defined for current state) is rejected', () => {
  const machine = new StateMachine(buildBasicConfig());
  assert.throws(() => machine.send('complete'), InvalidTransitionError);
  assert.strictEqual(machine.current, 'pending');
});

test('invalid transition leaves history unchanged', () => {
  const machine = new StateMachine(buildBasicConfig());
  assert.throws(() => machine.send('complete'));
  assert.deepStrictEqual(machine.history, ['pending']);
});

test('event with no transition from a terminal state is rejected', () => {
  const machine = new StateMachine(buildBasicConfig());
  machine.send('reject');
  assert.strictEqual(machine.current, 'rejected');
  assert.throws(() => machine.send('approve'), InvalidTransitionError);
});

test('can() reports whether an event is currently valid', () => {
  const machine = new StateMachine(buildBasicConfig());
  assert.strictEqual(machine.can('approve'), true);
  assert.strictEqual(machine.can('complete'), false);
});

test('matches() reports whether the machine is in a given state', () => {
  const machine = new StateMachine(buildBasicConfig());
  assert.strictEqual(machine.matches('pending'), true);
  assert.strictEqual(machine.matches('approved'), false);
});

test('guard blocks a transition until its condition becomes true', () => {
  const machine = new StateMachine({
    initial: 'pending',
    context: { verified: false },
    states: {
      pending: {
        on: {
          approve: {
            target: 'approved',
            guard: (ctx) => ctx.verified === true,
          },
        },
      },
      approved: { on: {} },
    },
  });

  assert.strictEqual(machine.can('approve'), false);
  assert.throws(() => machine.send('approve'), InvalidTransitionError);
  assert.strictEqual(machine.current, 'pending');

  machine.context.verified = true;
  assert.strictEqual(machine.can('approve'), true);
  machine.send('approve');
  assert.strictEqual(machine.current, 'approved');
});

test('a rejected guarded transition does not fire onExit/onEnter hooks', () => {
  const calls = [];
  const machine = new StateMachine({
    initial: 'pending',
    context: { verified: false },
    states: {
      pending: {
        onExit: () => calls.push('exit-pending'),
        on: {
          approve: {
            target: 'approved',
            guard: (ctx) => ctx.verified === true,
          },
        },
      },
      approved: {
        onEnter: () => calls.push('enter-approved'),
        on: {},
      },
    },
  });

  assert.throws(() => machine.send('approve'));
  assert.deepStrictEqual(calls, []);
});

test('onEnter and onExit hooks fire in the correct order on a valid transition', () => {
  const calls = [];
  const machine = new StateMachine({
    initial: 'pending',
    states: {
      pending: {
        onEnter: () => calls.push('enter-pending'),
        onExit: () => calls.push('exit-pending'),
        on: { approve: 'approved' },
      },
      approved: {
        onEnter: () => calls.push('enter-approved'),
        onExit: () => calls.push('exit-approved'),
        on: { complete: 'completed' },
      },
      completed: {
        onEnter: () => calls.push('enter-completed'),
        on: {},
      },
    },
  });

  calls.length = 0;
  machine.send('approve');
  assert.deepStrictEqual(calls, ['exit-pending', 'enter-approved']);

  calls.length = 0;
  machine.send('complete');
  assert.deepStrictEqual(calls, ['exit-approved', 'enter-completed']);
});

test('hooks receive shared context plus transition metadata', () => {
  const seen = [];
  const machine = new StateMachine({
    initial: 'pending',
    context: { orderId: 'X1' },
    states: {
      pending: {
        onExit: (ctx, meta) => seen.push({ ctx: { ...ctx }, meta }),
        on: { approve: 'approved' },
      },
      approved: {
        onEnter: (ctx, meta) => seen.push({ ctx: { ...ctx }, meta }),
        on: {},
      },
    },
  });

  machine.send('approve');
  assert.strictEqual(seen.length, 2);
  assert.strictEqual(seen[0].ctx.orderId, 'X1');
  assert.deepStrictEqual(seen[0].meta, { from: 'pending', to: 'approved', event: 'approve' });
  assert.deepStrictEqual(seen[1].meta, { from: 'pending', to: 'approved', event: 'approve' });
});

test('mutating context in a hook is visible to later guards', () => {
  const machine = new StateMachine({
    initial: 'pending',
    context: { attempts: 0 },
    states: {
      pending: {
        onExit: (ctx) => {
          ctx.attempts += 1;
        },
        on: {
          retry: {
            target: 'pending',
            guard: (ctx) => ctx.attempts < 2,
          },
          give_up: 'failed',
        },
      },
      failed: { on: {} },
    },
  });

  machine.send('retry');
  assert.strictEqual(machine.context.attempts, 1);
  machine.send('retry');
  assert.strictEqual(machine.context.attempts, 2);
  assert.throws(() => machine.send('retry'), InvalidTransitionError);
  machine.send('give_up');
  assert.strictEqual(machine.current, 'failed');
});

test('rejects a transition target that is not a known state', () => {
  const machine = new StateMachine({
    initial: 'pending',
    states: {
      pending: { on: { approve: 'nowhere' } },
    },
  });
  assert.throws(() => machine.send('approve'), RangeError);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
