'use strict';

function resolveBinaryHeapClass() {
  if (typeof module !== 'undefined' && module.exports) {
    return require('./heap.js').BinaryHeap;
  }
  return window.BinaryHeap;
}

const BinaryHeapClass = resolveBinaryHeapClass();

class TaskScheduler {
  constructor() {
    this._sequence = 0;
    this._heap = new BinaryHeapClass((a, b) => {
      if (a.priority !== b.priority) {
        return a.priority - b.priority;
      }
      return a.sequence - b.sequence;
    });
    this._log = [];
  }

  get pending() {
    return this._heap.size;
  }

  isEmpty() {
    return this._heap.isEmpty();
  }

  schedule(name, priority) {
    if (!Number.isFinite(priority)) {
      throw new TypeError('priority must be a finite number');
    }
    const task = { id: this._sequence, name, priority, sequence: this._sequence };
    this._sequence += 1;
    this._heap.push(task);
    return task.id;
  }

  peekNext() {
    return this._heap.isEmpty() ? null : this._heap.peek();
  }

  snapshot() {
    return this._heap.toSortedArray();
  }

  runNext() {
    if (this._heap.isEmpty()) {
      return null;
    }
    const task = this._heap.pop();
    this._log.push(task.name);
    return task;
  }

  runAll() {
    const executed = [];
    while (!this.isEmpty()) {
      executed.push(this.runNext());
    }
    return executed;
  }

  executionLog() {
    return this._log.slice();
  }
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { TaskScheduler };
}
if (typeof window !== 'undefined') {
  window.TaskScheduler = TaskScheduler;
}
