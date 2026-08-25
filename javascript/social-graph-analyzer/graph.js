'use strict';

class SocialGraph {
  constructor() {
    this._adjacency = new Map();
  }

  addNode(id) {
    if (!this._adjacency.has(id)) {
      this._adjacency.set(id, new Set());
    }
    return this;
  }

  addEdge(a, b) {
    if (a === b) {
      throw new RangeError('cannot connect a node to itself');
    }
    this.addNode(a);
    this.addNode(b);
    this._adjacency.get(a).add(b);
    this._adjacency.get(b).add(a);
    return this;
  }

  hasNode(id) {
    return this._adjacency.has(id);
  }

  hasEdge(a, b) {
    return this._adjacency.has(a) && this._adjacency.get(a).has(b);
  }

  neighbors(id) {
    const set = this._adjacency.get(id);
    if (!set) {
      throw new RangeError(`unknown node: ${id}`);
    }
    return Array.from(set);
  }

  degree(id) {
    return this.neighbors(id).length;
  }

  nodes() {
    return Array.from(this._adjacency.keys());
  }

  edgeCount() {
    let total = 0;
    for (const set of this._adjacency.values()) {
      total += set.size;
    }
    return total / 2;
  }

  size() {
    return this._adjacency.size;
  }
}

module.exports = { SocialGraph };
