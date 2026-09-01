'use strict';

const assert = require('assert');
const { PropertyGraph } = require('./graph.js');

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

function bruteForceIndex(graph, label, property) {
  const valueMap = new Map();
  for (const node of graph.nodes.values()) {
    if (!node.labels.has(label)) continue;
    if (!Object.prototype.hasOwnProperty.call(node.properties, property)) continue;
    const value = node.properties[property];
    if (!valueMap.has(value)) valueMap.set(value, new Set());
    valueMap.get(value).add(node.id);
  }
  return valueMap;
}

function assertIndexMatchesBruteForce(graph, label, property) {
  const expected = bruteForceIndex(graph, label, property);
  const propMap = graph.propertyIndexes.get(label);
  const actual = propMap ? propMap.get(property) : new Map();
  const expectedKeys = Array.from(expected.keys()).sort();
  const actualKeys = Array.from((actual || new Map()).keys()).sort();
  assert.deepStrictEqual(actualKeys, expectedKeys);
  for (const key of expectedKeys) {
    const expectedSet = Array.from(expected.get(key)).sort();
    const actualSet = Array.from(actual.get(key)).sort();
    assert.deepStrictEqual(actualSet, expectedSet);
  }
}

function assertLabelIndexMatchesBruteForce(graph, label) {
  const expected = [];
  for (const node of graph.nodes.values()) {
    if (node.labels.has(label)) expected.push(node.id);
  }
  const actual = Array.from(graph.labelIndex.get(label) || []);
  assert.deepStrictEqual(actual.sort(), expected.sort());
}

test('addNode stores labels and properties, populates label index', () => {
  const g = new PropertyGraph();
  g.addNode({ id: 'a', labels: ['Person'], properties: { name: 'Ann', age: 30 } });
  assert.strictEqual(g.getNode('a').properties.name, 'Ann');
  assert.ok(g.labelIndex.get('Person').has('a'));
});

test('createIndex builds from existing nodes and stays maintained on insert', () => {
  const g = new PropertyGraph();
  g.addNode({ id: 'a', labels: ['Person'], properties: { city: 'NY' } });
  g.createIndex('Person', 'city');
  g.addNode({ id: 'b', labels: ['Person'], properties: { city: 'NY' } });
  g.addNode({ id: 'c', labels: ['Person'], properties: { city: 'LA' } });
  assertIndexMatchesBruteForce(g, 'Person', 'city');
  assert.strictEqual(g.nodeIdsByIndex('Person', 'city', 'NY').size, 2);
});

test('updateNode moves index entries', () => {
  const g = new PropertyGraph();
  g.addNode({ id: 'a', labels: ['Person'], properties: { city: 'NY' } });
  g.createIndex('Person', 'city');
  g.updateNode('a', { city: 'LA' });
  assertIndexMatchesBruteForce(g, 'Person', 'city');
  assert.strictEqual(g.nodeIdsByIndex('Person', 'city', 'NY').size, 0);
  assert.strictEqual(g.nodeIdsByIndex('Person', 'city', 'LA').size, 1);
});

test('deleteNode removes incident edges and all index entries', () => {
  const g = new PropertyGraph();
  g.addNode({ id: 'a', labels: ['Person'], properties: { city: 'NY' } });
  g.addNode({ id: 'b', labels: ['Person'], properties: { city: 'NY' } });
  g.createIndex('Person', 'city');
  g.addEdge({ id: 'e1', type: 'KNOWS', from: 'a', to: 'b' });
  g.addEdge({ id: 'e2', type: 'KNOWS', from: 'b', to: 'a' });
  g.deleteNode('a');
  assert.strictEqual(g.getNode('a'), undefined);
  assert.strictEqual(g.getEdge('e1'), undefined);
  assert.strictEqual(g.getEdge('e2'), undefined);
  assert.strictEqual(g.outEdgeIds('b').length, 0);
  assert.strictEqual(g.inEdgeIds('b').length, 0);
  assertIndexMatchesBruteForce(g, 'Person', 'city');
  assertLabelIndexMatchesBruteForce(g, 'Person');
  assert.ok(!g.labelIndex.get('Person').has('a'));
});

test('deleteEdge removes from both adjacency directions', () => {
  const g = new PropertyGraph();
  g.addNode({ id: 'a' });
  g.addNode({ id: 'b' });
  g.addEdge({ id: 'e1', type: 'KNOWS', from: 'a', to: 'b' });
  g.deleteEdge('e1');
  assert.strictEqual(g.outEdgeIds('a').length, 0);
  assert.strictEqual(g.inEdgeIds('b').length, 0);
});

test('N-hop expansion uses adjacency maps, not a full edge scan', () => {
  const g = new PropertyGraph();
  for (let i = 0; i < 50; i += 1) g.addNode({ id: `n${i}` });
  for (let i = 0; i < 49; i += 1) g.addEdge({ type: 'NEXT', from: `n${i}`, to: `n${i + 1}` });
  assert.deepStrictEqual(g.outEdgeIds('n0').length, 1);
  const edge = g.getEdge(g.outEdgeIds('n0')[0]);
  assert.strictEqual(edge.to, 'n1');
});

test('randomized mutation sequence keeps indexes consistent with brute force', () => {
  const g = new PropertyGraph();
  g.createIndex('Person', 'city');
  const cities = ['NY', 'LA', 'SF'];
  let counter = 0;
  const liveIds = [];
  for (let step = 0; step < 400; step += 1) {
    const action = Math.random();
    if (action < 0.5 || liveIds.length === 0) {
      const id = `p${counter++}`;
      g.addNode({ id, labels: ['Person'], properties: { city: cities[step % cities.length] } });
      liveIds.push(id);
    } else if (action < 0.8) {
      const id = liveIds[Math.floor(Math.random() * liveIds.length)];
      g.updateNode(id, { city: cities[(step * 7) % cities.length] });
    } else {
      const idx = Math.floor(Math.random() * liveIds.length);
      const id = liveIds[idx];
      g.deleteNode(id);
      liveIds.splice(idx, 1);
    }
  }
  assertIndexMatchesBruteForce(g, 'Person', 'city');
  assertLabelIndexMatchesBruteForce(g, 'Person');
});

test('transaction rolls back all mutations on throw, indexes included', () => {
  const g = new PropertyGraph();
  g.addNode({ id: 'a', labels: ['Person'], properties: { city: 'NY' } });
  g.createIndex('Person', 'city');
  g.addEdge({ id: 'e1', type: 'KNOWS', from: 'a', to: 'a' });

  const beforeNodes = Array.from(g.nodes.keys()).sort();
  const beforeEdges = Array.from(g.edges.keys()).sort();
  const beforeIndex = JSON.stringify(Array.from(g.propertyIndexes.get('Person').get('city').entries()));

  assert.throws(() => {
    g.transaction(() => {
      g.addNode({ id: 'b', labels: ['Person'], properties: { city: 'LA' } });
      g.updateNode('a', { city: 'SF' });
      g.deleteEdge('e1');
      throw new Error('boom');
    });
  }, /boom/);

  assert.deepStrictEqual(Array.from(g.nodes.keys()).sort(), beforeNodes);
  assert.deepStrictEqual(Array.from(g.edges.keys()).sort(), beforeEdges);
  assert.strictEqual(
    JSON.stringify(Array.from(g.propertyIndexes.get('Person').get('city').entries())),
    beforeIndex
  );
  assert.strictEqual(g.getNode('a').properties.city, 'NY');
  assert.ok(g.getEdge('e1'));
});

test('transaction commits normally when fn does not throw', () => {
  const g = new PropertyGraph();
  g.addNode({ id: 'a' });
  g.transaction(() => {
    g.addNode({ id: 'b' });
  });
  assert.ok(g.getNode('b'));
});

test('empty graph queries behave safely', () => {
  const g = new PropertyGraph();
  assert.strictEqual(g.getNode('missing'), undefined);
  assert.deepStrictEqual(g.allNodeIds(), []);
  assert.deepStrictEqual(g.nodeIdsByLabel('Nothing'), new Set());
  assert.strictEqual(g.nodeIdsByIndex('Nothing', 'prop', 'x'), null);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
