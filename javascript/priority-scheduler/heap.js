'use strict';

class BinaryHeap {
  constructor(compare) {
    this._items = [];
    this._compare = compare || ((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  }

  static from(array, compare) {
    const heap = new BinaryHeap(compare);
    heap._items = array.slice();
    for (let i = Math.floor(heap._items.length / 2) - 1; i >= 0; i--) {
      heap._siftDown(i);
    }
    return heap;
  }

  get size() {
    return this._items.length;
  }

  isEmpty() {
    return this._items.length === 0;
  }

  peek() {
    if (this.isEmpty()) {
      throw new Error('peek() called on an empty heap');
    }
    return this._items[0];
  }

  push(value) {
    this._items.push(value);
    this._siftUp(this._items.length - 1);
  }

  pop() {
    if (this.isEmpty()) {
      throw new Error('pop() called on an empty heap');
    }
    const top = this._items[0];
    const last = this._items.pop();
    if (this._items.length > 0) {
      this._items[0] = last;
      this._siftDown(0);
    }
    return top;
  }

  toArray() {
    return this._items.slice();
  }

  toSortedArray() {
    const clone = BinaryHeap.from(this._items, this._compare);
    const out = [];
    while (!clone.isEmpty()) {
      out.push(clone.pop());
    }
    return out;
  }

  _siftUp(index) {
    let child = index;
    while (child > 0) {
      const parent = (child - 1) >> 1;
      if (this._compare(this._items[child], this._items[parent]) >= 0) {
        break;
      }
      this._swap(child, parent);
      child = parent;
    }
  }

  _siftDown(index) {
    let parent = index;
    const n = this._items.length;
    for (;;) {
      const left = parent * 2 + 1;
      const right = parent * 2 + 2;
      let smallest = parent;

      if (left < n && this._compare(this._items[left], this._items[smallest]) < 0) {
        smallest = left;
      }
      if (right < n && this._compare(this._items[right], this._items[smallest]) < 0) {
        smallest = right;
      }
      if (smallest === parent) {
        break;
      }
      this._swap(parent, smallest);
      parent = smallest;
    }
  }

  _swap(i, j) {
    const tmp = this._items[i];
    this._items[i] = this._items[j];
    this._items[j] = tmp;
  }
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { BinaryHeap };
}
if (typeof window !== 'undefined') {
  window.BinaryHeap = BinaryHeap;
}
