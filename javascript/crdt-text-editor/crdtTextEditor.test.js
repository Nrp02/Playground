'use strict';

const assert = require('assert');
const { RGA } = require('./rga.js');
const { Replica } = require('./replica.js');

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

test('local insert builds text in order', () => {
  const rga = new RGA('a');
  rga.localInsert(0, 'h');
  rga.localInsert(1, 'i');
  assert.strictEqual(rga.getText(), 'hi');
});

test('local insert in the middle shifts correctly', () => {
  const rga = new RGA('a');
  rga.localInsert(0, 'h');
  rga.localInsert(1, 'o');
  rga.localInsert(1, 'e');
  rga.localInsert(2, 'l');
  rga.localInsert(3, 'l');
  assert.strictEqual(rga.getText(), 'hello');
});

test('local delete removes the correct character', () => {
  const rga = new RGA('a');
  rga.localInsert(0, 'h');
  rga.localInsert(1, 'e');
  rga.localInsert(2, 'y');
  rga.localDelete(1);
  assert.strictEqual(rga.getText(), 'hy');
});

test('insert rejects invalid characters', () => {
  const rga = new RGA('a');
  assert.throws(() => rga.localInsert(0, 'ab'), TypeError);
  assert.throws(() => rga.localInsert(0, ''), TypeError);
});

test('insert/delete out of range throws', () => {
  const rga = new RGA('a');
  assert.throws(() => rga.localInsert(1, 'x'), RangeError);
  assert.throws(() => rga.localDelete(0), RangeError);
});

test('two replicas converge after exchanging ops in-order', () => {
  const alice = new Replica('alice');
  const bob = new Replica('bob');

  alice.insertText(0, 'abc');
  for (const op of alice.drainOutbox()) bob.receive(op);

  bob.insert(3, 'd');
  bob.delete(0);
  for (const op of bob.drainOutbox()) alice.receive(op);

  assert.strictEqual(alice.getText(), bob.getText());
  assert.strictEqual(alice.getText(), 'bcd');
});

test('two replicas converge after exchanging ops out-of-order with delay', () => {
  const alice = new Replica('alice');
  const bob = new Replica('bob');

  alice.insertText(0, 'wxyz');
  const ops = alice.drainOutbox();

  const reversed = ops.slice().reverse();
  for (const op of reversed) bob.receive(op);

  assert.strictEqual(bob.getText(), alice.getText());
  assert.strictEqual(bob.getText(), 'wxyz');
});

test('delete op arriving before its insert is buffered then applied', () => {
  const alice = new Replica('alice');
  const bob = new Replica('bob');

  alice.insertText(0, 'ab');
  const insertOps = alice.drainOutbox();
  alice.delete(1);
  const deleteOp = alice.drainOutbox()[0];

  bob.receive(deleteOp);
  assert.strictEqual(bob.rga.pendingCount(), 1);
  assert.strictEqual(bob.getText(), '');

  for (const op of insertOps) bob.receive(op);

  assert.strictEqual(bob.rga.pendingCount(), 0);
  assert.strictEqual(bob.getText(), alice.getText());
  assert.strictEqual(bob.getText(), 'a');
});

test('concurrent inserts at the same position converge deterministically', () => {
  const alice = new Replica('alice');
  const bob = new Replica('bob');
  const carol = new Replica('carol');

  alice.insertText(0, 'go');
  const baseOps = alice.drainOutbox();
  for (const op of baseOps) {
    bob.receive(op);
    carol.receive(op);
  }

  alice.insert(1, '1');
  bob.insert(1, '2');
  carol.insert(1, '3');

  const aliceOps = alice.drainOutbox();
  const bobOps = bob.drainOutbox();
  const carolOps = carol.drainOutbox();

  bob.receive(carolOps[0]);
  bob.receive(aliceOps[0]);
  carol.receive(aliceOps[0]);
  carol.receive(bobOps[0]);
  alice.receive(bobOps[0]);
  alice.receive(carolOps[0]);

  assert.strictEqual(alice.getText(), bob.getText());
  assert.strictEqual(bob.getText(), carol.getText());
  assert.strictEqual(alice.getText().length, 5);
});

test('applying the same remote op twice is idempotent', () => {
  const alice = new Replica('alice');
  const bob = new Replica('bob');

  alice.insertText(0, 'idem');
  const ops = alice.drainOutbox();
  for (const op of ops) bob.receive(op);
  for (const op of ops) bob.receive(op);

  assert.strictEqual(bob.getText(), 'idem');
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
