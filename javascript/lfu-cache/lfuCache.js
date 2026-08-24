'use strict';

class DLLNode {
  constructor(key, value, freq) {
    this.key = key;
    this.value = value;
    this.freq = freq;
    this.prev = null;
    this.next = null;
  }
}

class DoublyLinkedList {
  constructor() {
    this.head = new DLLNode(null, null, null);
    this.tail = new DLLNode(null, null, null);
    this.head.next = this.tail;
    this.tail.prev = this.head;
    this.size = 0;
  }

  addFront(node) {
    node.prev = this.head;
    node.next = this.head.next;
    this.head.next.prev = node;
    this.head.next = node;
    this.size += 1;
  }

  remove(node) {
    node.prev.next = node.next;
    node.next.prev = node.prev;
    node.prev = null;
    node.next = null;
    this.size -= 1;
  }

  removeLast() {
    if (this.size === 0) {
      return null;
    }
    const node = this.tail.prev;
    this.remove(node);
    return node;
  }

  isEmpty() {
    return this.size === 0;
  }
}

class LFUCache {
  constructor(capacity) {
    if (!Number.isInteger(capacity) || capacity < 0) {
      throw new RangeError('capacity must be a non-negative integer');
    }
    this.capacity = capacity;
    this._nodes = new Map();
    this._freqLists = new Map();
    this._minFreq = 0;
  }

  _listFor(freq) {
    let list = this._freqLists.get(freq);
    if (!list) {
      list = new DoublyLinkedList();
      this._freqLists.set(freq, list);
    }
    return list;
  }

  _touch(node) {
    const oldFreq = node.freq;
    const oldList = this._freqLists.get(oldFreq);
    oldList.remove(node);
    if (oldList.isEmpty()) {
      this._freqLists.delete(oldFreq);
      if (this._minFreq === oldFreq) {
        this._minFreq += 1;
      }
    }
    node.freq += 1;
    this._listFor(node.freq).addFront(node);
  }

  get(key) {
    const node = this._nodes.get(key);
    if (!node) {
      return -1;
    }
    this._touch(node);
    return node.value;
  }

  has(key) {
    return this._nodes.has(key);
  }

  put(key, value) {
    if (this.capacity === 0) {
      return;
    }
    const existing = this._nodes.get(key);
    if (existing) {
      existing.value = value;
      this._touch(existing);
      return;
    }

    if (this._nodes.size >= this.capacity) {
      const evictList = this._freqLists.get(this._minFreq);
      const evicted = evictList.removeLast();
      this._nodes.delete(evicted.key);
      if (evictList.isEmpty()) {
        this._freqLists.delete(this._minFreq);
      }
    }

    const node = new DLLNode(key, value, 1);
    this._nodes.set(key, node);
    this._listFor(1).addFront(node);
    this._minFreq = 1;
  }

  peekFrequency(key) {
    const node = this._nodes.get(key);
    return node ? node.freq : -1;
  }

  get size() {
    return this._nodes.size;
  }
}

module.exports = { LFUCache };
