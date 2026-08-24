'use strict';

/**
 * createStore — a minimal, dependency-free, Redux-inspired pub-sub store.
 *
 * The store holds a single state tree. State can only change by dispatching
 * a plain action object through a pure reducer function; listeners
 * registered with subscribe() are notified after every dispatch.
 *
 * @param {(state: any, action: {type: string, [key: string]: any}) => any} reducer
 *   Pure function that computes the next state from the current state and a
 *   dispatched action. Should return unchanged state for actions it does not
 *   recognize (a `default: return state` branch), and may use a default
 *   parameter (`state = initialState`) to supply its own initial state.
 * @param {any} [preloadedState] Optional initial state. If omitted, the
 *   reducer is invoked once with `undefined` so it can supply its own
 *   default, exactly as it would for any other unrecognized state value.
 * @returns {{getState: () => any, dispatch: (action: object) => object, subscribe: (listener: Function) => Function}}
 */
function createStore(reducer, preloadedState) {
  if (typeof reducer !== 'function') {
    throw new TypeError('createStore expects a reducer function as its first argument.');
  }

  let state = preloadedState;
  let listeners = [];
  let nextListenerId = 0;
  let isDispatching = false;

  function getState() {
    return state;
  }

  function isPlainAction(action) {
    return (
      typeof action === 'object' &&
      action !== null &&
      !Array.isArray(action) &&
      typeof action.type === 'string' &&
      action.type.length > 0
    );
  }

  function dispatch(action) {
    if (isDispatching) {
      throw new Error('Reducers may not dispatch actions. Dispatch is already in progress.');
    }
    if (!isPlainAction(action)) {
      throw new TypeError('Actions must be plain objects with a non-empty string "type" property.');
    }

    try {
      isDispatching = true;
      state = reducer(state, action);
    } finally {
      isDispatching = false;
    }

    // Snapshot the listener list before iterating. If a listener subscribes
    // or unsubscribes while we're notifying, that change applies to the
    // *next* dispatch, not the one currently in flight.
    const currentListeners = listeners.slice();
    for (const entry of currentListeners) {
      entry.listener(state, action);
    }

    return action;
  }

  function subscribe(listener) {
    if (typeof listener !== 'function') {
      throw new TypeError('subscribe expects a function as its argument.');
    }

    const id = nextListenerId++;
    listeners.push({ id, listener });
    let isSubscribed = true;

    return function unsubscribe() {
      if (!isSubscribed) return;
      isSubscribed = false;
      listeners = listeners.filter((entry) => entry.id !== id);
    };
  }

  // Seed initial state by running the reducer with a private init action,
  // the same trick Redux uses so reducers can own their default state via
  // a `state = initialValue` default parameter.
  state = reducer(state, { type: '@@store/INIT' });

  return { getState, dispatch, subscribe };
}

// Usable both as a browser global (<script src="store.js">) and as a
// CommonJS module (`require('./store.js')` from store.test.js under Node) —
// no bundler or build step needed either way.
if (typeof module !== 'undefined' && module.exports) {
  module.exports = { createStore };
}
if (typeof window !== 'undefined') {
  window.createStore = createStore;
}
