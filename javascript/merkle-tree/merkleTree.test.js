'use strict';

const assert = require('assert');
const { MerkleTree, hashLeaf, hashPair } = require('./merkleTree.js');

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

const sampleBlocks = ['alpha', 'bravo', 'charlie', 'delta', 'echo'];

test('root hash is deterministic across identical builds', () => {
  const treeA = new MerkleTree(sampleBlocks);
  const treeB = new MerkleTree(sampleBlocks.slice());
  assert.strictEqual(treeA.getRoot(), treeB.getRoot());
});

test('root hash changes when any block changes', () => {
  const original = new MerkleTree(sampleBlocks);
  const changed = sampleBlocks.slice();
  changed[3] = 'delta-modified';
  const modified = new MerkleTree(changed);
  assert.notStrictEqual(original.getRoot(), modified.getRoot());
});

test('single-block tree root equals the leaf hash', () => {
  const tree = new MerkleTree(['solo']);
  assert.strictEqual(tree.getRoot(), hashLeaf('solo'));
});

test('root matches manual computation for a 4-leaf tree', () => {
  const blocks = ['a', 'b', 'c', 'd'];
  const tree = new MerkleTree(blocks);
  const h0 = hashLeaf('a');
  const h1 = hashLeaf('b');
  const h2 = hashLeaf('c');
  const h3 = hashLeaf('d');
  const left = hashPair(h0, h1);
  const right = hashPair(h2, h3);
  const expectedRoot = hashPair(left, right);
  assert.strictEqual(tree.getRoot(), expectedRoot);
});

test('root handles odd leaf counts via last-node duplication', () => {
  const blocks = ['a', 'b', 'c'];
  const tree = new MerkleTree(blocks);
  const h0 = hashLeaf('a');
  const h1 = hashLeaf('b');
  const h2 = hashLeaf('c');
  const left = hashPair(h0, h1);
  const right = hashPair(h2, h2);
  const expectedRoot = hashPair(left, right);
  assert.strictEqual(tree.getRoot(), expectedRoot);
});

test('constructor rejects empty block lists', () => {
  assert.throws(() => new MerkleTree([]), RangeError);
});

test('valid proof verifies for every leaf index', () => {
  const tree = new MerkleTree(sampleBlocks);
  for (let i = 0; i < sampleBlocks.length; i++) {
    const proof = tree.getProof(i);
    assert.strictEqual(MerkleTree.verifyProof(sampleBlocks[i], proof, tree.getRoot()), true);
  }
});

test('valid proof verifies on an odd-sized tree', () => {
  const blocks = ['a', 'b', 'c', 'd', 'e', 'f', 'g'];
  const tree = new MerkleTree(blocks);
  for (let i = 0; i < blocks.length; i++) {
    const proof = tree.getProof(i);
    assert.strictEqual(MerkleTree.verifyProof(blocks[i], proof, tree.getRoot()), true);
  }
});

test('proof rejects tampered leaf data', () => {
  const tree = new MerkleTree(sampleBlocks);
  const proof = tree.getProof(1);
  assert.strictEqual(MerkleTree.verifyProof('not-bravo', proof, tree.getRoot()), false);
});

test('proof rejects a wrong claimed root', () => {
  const tree = new MerkleTree(sampleBlocks);
  const proof = tree.getProof(1);
  assert.strictEqual(MerkleTree.verifyProof(sampleBlocks[1], proof, hashLeaf('unrelated')), false);
});

test('proof rejects tampered sibling hashes', () => {
  const tree = new MerkleTree(sampleBlocks);
  const proof = tree.getProof(1);
  const tamperedProof = proof.map((step, i) => (i === 0 ? { ...step, hash: hashLeaf('forged') } : step));
  assert.strictEqual(MerkleTree.verifyProof(sampleBlocks[1], tamperedProof, tree.getRoot()), false);
});

test('proof from one tree does not verify against a different tree root', () => {
  const treeA = new MerkleTree(sampleBlocks);
  const changed = sampleBlocks.slice();
  changed[4] = 'echo-modified';
  const treeB = new MerkleTree(changed);
  const proof = treeA.getProof(0);
  assert.strictEqual(MerkleTree.verifyProof(sampleBlocks[0], proof, treeB.getRoot()), false);
});

test('getProof rejects out-of-range indices', () => {
  const tree = new MerkleTree(sampleBlocks);
  assert.throws(() => tree.getProof(-1), RangeError);
  assert.throws(() => tree.getProof(sampleBlocks.length), RangeError);
});

test('diff reports no differences for identical block lists', () => {
  const treeA = new MerkleTree(sampleBlocks);
  const treeB = new MerkleTree(sampleBlocks.slice());
  assert.deepStrictEqual(treeA.diff(treeB), []);
});

test('diff identifies a single changed leaf', () => {
  const treeA = new MerkleTree(sampleBlocks);
  const changed = sampleBlocks.slice();
  changed[2] = 'charlie-modified';
  const treeB = new MerkleTree(changed);
  assert.deepStrictEqual(treeA.diff(treeB), [2]);
});

test('diff identifies multiple changed leaves', () => {
  const blocks = ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'];
  const treeA = new MerkleTree(blocks);
  const changed = blocks.slice();
  changed[0] = 'a-modified';
  changed[5] = 'f-modified';
  const treeB = new MerkleTree(changed);
  assert.deepStrictEqual(treeA.diff(treeB), [0, 5]);
});

test('diff identifies changes on an odd-sized tree, including the duplicated leaf', () => {
  const blocks = ['a', 'b', 'c'];
  const treeA = new MerkleTree(blocks);
  const changed = blocks.slice();
  changed[2] = 'c-modified';
  const treeB = new MerkleTree(changed);
  assert.deepStrictEqual(treeA.diff(treeB), [2]);
});

test('diff is symmetric regardless of call direction', () => {
  const blocks = ['a', 'b', 'c', 'd'];
  const treeA = new MerkleTree(blocks);
  const changed = blocks.slice();
  changed[3] = 'd-modified';
  const treeB = new MerkleTree(changed);
  assert.deepStrictEqual(treeA.diff(treeB), treeB.diff(treeA));
});

test('diff throws when block lists differ in length', () => {
  const treeA = new MerkleTree(['a', 'b', 'c']);
  const treeB = new MerkleTree(['a', 'b', 'c', 'd']);
  assert.throws(() => treeA.diff(treeB), RangeError);
});

test('diff throws when argument is not a MerkleTree', () => {
  const treeA = new MerkleTree(['a', 'b']);
  assert.throws(() => treeA.diff({ blockCount: 2 }), TypeError);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
