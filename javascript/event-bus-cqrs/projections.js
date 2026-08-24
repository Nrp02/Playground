'use strict';

const { WILDCARD } = require('./eventBus.js');

function replay(eventStore, streamId, initialState, applyFn) {
  let state = initialState;
  for (const event of eventStore.getStream(streamId)) {
    state = applyFn(state, event);
  }
  return state;
}

class LiveProjection {
  constructor(eventStore, eventBus, streamId, initialState, applyFn) {
    this._streamId = streamId;
    this._apply = applyFn;
    this._state = replay(eventStore, streamId, initialState, applyFn);

    eventBus.subscribe(WILDCARD, (event) => {
      if (event.streamId === this._streamId) {
        this._state = this._apply(this._state, event);
      }
    });
  }

  get state() {
    return this._state;
  }
}

module.exports = { replay, LiveProjection };
