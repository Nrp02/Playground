'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { hashObject, writeObject, readObject, init } = require('./objects.js');

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
  const workDir = fs.mkdtempSync(path.join(os.tmpdir(), 'git-implementation-test-'));
  const gitDir = init(workDir);
  return { workDir, gitDir };
}

test('hashObject matches the known real-git blob hash for "hello world\\n"', () => {
  const sha1Hex = hashObject('blob', 'hello world\n');
  assert.strictEqual(sha1Hex, '3b18e512dba79e4c8300dd08aeb37f8e728b8dad');
});

test('hashObject is deterministic for the same type and content', () => {
  const first = hashObject('blob', 'repeat me\n');
  const second = hashObject('blob', 'repeat me\n');
  assert.strictEqual(first, second);
});

test('hashObject differs for different content', () => {
  const a = hashObject('blob', 'a\n');
  const b = hashObject('blob', 'b\n');
  assert.notStrictEqual(a, b);
});

test('writeObject then readObject round trips a blob', () => {
  const { workDir, gitDir } = makeTempRepo();
  try {
    const sha1Hex = writeObject(gitDir, 'blob', 'round trip content\n');
    const { type, content } = readObject(gitDir, sha1Hex);
    assert.strictEqual(type, 'blob');
    assert.strictEqual(content.toString('utf8'), 'round trip content\n');
  } finally {
    fs.rmSync(workDir, { recursive: true, force: true });
  }
});

test('writeObject stores at .git/objects/<2 hex>/<38 hex> loose layout', () => {
  const { workDir, gitDir } = makeTempRepo();
  try {
    const sha1Hex = writeObject(gitDir, 'blob', 'layout check\n');
    const objectFile = path.join(gitDir, 'objects', sha1Hex.slice(0, 2), sha1Hex.slice(2));
    assert.strictEqual(fs.existsSync(objectFile), true);
  } finally {
    fs.rmSync(workDir, { recursive: true, force: true });
  }
});

test('writing the same content twice produces the same hash and does not error', () => {
  const { workDir, gitDir } = makeTempRepo();
  try {
    const first = writeObject(gitDir, 'blob', 'dedup me\n');
    const second = writeObject(gitDir, 'blob', 'dedup me\n');
    assert.strictEqual(first, second);
    const { content } = readObject(gitDir, first);
    assert.strictEqual(content.toString('utf8'), 'dedup me\n');
  } finally {
    fs.rmSync(workDir, { recursive: true, force: true });
  }
});

test('init creates the .git/objects and .git/refs directory skeleton', () => {
  const workDir = fs.mkdtempSync(path.join(os.tmpdir(), 'git-implementation-init-test-'));
  try {
    const gitDir = init(workDir);
    assert.strictEqual(fs.existsSync(path.join(gitDir, 'objects')), true);
    assert.strictEqual(fs.existsSync(path.join(gitDir, 'refs', 'heads')), true);
    assert.strictEqual(fs.existsSync(path.join(gitDir, 'refs', 'tags')), true);
  } finally {
    fs.rmSync(workDir, { recursive: true, force: true });
  }
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
