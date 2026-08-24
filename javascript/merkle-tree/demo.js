'use strict';

const { MerkleTree } = require('./merkleTree.js');

function main() {
  const blocks = [
    'block-0: alice pays bob 10',
    'block-1: bob pays carol 5',
    'block-2: carol pays dave 3',
    'block-3: dave pays alice 1',
    'block-4: alice pays eve 7',
  ];

  console.log('=== building tree ===');
  const tree = new MerkleTree(blocks);
  console.log(`blocks: ${tree.blockCount}`);
  console.log(`root:   ${tree.getRoot()}`);

  console.log('\n=== inclusion proof for block-2 ===');
  const index = 2;
  const proof = tree.getProof(index);
  console.log(`proof length: ${proof.length}`);
  const validCheck = MerkleTree.verifyProof(blocks[index], proof, tree.getRoot());
  console.log(`verify(correct data, correct root): ${validCheck}`);

  const tamperedDataCheck = MerkleTree.verifyProof('tampered data', proof, tree.getRoot());
  console.log(`verify(tampered data, correct root): ${tamperedDataCheck}`);

  const wrongRootCheck = MerkleTree.verifyProof(blocks[index], proof, '0'.repeat(64));
  console.log(`verify(correct data, wrong root):    ${wrongRootCheck}`);

  console.log('\n=== modifying block-2 and diffing trees ===');
  const modifiedBlocks = blocks.slice();
  modifiedBlocks[2] = 'block-2: carol pays dave 999';
  const modifiedTree = new MerkleTree(modifiedBlocks);

  console.log(`original root: ${tree.getRoot()}`);
  console.log(`modified root: ${modifiedTree.getRoot()}`);

  const changedIndices = tree.diff(modifiedTree);
  console.log(`changed leaf indices: ${JSON.stringify(changedIndices)}`);
}

main();
