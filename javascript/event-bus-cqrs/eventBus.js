'use strict';

const WILDCARD = '*';

class EventBus {
  constructor() {
    this._handlers = new Map();
  }

  subscribe(eventType, handler) {
    let handlers = this._handlers.get(eventType);
    if (!handlers) {
      handlers = new Set();
      this._handlers.set(eventType, handlers);
    }
    handlers.add(handler);
    return () => handlers.delete(handler);
  }

  publish(event) {
    for (const handler of this._handlers.get(event.type) || []) {
      handler(event);
    }
    for (const handler of this._handlers.get(WILDCARD) || []) {
      handler(event);
    }
  }

  publishAll(events) {
    for (const event of events) {
      this.publish(event);
    }
  }
}

module.exports = { EventBus, WILDCARD };
