'use strict';

class BKTree {
  constructor(distanceFn) {
    this.distanceFn = distanceFn;
    this.root = null;
    this.size = 0;
  }

  insert(word) {
    this.size++;
    if (this.root === null) {
      this.root = { word, children: new Map() };
      return;
    }
    let node = this.root;
    for (;;) {
      const d = this.distanceFn(word, node.word);
      if (d === 0) return;
      const next = node.children.get(d);
      if (!next) {
        node.children.set(d, { word, children: new Map() });
        return;
      }
      node = next;
    }
  }

  search(query, maxDistance) {
    const results = [];
    let nodesVisited = 0;
    if (this.root === null) return { results, nodesVisited };
    const stack = [this.root];
    while (stack.length > 0) {
      const node = stack.pop();
      nodesVisited++;
      const d = this.distanceFn(query, node.word);
      if (d <= maxDistance) results.push({ word: node.word, distance: d });
      const lo = d - maxDistance;
      const hi = d + maxDistance;
      for (const [childDistance, child] of node.children) {
        if (childDistance >= lo && childDistance <= hi) stack.push(child);
      }
    }
    return { results, nodesVisited };
  }
}

module.exports = { BKTree };
