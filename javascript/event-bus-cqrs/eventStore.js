'use strict';

class ConcurrencyError extends Error {
  constructor(streamId, expectedVersion, actualVersion) {
    super(`concurrency conflict on stream "${streamId}": expected version ${expectedVersion}, actual ${actualVersion}`);
    this.name = 'ConcurrencyError';
    this.streamId = streamId;
    this.expectedVersion = expectedVersion;
    this.actualVersion = actualVersion;
  }
}

class EventStore {
  constructor() {
    this._streams = new Map();
  }

  _streamEvents(streamId) {
    let events = this._streams.get(streamId);
    if (!events) {
      events = [];
      this._streams.set(streamId, events);
    }
    return events;
  }

  version(streamId) {
    return this._streamEvents(streamId).length;
  }

  append(streamId, newEvents, expectedVersion) {
    const events = this._streamEvents(streamId);
    if (expectedVersion !== undefined && expectedVersion !== events.length) {
      throw new ConcurrencyError(streamId, expectedVersion, events.length);
    }

    const appended = [];
    for (const draft of newEvents) {
      const event = {
        streamId,
        type: draft.type,
        payload: draft.payload,
        version: events.length,
        timestamp: draft.timestamp !== undefined ? draft.timestamp : Date.now(),
      };
      events.push(event);
      appended.push(event);
    }
    return appended;
  }

  getStream(streamId) {
    return this._streamEvents(streamId).slice();
  }

  allEvents() {
    const all = [];
    for (const events of this._streams.values()) {
      all.push(...events);
    }
    all.sort((a, b) => a.timestamp - b.timestamp);
    return all;
  }
}

module.exports = { EventStore, ConcurrencyError };
