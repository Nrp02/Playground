'use strict';

const { RGA } = require('./rga.js');

class Replica {
  constructor(siteId) {
    this.rga = new RGA(siteId);
    this.outbox = [];
  }

  insert(pos, char) {
    const op = this.rga.localInsert(pos, char);
    this.outbox.push(op);
    return op;
  }

  insertText(pos, text) {
    const ops = [];
    for (let i = 0; i < text.length; i++) {
      ops.push(this.insert(pos + i, text[i]));
    }
    return ops;
  }

  delete(pos) {
    const op = this.rga.localDelete(pos);
    this.outbox.push(op);
    return op;
  }

  receive(op) {
    this.rga.applyRemote(op);
  }

  receiveLog(ops) {
    for (const op of ops) {
      this.receive(op);
    }
  }

  drainOutbox() {
    const ops = this.outbox;
    this.outbox = [];
    return ops;
  }

  getText() {
    return this.rga.getText();
  }
}

module.exports = { Replica };
