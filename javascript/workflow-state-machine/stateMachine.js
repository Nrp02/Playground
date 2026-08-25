'use strict';

class InvalidTransitionError extends Error {
  constructor(message) {
    super(message);
    this.name = 'InvalidTransitionError';
  }
}

class StateMachine {
  constructor(config) {
    if (!config || typeof config !== 'object') {
      throw new TypeError('config is required');
    }
    if (!config.initial || typeof config.initial !== 'string') {
      throw new TypeError('config.initial must be a string');
    }
    if (!config.states || typeof config.states !== 'object') {
      throw new TypeError('config.states must be an object');
    }
    if (!Object.prototype.hasOwnProperty.call(config.states, config.initial)) {
      throw new RangeError(`unknown initial state: ${config.initial}`);
    }

    this._states = config.states;
    this._current = config.initial;
    this._history = [config.initial];
    this._context = config.context || {};
  }

  get current() {
    return this._current;
  }

  get history() {
    return this._history.slice();
  }

  get context() {
    return this._context;
  }

  can(event) {
    const stateDef = this._states[this._current];
    if (!stateDef || !stateDef.on || !stateDef.on[event]) {
      return false;
    }
    const transition = stateDef.on[event];
    const target = typeof transition === 'string' ? transition : transition.target;
    if (!Object.prototype.hasOwnProperty.call(this._states, target)) {
      return false;
    }
    if (typeof transition === 'object' && typeof transition.guard === 'function') {
      return Boolean(transition.guard(this._context));
    }
    return true;
  }

  send(event) {
    const stateDef = this._states[this._current];
    const transition = stateDef && stateDef.on && stateDef.on[event];

    if (!transition) {
      throw new InvalidTransitionError(
        `no transition for event "${event}" from state "${this._current}"`
      );
    }

    const target = typeof transition === 'string' ? transition : transition.target;
    if (!Object.prototype.hasOwnProperty.call(this._states, target)) {
      throw new RangeError(`transition target "${target}" is not a known state`);
    }

    const guard = typeof transition === 'object' ? transition.guard : undefined;
    if (typeof guard === 'function' && !guard(this._context)) {
      throw new InvalidTransitionError(
        `guard rejected event "${event}" from state "${this._current}"`
      );
    }

    const from = this._current;
    const fromDef = this._states[from];
    const toDef = this._states[target];

    if (fromDef && typeof fromDef.onExit === 'function') {
      fromDef.onExit(this._context, { from, to: target, event });
    }

    this._current = target;
    this._history.push(target);

    if (toDef && typeof toDef.onEnter === 'function') {
      toDef.onEnter(this._context, { from, to: target, event });
    }

    return this._current;
  }

  matches(state) {
    return this._current === state;
  }
}

module.exports = { StateMachine, InvalidTransitionError };
