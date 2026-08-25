'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { init, writeObject } = require('./objects.js');
const { writeTree } = require('./tree.js');
const { writeCommit, readCommit } = require('./commit.js');

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
  const workDir = fs.mkdtempSync(path.join(os.tmpdir(), 'git-implementation-commit-test-'));
  const gitDir = init(workDir);
  return { workDir, gitDir };
}

test('writeCommit then readCommit round trips tree, author, and message', () => {
  const { workDir, gitDir } = makeTempRepo();
  try {
    const blobSha = writeObject(gitDir, 'blob', 'content\n');
    const treeSha = writeTree(gitDir, [{ mode: '100644', name: 'file.txt', sha1Hex: blobSha }]);
    const author = 'Test User <test@example.com> 1700000000 +0000';
    const commitSha = writeCommit(gitDir, {
      treeSha1: treeSha,
      author,
      committer: author,
      message: 'a test commit\n',
    });
    const commit = readCommit(gitDir, commitSha);
    assert.strictEqual(commit.tree, treeSha);
    assert.strictEqual(commit.author, author);
    assert.strictEqual(commit.committer, author);
    assert.strictEqual(commit.parent, null);
    assert.strictEqual(commit.message.trim(), 'a test commit');
  } finally {
    fs.rmSync(workDir, { recursive: true, force: true });
  }
});

test('writeCommit includes an optional parent line', () => {
  const { workDir, gitDir } = makeTempRepo();
  try {
    const blobSha = writeObject(gitDir, 'blob', 'content\n');
    const treeSha = writeTree(gitDir, [{ mode: '100644', name: 'file.txt', sha1Hex: blobSha }]);
    const author = 'Test User <test@example.com> 1700000000 +0000';
    const firstCommitSha = writeCommit(gitDir, {
      treeSha1: treeSha,
      author,
      committer: author,
      message: 'first commit\n',
    });
    const secondCommitSha = writeCommit(gitDir, {
      treeSha1: treeSha,
      parentSha1: firstCommitSha,
      author,
      committer: author,
      message: 'second commit\n',
    });
    const commit = readCommit(gitDir, secondCommitSha);
    assert.strictEqual(commit.parent, firstCommitSha);
  } finally {
    fs.rmSync(workDir, { recursive: true, force: true });
  }
});

test('writing the same commit content twice produces the same hash', () => {
  const { workDir, gitDir } = makeTempRepo();
  try {
    const blobSha = writeObject(gitDir, 'blob', 'content\n');
    const treeSha = writeTree(gitDir, [{ mode: '100644', name: 'file.txt', sha1Hex: blobSha }]);
    const author = 'Test User <test@example.com> 1700000000 +0000';
    const options = {
      treeSha1: treeSha,
      author,
      committer: author,
      message: 'identical commit\n',
    };
    const first = writeCommit(gitDir, options);
    const second = writeCommit(gitDir, options);
    assert.strictEqual(first, second);
  } finally {
    fs.rmSync(workDir, { recursive: true, force: true });
  }
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
