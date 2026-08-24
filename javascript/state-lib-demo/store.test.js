'use strict';

/**
 * Unit tests for store.js — no test framework, just Node's built-in
 * `assert` module. Run with: node store.test.js
 */

const assert = require('assert');
const { createStore } = require('./store.js');

let passCount = 0;
let failCount = 0;

/**
 * Tiny test runner: runs `fn`, catches assertion errors so one failing
 * test doesn't stop the rest of the suite, and logs a result line.
 */
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

// A simple counter reducer used by most tests below.
function counterReducer(state = { count: 0 }, action) {
  switch (action.type) {
    case 'increment':
      return { count: state.count + action.amount };
    case 'decrement':
      return { count: state.count - action.amount };
    case 'reset':
      return { count: 0 };
    default:
      return state;
  }
}

console.log('Running store.js test suite...\n');

// ---------------------------------------------------------------------------
// Construction / initial state
// ---------------------------------------------------------------------------

test('createStore throws if reducer is not a function', () => {
  assert.throws(() => createStore(undefined), TypeError);
  assert.throws(() => createStore(null), TypeError);
  assert.throws(() => createStore({}), TypeError);
});

test('getState() returns the reducer default state when no preloadedState is given', () => {
  const store = createStore(counterReducer);
  assert.deepStrictEqual(store.getState(), { count: 0 });
});

test('getState() returns the given preloadedState instead of the reducer default', () => {
  const store = createStore(counterReducer, { count: 42 });
  assert.deepStrictEqual(store.getState(), { count: 42 });
});

// ---------------------------------------------------------------------------
// dispatch / state updates
// ---------------------------------------------------------------------------

test('dispatch runs the reducer and updates state', () => {
  const store = createStore(counterReducer);
  store.dispatch({ type: 'increment', amount: 5 });
  assert.deepStrictEqual(store.getState(), { count: 5 });
  store.dispatch({ type: 'increment', amount: 2 });
  assert.deepStrictEqual(store.getState(), { count: 7 });
  store.dispatch({ type: 'decrement', amount: 3 });
  assert.deepStrictEqual(store.getState(), { count: 4 });
});

test('dispatch returns the action it was given', () => {
  const store = createStore(counterReducer);
  const action = { type: 'increment', amount: 1 };
  const result = store.dispatch(action);
  assert.strictEqual(result, action);
});

test('unrecognized action types leave state referentially unchanged', () => {
  const store = createStore(counterReducer);
  const before = store.getState();
  store.dispatch({ type: 'not/a/real/action' });
  assert.strictEqual(store.getState(), before);
});

test('dispatch rejects actions without a valid "type" property', () => {
  const store = createStore(counterReducer);
  assert.throws(() => store.dispatch({}), TypeError);
  assert.throws(() => store.dispatch({ type: '' }), TypeError);
  assert.throws(() => store.dispatch(null), TypeError);
  assert.throws(() => store.dispatch('increment'), TypeError);
  assert.throws(() => store.dispatch(['increment']), TypeError);
});

test('a reducer that dispatches while already dispatching throws', () => {
  const reenteringReducer = (state = 0, action) => {
    if (action.type === 'trigger') {
      // Reducers must be pure — dispatching from inside one is a misuse
      // the store should catch, mirroring Redux's own guard.
      store.dispatch({ type: 'nested' });
    }
    return state;
  };
  const store = createStore(reenteringReducer);
  assert.throws(() => store.dispatch({ type: 'trigger' }), /Reducers may not dispatch actions/);
});

// ---------------------------------------------------------------------------
// subscribe / unsubscribe
// ---------------------------------------------------------------------------

test('subscribe throws if listener is not a function', () => {
  const store = createStore(counterReducer);
  assert.throws(() => store.subscribe('not a function'), TypeError);
  assert.throws(() => store.subscribe(undefined), TypeError);
});

test('a subscribed listener is called after dispatch with the new state', () => {
  const store = createStore(counterReducer);
  const seen = [];
  store.subscribe((state) => seen.push(state));

  store.dispatch({ type: 'increment', amount: 1 });
  store.dispatch({ type: 'increment', amount: 1 });

  assert.strictEqual(seen.length, 2);
  assert.deepStrictEqual(seen[0], { count: 1 });
  assert.deepStrictEqual(seen[1], { count: 2 });
});

test('listener receives both the new state and the dispatched action', () => {
  const store = createStore(counterReducer);
  let receivedState = null;
  let receivedAction = null;
  store.subscribe((state, action) => {
    receivedState = state;
    receivedAction = action;
  });

  store.dispatch({ type: 'increment', amount: 9 });

  assert.deepStrictEqual(receivedState, { count: 9 });
  assert.strictEqual(receivedAction.type, 'increment');
});

test('multiple listeners are all notified, in subscription order', () => {
  const store = createStore(counterReducer);
  const calls = [];
  store.subscribe(() => calls.push('a'));
  store.subscribe(() => calls.push('b'));
  store.subscribe(() => calls.push('c'));

  store.dispatch({ type: 'increment', amount: 1 });

  assert.deepStrictEqual(calls, ['a', 'b', 'c']);
});

test('unsubscribe stops further notifications to that listener', () => {
  const store = createStore(counterReducer);
  const calls = [];
  const unsubscribe = store.subscribe(() => calls.push('x'));

  store.dispatch({ type: 'increment', amount: 1 });
  unsubscribe();
  store.dispatch({ type: 'increment', amount: 1 });

  assert.strictEqual(calls.length, 1);
});

test('calling unsubscribe twice is a harmless no-op', () => {
  const store = createStore(counterReducer);
  const unsubscribe = store.subscribe(() => {});
  unsubscribe();
  assert.doesNotThrow(() => unsubscribe());
});

test('unsubscribing does not disturb other listeners', () => {
  const store = createStore(counterReducer);
  const calls = [];
  const unsubA = store.subscribe(() => calls.push('a'));
  store.subscribe(() => calls.push('b'));

  unsubA();
  store.dispatch({ type: 'increment', amount: 1 });

  assert.deepStrictEqual(calls, ['b']);
});

test('subscribing the same function twice registers two independent subscriptions', () => {
  const store = createStore(counterReducer);
  let callCount = 0;
  const listener = () => {
    callCount++;
  };
  const unsubscribeFirst = store.subscribe(listener);
  store.subscribe(listener);

  store.dispatch({ type: 'increment', amount: 1 });
  assert.strictEqual(callCount, 2, 'both subscriptions should fire');

  unsubscribeFirst();
  store.dispatch({ type: 'increment', amount: 1 });
  assert.strictEqual(callCount, 3, 'only the remaining subscription should fire');
});

test('a listener that unsubscribes itself mid-dispatch still lets sibling listeners run', () => {
  const store = createStore(counterReducer);
  const calls = [];
  let unsubSelf;
  unsubSelf = store.subscribe(() => {
    calls.push('self');
    unsubSelf();
  });
  store.subscribe(() => calls.push('sibling'));

  store.dispatch({ type: 'increment', amount: 1 });
  assert.deepStrictEqual(calls, ['self', 'sibling'], 'snapshot should include both for this dispatch');

  calls.length = 0;
  store.dispatch({ type: 'increment', amount: 1 });
  assert.deepStrictEqual(calls, ['sibling'], 'self-unsubscribed listener should not fire again');
});

test('a listener that subscribes a new listener mid-dispatch does not run the new one until the next dispatch', () => {
  const store = createStore(counterReducer);
  const calls = [];
  store.subscribe(() => {
    calls.push('first');
    store.subscribe(() => calls.push('late-added'));
  });

  store.dispatch({ type: 'increment', amount: 1 });
  assert.deepStrictEqual(calls, ['first']);

  store.dispatch({ type: 'increment', amount: 1 });
  assert.deepStrictEqual(calls, ['first', 'first', 'late-added']);
});

// ---------------------------------------------------------------------------
// Independence between store instances
// ---------------------------------------------------------------------------

test('two stores created from the same reducer do not share state or listeners', () => {
  const storeA = createStore(counterReducer);
  const storeB = createStore(counterReducer);

  let bNotifications = 0;
  storeB.subscribe(() => bNotifications++);

  storeA.dispatch({ type: 'increment', amount: 10 });

  assert.deepStrictEqual(storeA.getState(), { count: 10 });
  assert.deepStrictEqual(storeB.getState(), { count: 0 });
  assert.strictEqual(bNotifications, 0);
});

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------

console.log(`\n${passCount} passed, ${failCount} failed (${passCount + failCount} total)`);

if (failCount > 0) {
  process.exitCode = 1;
} else {
  console.log('All assertions passed.');
}
