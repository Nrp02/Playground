'use strict';

function compareId(a, b) {
  if (a.counter !== b.counter) return a.counter - b.counter;
  if (a.site < b.site) return -1;
  if (a.site > b.site) return 1;
  return 0;
}

function idKey(id) {
  return id === null ? 'HEAD' : `${id.counter}@${id.site}`;
}

class RGA {
  constructor(siteId) {
    if (typeof siteId !== 'string' || siteId.length === 0) {
      throw new TypeError('siteId must be a non-empty string');
    }
    this.siteId = siteId;
    this.counter = 0;
    this.list = [];
    this.pending = [];
  }

  _nextId() {
    this.counter += 1;
    return { site: this.siteId, counter: this.counter };
  }

  _findIndexById(id) {
    if (id === null) return -1;
    const key = idKey(id);
    return this.list.findIndex((n) => idKey(n.id) === key);
  }

  _hasId(id) {
    return id === null || this._findIndexById(id) !== -1;
  }

  _visibleNodeAt(pos) {
    let count = -1;
    for (let i = 0; i < this.list.length; i++) {
      if (!this.list[i].tombstone) {
        count++;
        if (count === pos) return this.list[i];
      }
    }
    throw new RangeError(`position ${pos} out of range`);
  }

  _originIdForInsertAt(pos) {
    if (pos === 0) return null;
    return this._visibleNodeAt(pos - 1).id;
  }

  _integrateInsert(node) {
    const leftIdx = this._findIndexById(node.originId);
    let i = leftIdx + 1;
    while (i < this.list.length) {
      const other = this.list[i];
      const otherOriginIdx = this._findIndexById(other.originId);
      if (otherOriginIdx < leftIdx) break;
      if (otherOriginIdx === leftIdx) {
        if (compareId(other.id, node.id) > 0) {
          i++;
          continue;
        }
        break;
      }
      i++;
    }
    this.list.splice(i, 0, node);
  }

  localInsert(pos, char) {
    if (typeof char !== 'string' || char.length !== 1) {
      throw new TypeError('char must be a single character string');
    }
    const originId = this._originIdForInsertAt(pos);
    const id = this._nextId();
    const node = { id, value: char, tombstone: false, originId };
    this._integrateInsert(node);
    return { type: 'insert', id, value: char, originId };
  }

  localDelete(pos) {
    const node = this._visibleNodeAt(pos);
    node.tombstone = true;
    return { type: 'delete', id: node.id };
  }

  applyRemote(op) {
    this.pending.push(op);
    this._drainPending();
  }

  _drainPending() {
    let progress = true;
    while (progress) {
      progress = false;
      for (let idx = 0; idx < this.pending.length; idx++) {
        const op = this.pending[idx];
        if (op.type === 'insert') {
          if (this._hasId(op.originId)) {
            if (!this._hasId(op.id)) {
              const node = { id: op.id, value: op.value, tombstone: false, originId: op.originId };
              this._integrateInsert(node);
            }
            this.pending.splice(idx, 1);
            progress = true;
            break;
          }
        } else if (op.type === 'delete') {
          if (this._hasId(op.id)) {
            const targetIdx = this._findIndexById(op.id);
            this.list[targetIdx].tombstone = true;
            this.pending.splice(idx, 1);
            progress = true;
            break;
          }
        } else {
          throw new TypeError(`unknown op type: ${op.type}`);
        }
      }
    }
  }

  getText() {
    return this.list.filter((n) => !n.tombstone).map((n) => n.value).join('');
  }

  pendingCount() {
    return this.pending.length;
  }
}

module.exports = { RGA, compareId, idKey };
