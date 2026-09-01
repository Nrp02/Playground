'use strict';

const assert = require('assert');
const { GraphDB } = require('./query.js');

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
    console.log(`        ${err.stack}`);
  }
}

function buildSampleDb() {
  const db = new GraphDB();
  db.addNode({ id: 'alice', labels: ['Person'], properties: { name: 'Alice', age: 30 } });
  db.addNode({ id: 'bob', labels: ['Person'], properties: { name: 'Bob', age: 25 } });
  db.addNode({ id: 'carol', labels: ['Person'], properties: { name: 'Carol', age: 40 } });
  db.addNode({ id: 'dave', labels: ['Person'], properties: { name: 'Dave', age: 22 } });
  db.addNode({ id: 'acme', labels: ['Company'], properties: { name: 'Acme' } });
  db.addNode({ id: 'globex', labels: ['Company'], properties: { name: 'Globex' } });
  db.addNode({ id: 'nyc', labels: ['City'], properties: { name: 'NYC' } });

  db.addEdge({ type: 'KNOWS', from: 'alice', to: 'bob' });
  db.addEdge({ type: 'KNOWS', from: 'bob', to: 'carol' });
  db.addEdge({ type: 'KNOWS', from: 'carol', to: 'alice' });
  db.addEdge({ type: 'KNOWS', from: 'bob', to: 'dave' });
  db.addEdge({ type: 'WORKS_AT', from: 'carol', to: 'acme' });
  db.addEdge({ type: 'WORKS_AT', from: 'dave', to: 'globex' });
  db.addEdge({ type: 'LIVES_IN', from: 'alice', to: 'nyc' });
  return db;
}

function bruteForceMatchLabel(db, label, where) {
  const results = [];
  for (const node of db.graph.nodes.values()) {
    if (label && !node.labels.has(label)) continue;
    if (where) {
      let ok = true;
      for (const key of Object.keys(where)) {
        const cond = where[key];
        const value = node.properties[key];
        if (typeof cond === 'function') {
          if (!cond(value, node)) ok = false;
        } else if (value !== cond) ok = false;
      }
      if (!ok) continue;
    }
    results.push(node);
  }
  return results;
}

function bruteForceOut(db, nodeIds, edgeType, where) {
  const next = new Set();
  for (const nodeId of nodeIds) {
    for (const edge of db.graph.edges.values()) {
      if (edge.from !== nodeId) continue;
      if (edgeType !== undefined && edge.type !== edgeType) continue;
      if (where) {
        let ok = true;
        for (const key of Object.keys(where)) {
          if (edge.properties[key] !== where[key]) ok = false;
        }
        if (!ok) continue;
      }
      next.add(edge.to);
    }
  }
  return Array.from(next);
}

function ids(nodes) {
  return nodes.map((n) => n.id).sort();
}

test('match by label returns all nodes with that label', () => {
  const db = buildSampleDb();
  const result = db.match({ label: 'Person' }).run();
  assert.deepStrictEqual(ids(result), ids(bruteForceMatchLabel(db, 'Person')));
});

test('match with where filters properties, works without index', () => {
  const db = buildSampleDb();
  const result = db.match({ label: 'Person', where: { age: 25 } }).run();
  assert.deepStrictEqual(ids(result), ['bob']);
});

test('match with where uses index and matches brute force', () => {
  const db = buildSampleDb();
  db.createIndex('Person', 'age');
  const result = db.match({ label: 'Person', where: { age: 40 } }).run();
  assert.deepStrictEqual(ids(result), ids(bruteForceMatchLabel(db, 'Person', { age: 40 })));
});

test('single hop out traversal matches brute force', () => {
  const db = buildSampleDb();
  const result = db.match({ label: 'Person', where: { name: 'Alice' } }).out('KNOWS').run();
  const expected = bruteForceOut(db, ['alice'], 'KNOWS');
  assert.deepStrictEqual(ids(result), expected.sort());
});

test('multi hop traversal with cycle terminates and does not double-count', () => {
  const db = buildSampleDb();
  const result = db
    .match({ label: 'Person', where: { name: 'Alice' } })
    .out('KNOWS')
    .out('KNOWS')
    .out('KNOWS')
    .run();
  const ids1 = ids(result);
  assert.strictEqual(new Set(ids1).size, ids1.length);
  const step1 = bruteForceOut(db, ['alice'], 'KNOWS');
  const step2 = bruteForceOut(db, step1, 'KNOWS');
  const step3 = bruteForceOut(db, step2, 'KNOWS');
  assert.deepStrictEqual(ids1, Array.from(new Set(step3)).sort());
});

test('in traversal follows incoming edges', () => {
  const db = buildSampleDb();
  const result = db.match({ label: 'Person', where: { name: 'Bob' } }).in('KNOWS').run();
  assert.deepStrictEqual(ids(result), ['alice']);
});

test('where step filters after traversal', () => {
  const db = buildSampleDb();
  const result = db
    .match({ label: 'Person' })
    .out('KNOWS')
    .where((node) => node.properties.age > 30)
    .run();
  assert.deepStrictEqual(ids(result), ['carol']);
});

test('limit truncates results', () => {
  const db = buildSampleDb();
  const result = db.match({ label: 'Person' }).limit(2).run();
  assert.strictEqual(result.length, 2);
});

test('return projects each row', () => {
  const db = buildSampleDb();
  const result = db
    .match({ label: 'Person', where: { name: 'Alice' } })
    .return((node) => node.properties.name)
    .run();
  assert.deepStrictEqual(result, ['Alice']);
});

test('empty graph match returns no rows', () => {
  const db = new GraphDB();
  assert.deepStrictEqual(db.match({ label: 'Person' }).run(), []);
});

test('no-match where returns empty', () => {
  const db = buildSampleDb();
  const result = db.match({ label: 'Person', where: { age: 999 } }).run();
  assert.deepStrictEqual(result, []);
});

test('planner picks index lookup when index exists', () => {
  const db = buildSampleDb();
  db.createIndex('Person', 'age');
  const explanation = db.match({ label: 'Person', where: { age: 30 } }).explain();
  assert.strictEqual(explanation.plan[0].type, 'indexLookup');
});

test('planner falls back to label scan without a matching index', () => {
  const db = buildSampleDb();
  const explanation = db.match({ label: 'Person', where: { age: 30 } }).explain();
  assert.strictEqual(explanation.plan[0].type, 'labelScan');
});

test('planner falls back to full scan without a label', () => {
  const db = buildSampleDb();
  const explanation = db.match({ where: { age: 30 } }).explain();
  assert.strictEqual(explanation.plan[0].type, 'fullScan');
});

test('planner chooses the smaller-cardinality index among candidates', () => {
  const db = buildSampleDb();
  db.createIndex('Person', 'age');
  db.createIndex('Person', 'name');
  const explanation = db.match({ label: 'Person', where: { age: 30, name: 'Alice' } }).explain();
  assert.strictEqual(explanation.plan[0].type, 'indexLookup');
  assert.strictEqual(explanation.actualRowsScanned, 1);
});

test('explain reports larger actual scan after dropping an index', () => {
  const db = buildSampleDb();
  db.createIndex('Person', 'age');
  const withIndex = db.match({ label: 'Person', where: { age: 30 } }).explain();
  db.dropIndex('Person', 'age');
  const withoutIndex = db.match({ label: 'Person', where: { age: 30 } }).explain();
  assert.ok(withoutIndex.actualRowsScanned > withIndex.actualRowsScanned);
});

test('shortestPath finds a direct path', () => {
  const db = buildSampleDb();
  const result = db.shortestPath('alice', 'bob');
  assert.deepStrictEqual(result.path, ['alice', 'bob']);
  assert.strictEqual(result.length, 1);
});

test('shortestPath finds a multi-hop path respecting direction', () => {
  const db = buildSampleDb();
  const result = db.shortestPath('alice', 'acme');
  assert.deepStrictEqual(result.path, ['alice', 'bob', 'carol', 'acme']);
  assert.strictEqual(result.length, 3);
});

test('shortestPath returns null when unreachable', () => {
  const db = buildSampleDb();
  db.addNode({ id: 'isolated', labels: ['Person'] });
  assert.strictEqual(db.shortestPath('alice', 'isolated'), null);
});

test('shortestPath handles same node', () => {
  const db = buildSampleDb();
  const result = db.shortestPath('alice', 'alice');
  assert.deepStrictEqual(result, { path: ['alice'], length: 0 });
});

test('shortestPath respects edgeTypes filter', () => {
  const db = buildSampleDb();
  const result = db.shortestPath('alice', 'acme', { edgeTypes: ['KNOWS'] });
  assert.strictEqual(result, null);
});

test('shortestPath respects maxDepth', () => {
  const db = buildSampleDb();
  const result = db.shortestPath('alice', 'acme', { maxDepth: 1 });
  assert.strictEqual(result, null);
});

test('neighbors depth 1 out matches adjacency', () => {
  const db = buildSampleDb();
  const result = db.neighbors('bob', { direction: 'out', depth: 1 });
  assert.deepStrictEqual(ids(result), ['carol', 'dave']);
});

test('neighbors multi-depth does not double-count and terminates with cycles', () => {
  const db = buildSampleDb();
  const result = db.neighbors('alice', { direction: 'out', edgeType: 'KNOWS', depth: 5 });
  const idsResult = ids(result);
  assert.strictEqual(new Set(idsResult).size, idsResult.length);
  assert.deepStrictEqual(idsResult, ['bob', 'carol', 'dave']);
});

test('neighbors both direction includes in and out edges', () => {
  const db = buildSampleDb();
  const result = db.neighbors('alice', { direction: 'both', depth: 1 });
  assert.deepStrictEqual(ids(result), ['bob', 'carol', 'nyc']);
});

test('transaction rollback restores query results and index state', () => {
  const db = buildSampleDb();
  db.createIndex('Person', 'age');
  const before = ids(db.match({ label: 'Person' }).run());
  const beforeExplain = db.match({ label: 'Person', where: { age: 30 } }).explain();

  assert.throws(() => {
    db.transaction(() => {
      db.addNode({ id: 'eve', labels: ['Person'], properties: { age: 30 } });
      db.updateNode('bob', { age: 99 });
      throw new Error('rollback me');
    });
  }, /rollback me/);

  const after = ids(db.match({ label: 'Person' }).run());
  assert.deepStrictEqual(after, before);
  const afterExplain = db.match({ label: 'Person', where: { age: 30 } }).explain();
  assert.deepStrictEqual(afterExplain.plan, beforeExplain.plan);
  assert.deepStrictEqual(
    ids(db.match({ label: 'Person', where: { age: 30 } }).run()),
    ids(db.match({ label: 'Person', where: { age: 30 } }).run())
  );
});

test('randomized graph: match+traversal results match brute force reference', () => {
  const db = new GraphDB();
  const labels = ['Person', 'Company'];
  const nodeIds = [];
  for (let i = 0; i < 60; i += 1) {
    const id = `x${i}`;
    const label = labels[i % 2];
    db.addNode({ id, labels: [label], properties: { bucket: i % 5 } });
    nodeIds.push(id);
  }
  for (let i = 0; i < 200; i += 1) {
    const from = nodeIds[Math.floor(Math.random() * nodeIds.length)];
    const to = nodeIds[Math.floor(Math.random() * nodeIds.length)];
    db.addEdge({ type: 'REL', from, to });
  }

  const actual = ids(db.match({ label: 'Person', where: { bucket: 2 } }).out('REL').run());
  const startNodes = bruteForceMatchLabel(db, 'Person', { bucket: 2 }).map((n) => n.id);
  const expected = Array.from(new Set(bruteForceOut(db, startNodes, 'REL'))).sort();
  assert.deepStrictEqual(actual, expected);
});

function bfsReferenceDistance(db, fromId, toId) {
  if (fromId === toId) return 0;
  const visited = new Set([fromId]);
  let frontier = [fromId];
  let depth = 0;
  while (frontier.length > 0) {
    depth += 1;
    const next = [];
    for (const nodeId of frontier) {
      for (const edgeId of db.graph.outEdgeIds(nodeId)) {
        const edge = db.graph.getEdge(edgeId);
        const to = edge.to;
        if (visited.has(to)) continue;
        if (to === toId) return depth;
        visited.add(to);
        next.push(to);
      }
    }
    frontier = next;
  }
  return -1;
}

test('shortestPath on a larger random graph has no repeated nodes and matches BFS distance', () => {
  const db = new GraphDB();
  const n = 80;
  for (let i = 0; i < n; i += 1) db.addNode({ id: `r${i}` });
  for (let i = 0; i < 300; i += 1) {
    const from = `r${Math.floor(Math.random() * n)}`;
    const to = `r${Math.floor(Math.random() * n)}`;
    if (from !== to) db.addEdge({ type: 'E', from, to });
  }
  for (let trial = 0; trial < 30; trial += 1) {
    const from = `r${Math.floor(Math.random() * n)}`;
    const to = `r${Math.floor(Math.random() * n)}`;
    const result = db.shortestPath(from, to);
    const expectedDistance = bfsReferenceDistance(db, from, to);
    if (expectedDistance === -1) {
      assert.strictEqual(result, null, `expected unreachable for ${from}->${to}`);
    } else {
      assert.ok(result, `expected a path for ${from}->${to}`);
      assert.strictEqual(result.length, expectedDistance);
      assert.strictEqual(new Set(result.path).size, result.path.length);
      assert.strictEqual(result.path[0], from);
      assert.strictEqual(result.path[result.path.length - 1], to);
    }
  }
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
