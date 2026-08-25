'use strict';

const { Replica } = require('./replica.js');

function shuffle(arr, seed) {
  const a = arr.slice();
  let s = seed;
  const rand = () => {
    s = (s * 1103515245 + 12345) & 0x7fffffff;
    return s / 0x7fffffff;
  };
  for (let i = a.length - 1; i > 0; i--) {
    const j = Math.floor(rand() * (i + 1));
    [a[i], a[j]] = [a[j], a[i]];
  }
  return a;
}

function main() {
  console.log('=== scenario 1: in-order delivery ===');
  const alice = new Replica('alice');
  const bob = new Replica('bob');

  alice.insertText(0, 'hello');
  const aliceOps = alice.drainOutbox();
  for (const op of aliceOps) bob.receive(op);

  bob.insertText(5, ' world');
  const bobOps = bob.drainOutbox();
  for (const op of bobOps) alice.receive(op);

  console.log(`alice: "${alice.getText()}"`);
  console.log(`bob:   "${bob.getText()}"`);
  console.log(`converged: ${alice.getText() === bob.getText()}`);

  console.log('\n=== scenario 2: out-of-order / delayed delivery ===');
  const carol = new Replica('carol');
  const dave = new Replica('dave');

  carol.insertText(0, 'CRDT');
  const carolOps = carol.drainOutbox();
  const daveOps = [];
  for (let i = 0; i < carolOps.length; i++) {
    dave.insert(Math.min(i, dave.getText().length), '?');
    dave.delete(dave.getText().length - 1);
    daveOps.push(...dave.drainOutbox());
  }

  const reordered = shuffle(carolOps, 42);
  console.log(`delivering ${reordered.length} carol ops to dave, out of order`);
  for (const op of reordered) dave.receive(op);
  console.log(`dave pending after out-of-order delivery: ${dave.rga.pendingCount()}`);

  for (const op of daveOps) carol.receive(op);

  console.log(`carol: "${carol.getText()}"`);
  console.log(`dave:  "${dave.getText()}"`);
  console.log(`converged: ${carol.getText() === dave.getText()}`);

  console.log('\n=== scenario 3: three replicas, concurrent inserts at same position ===');
  const r1 = new Replica('r1');
  const r2 = new Replica('r2');
  const r3 = new Replica('r3');

  r1.insertText(0, 'base');
  const baseOps = r1.drainOutbox();
  for (const op of baseOps) {
    r2.receive(op);
    r3.receive(op);
  }

  r1.insert(2, 'X');
  r2.insert(2, 'Y');
  r3.insert(2, 'Z');

  const r1Ops = r1.drainOutbox();
  const r2Ops = r2.drainOutbox();
  const r3Ops = r3.drainOutbox();

  const allOps = [...r1Ops, ...r2Ops, ...r3Ops];
  const orderForR1 = shuffle(allOps, 7).filter((op) => !r1Ops.includes(op));
  const orderForR2 = shuffle(allOps, 99).filter((op) => !r2Ops.includes(op));
  const orderForR3 = shuffle(allOps, 1337).filter((op) => !r3Ops.includes(op));

  for (const op of orderForR1) r1.receive(op);
  for (const op of orderForR2) r2.receive(op);
  for (const op of orderForR3) r3.receive(op);

  console.log(`r1: "${r1.getText()}"`);
  console.log(`r2: "${r2.getText()}"`);
  console.log(`r3: "${r3.getText()}"`);
  console.log(`all converged: ${r1.getText() === r2.getText() && r2.getText() === r3.getText()}`);
}

main();
