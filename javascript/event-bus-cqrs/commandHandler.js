'use strict';

class CommandBus {
  constructor(eventStore, eventBus) {
    this._store = eventStore;
    this._bus = eventBus;
    this._handlers = new Map();
  }

  register(commandType, { streamIdFor, reduce, handle }) {
    this._handlers.set(commandType, { streamIdFor, reduce, handle });
  }

  dispatch(command) {
    const registration = this._handlers.get(command.type);
    if (!registration) {
      throw new Error(`no handler registered for command type "${command.type}"`);
    }

    const streamId = registration.streamIdFor(command);
    const existingEvents = this._store.getStream(streamId);
    const state = registration.reduce(existingEvents);
    const eventDrafts = registration.handle(command, state);

    const appended = this._store.append(streamId, eventDrafts, existingEvents.length);
    this._bus.publishAll(appended);
    return appended;
  }
}

module.exports = { CommandBus };
