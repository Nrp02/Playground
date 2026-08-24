'use strict';

const crypto = require('crypto');

function sha256Hex(input) {
  return crypto.createHash('sha256').update(input).digest('hex');
}

function toBuffer(data) {
  return Buffer.isBuffer(data) ? data : Buffer.from(String(data));
}

function hashLeaf(data) {
  return sha256Hex(toBuffer(data));
}

function hashPair(leftHex, rightHex) {
  return sha256Hex(leftHex + rightHex);
}

class MerkleTree {
  constructor(blocks) {
    if (!Array.isArray(blocks) || blocks.length === 0) {
      throw new RangeError('MerkleTree requires a non-empty array of blocks');
    }
    this.blockCount = blocks.length;
    this.levels = MerkleTree._buildLevels(blocks);
  }

  static _buildLevels(blocks) {
    const levels = [blocks.map(hashLeaf)];
    let current = levels[0];
    while (current.length > 1) {
      const next = [];
      for (let i = 0; i < current.length; i += 2) {
        const left = current[i];
        const right = i + 1 < current.length ? current[i + 1] : current[i];
        next.push(hashPair(left, right));
      }
      levels.push(next);
      current = next;
    }
    return levels;
  }

  getRoot() {
    return this.levels[this.levels.length - 1][0];
  }

  getProof(index) {
    if (!Number.isInteger(index) || index < 0 || index >= this.blockCount) {
      throw new RangeError(`index ${index} out of range`);
    }
    const proof = [];
    let idx = index;
    for (let level = 0; level < this.levels.length - 1; level++) {
      const currentLevel = this.levels[level];
      const isRightNode = idx % 2 === 1;
      const siblingIdx = isRightNode ? idx - 1 : Math.min(idx + 1, currentLevel.length - 1);
      proof.push({
        hash: currentLevel[siblingIdx],
        position: isRightNode ? 'left' : 'right',
      });
      idx = Math.floor(idx / 2);
    }
    return proof;
  }

  static verifyProof(data, proof, rootHash) {
    let hash = hashLeaf(data);
    for (const step of proof) {
      hash = step.position === 'left' ? hashPair(step.hash, hash) : hashPair(hash, step.hash);
    }
    return hash === rootHash;
  }

  diff(other) {
    if (!(other instanceof MerkleTree)) {
      throw new TypeError('diff requires another MerkleTree instance');
    }
    if (this.blockCount !== other.blockCount) {
      throw new RangeError('diff requires trees built from block lists of the same length');
    }
    const changed = [];
    const topLevel = this.levels.length - 1;
    MerkleTree._diffNode(this, other, topLevel, 0, changed);
    return changed.sort((a, b) => a - b);
  }

  static _diffNode(treeA, treeB, level, index, changed) {
    const hashA = treeA.levels[level][index];
    const hashB = treeB.levels[level][index];
    if (hashA === hashB) {
      return;
    }
    if (level === 0) {
      changed.push(index);
      return;
    }
    const levelLen = treeA.levels[level - 1].length;
    const leftChild = index * 2;
    const rightChild = Math.min(index * 2 + 1, levelLen - 1);
    MerkleTree._diffNode(treeA, treeB, level - 1, leftChild, changed);
    if (rightChild !== leftChild) {
      MerkleTree._diffNode(treeA, treeB, level - 1, rightChild, changed);
    }
  }
}

module.exports = { MerkleTree, hashLeaf, hashPair };
