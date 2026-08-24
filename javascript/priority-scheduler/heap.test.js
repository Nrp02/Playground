'use strict';

const assert = require('assert');
const { BinaryHeap } = require('./heap.js');

let passCount = 0;
let failCount = 0;

function test(name, fn) {
  try {
    fn();
    passCount++;
    console.log(`  PASS  ${name}`);
  } catch (err) {
    failCount++;
    console.log(`  FAIL  ${name}`);
    console.log(`        ${err.message}`);
  }
}

test('new heap is empty', () => {
  const heap = new BinaryHeap();
  assert.strictEqual(heap.size, 0);
  assert.strictEqual(heap.isEmpty(), true);
});

test('peek and pop throw on an empty heap', () => {
  const heap = new BinaryHeap();
  assert.throws(() => heap.peek());
  assert.throws(() => heap.pop());
});

test('pop returns the minimum value by default', () => {
  const heap = new BinaryHeap();
  [5, 3, 8, 1, 9, 2].forEach((n) => heap.push(n));
  const out = [];
  while (!heap.isEmpty()) {
    out.push(heap.pop());
  }
  assert.deepStrictEqual(out, [1, 2, 3, 5, 8, 9]);
});

test('peek returns the min without removing it', () => {
  const heap = new BinaryHeap();
  heap.push(10);
  heap.push(4);
  assert.strictEqual(heap.peek(), 4);
  assert.strictEqual(heap.size, 2);
});

test('custom comparator makes a max-heap', () => {
  const heap = new BinaryHeap((a, b) => b - a);
  [5, 3, 8, 1, 9, 2].forEach((n) => heap.push(n));
  const out = [];
  while (!heap.isEmpty()) {
    out.push(heap.pop());
  }
  assert.deepStrictEqual(out, [9, 8, 5, 3, 2, 1]);
});

test('BinaryHeap.from heapifies an existing array in place order', () => {
  const heap = BinaryHeap.from([9, 5, 7, 1, 3, 8, 2]);
  assert.strictEqual(heap.size, 7);
  const out = [];
  while (!heap.isEmpty()) {
    out.push(heap.pop());
  }
  assert.deepStrictEqual(out, [1, 2, 3, 5, 7, 8, 9]);
});

test('toArray reflects current size without mutating the heap', () => {
  const heap = new BinaryHeap();
  heap.push(3);
  heap.push(1);
  heap.push(2);
  const snapshot = heap.toArray();
  assert.strictEqual(snapshot.length, 3);
  assert.strictEqual(heap.size, 3);
});

test('toSortedArray returns sorted values without draining the heap', () => {
  const heap = new BinaryHeap();
  [4, 2, 6, 1].forEach((n) => heap.push(n));
  assert.deepStrictEqual(heap.toSortedArray(), [1, 2, 4, 6]);
  assert.strictEqual(heap.size, 4);
});

test('handles duplicate values correctly', () => {
  const heap = new BinaryHeap();
  [5, 5, 1, 1, 3].forEach((n) => heap.push(n));
  const out = [];
  while (!heap.isEmpty()) {
    out.push(heap.pop());
  }
  assert.deepStrictEqual(out, [1, 1, 3, 5, 5]);
});

test('handles a large random sequence as a correct min-heap', () => {
  const values = [];
  let seed = 42;
  const rand = () => {
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    return seed % 1000;
  };
  for (let i = 0; i < 2000; i++) {
    values.push(rand());
  }
  const heap = new BinaryHeap();
  values.forEach((v) => heap.push(v));
  const out = [];
  while (!heap.isEmpty()) {
    out.push(heap.pop());
  }
  const expected = values.slice().sort((a, b) => a - b);
  assert.deepStrictEqual(out, expected);
});

console.log(`\n${passCount} passed, ${failCount} failed (${passCount + failCount} total)`);

if (failCount > 0) {
  process.exitCode = 1;
} else {
  console.log('All assertions passed.');
}
