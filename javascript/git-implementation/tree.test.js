'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { init, writeObject } = require('./objects.js');
const { sortEntries, writeTree, readTree, hashTree } = require('./tree.js');

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

function makeTempRepo() {
  const workDir = fs.mkdtempSync(path.join(os.tmpdir(), 'git-implementation-tree-test-'));
  const gitDir = init(workDir);
  return { workDir, gitDir };
}

test('sortEntries orders blob entries alphabetically by name', () => {
  const sorted = sortEntries([
    { mode: '100644', name: 'zeta.txt', sha1Hex: '0'.repeat(40) },
    { mode: '100644', name: 'alpha.txt', sha1Hex: '1'.repeat(40) },
    { mode: '100644', name: 'mid.txt', sha1Hex: '2'.repeat(40) },
  ]);
  assert.deepStrictEqual(sorted.map((e) => e.name), ['alpha.txt', 'mid.txt', 'zeta.txt']);
});

test('sortEntries treats directory entries as if suffixed with /', () => {
  const sorted = sortEntries([
    { mode: '100644', name: 'lib.txt', sha1Hex: '0'.repeat(40) },
    { mode: '40000', name: 'lib', sha1Hex: '1'.repeat(40) },
  ]);
  assert.deepStrictEqual(sorted.map((e) => e.name), ['lib.txt', 'lib']);
});

test('writeTree then readTree round trips entries sorted by name', () => {
  const { workDir, gitDir } = makeTempRepo();
  try {
    const bSha = writeObject(gitDir, 'blob', 'b content\n');
    const aSha = writeObject(gitDir, 'blob', 'a content\n');
    const treeSha = writeTree(gitDir, [
      { mode: '100644', name: 'b.txt', sha1Hex: bSha },
      { mode: '100644', name: 'a.txt', sha1Hex: aSha },
    ]);
    const entries = readTree(gitDir, treeSha);
    assert.deepStrictEqual(entries.map((e) => e.name), ['a.txt', 'b.txt']);
    assert.strictEqual(entries[0].sha1Hex, aSha);
    assert.strictEqual(entries[1].sha1Hex, bSha);
  } finally {
    fs.rmSync(workDir, { recursive: true, force: true });
  }
});

test('hashTree is deterministic and matches writeTree result', () => {
  const { workDir, gitDir } = makeTempRepo();
  try {
    const sha = writeObject(gitDir, 'blob', 'stable content\n');
    const entries = [{ mode: '100644', name: 'file.txt', sha1Hex: sha }];
    const expected = hashTree(entries);
    const actual = writeTree(gitDir, entries);
    assert.strictEqual(actual, expected);
  } finally {
    fs.rmSync(workDir, { recursive: true, force: true });
  }
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
