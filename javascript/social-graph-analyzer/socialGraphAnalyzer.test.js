'use strict';

const assert = require('assert');
const { SocialGraph } = require('./graph.js');
const {
  degreeCentrality,
  betweennessCentrality,
  labelPropagation,
  communitiesFromLabels,
  recommendFriends,
} = require('./analysis.js');

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

test('degree centrality on a star graph ranks the center highest', () => {
  const graph = new SocialGraph();
  graph.addEdge('center', 'leaf1');
  graph.addEdge('center', 'leaf2');
  graph.addEdge('center', 'leaf3');

  const centrality = degreeCentrality(graph);
  assert.strictEqual(centrality.get('center'), 1);
  assert.strictEqual(centrality.get('leaf1'), 1 / 3);
  assert.strictEqual(centrality.get('leaf2'), 1 / 3);
  assert.strictEqual(centrality.get('leaf3'), 1 / 3);
});

test('degree centrality unnormalized returns raw degree', () => {
  const graph = new SocialGraph();
  graph.addEdge('a', 'b');
  graph.addEdge('a', 'c');
  const centrality = degreeCentrality(graph, { normalized: false });
  assert.strictEqual(centrality.get('a'), 2);
  assert.strictEqual(centrality.get('b'), 1);
});

test('betweenness centrality on a 3-node path is exactly known', () => {
  const graph = new SocialGraph();
  graph.addEdge('n1', 'n2');
  graph.addEdge('n2', 'n3');

  const centrality = betweennessCentrality(graph);
  assert.strictEqual(centrality.get('n2'), 1);
  assert.strictEqual(centrality.get('n1'), 0);
  assert.strictEqual(centrality.get('n3'), 0);
});

test('betweenness centrality on a 5-node path matches known values', () => {
  const graph = new SocialGraph();
  graph.addEdge('a', 'b');
  graph.addEdge('b', 'c');
  graph.addEdge('c', 'd');
  graph.addEdge('d', 'e');

  const centrality = betweennessCentrality(graph, { normalized: false });
  assert.strictEqual(centrality.get('a'), 0);
  assert.strictEqual(centrality.get('e'), 0);
  assert.strictEqual(centrality.get('b'), 3);
  assert.strictEqual(centrality.get('d'), 3);
  assert.strictEqual(centrality.get('c'), 4);
});

test('betweenness centrality is zero for every node in a fully connected clique', () => {
  const graph = new SocialGraph();
  const members = ['p', 'q', 'r', 's'];
  for (let i = 0; i < members.length; i += 1) {
    for (let j = i + 1; j < members.length; j += 1) {
      graph.addEdge(members[i], members[j]);
    }
  }
  const centrality = betweennessCentrality(graph);
  for (const member of members) {
    assert.strictEqual(centrality.get(member), 0);
  }
});

test('label propagation converges and separates two disconnected clusters', () => {
  const graph = new SocialGraph();
  graph.addEdge('a', 'b');
  graph.addEdge('b', 'c');
  graph.addEdge('a', 'c');
  graph.addEdge('x', 'y');
  graph.addEdge('y', 'z');
  graph.addEdge('x', 'z');

  const labels = labelPropagation(graph, { seed: 7 });
  assert.strictEqual(labels.get('a'), labels.get('b'));
  assert.strictEqual(labels.get('b'), labels.get('c'));
  assert.strictEqual(labels.get('x'), labels.get('y'));
  assert.strictEqual(labels.get('y'), labels.get('z'));
  assert.notStrictEqual(labels.get('a'), labels.get('x'));

  const communities = communitiesFromLabels(labels);
  assert.strictEqual(communities.length, 2);
  const sizes = communities.map((group) => group.length).sort();
  assert.deepStrictEqual(sizes, [3, 3]);
});

test('label propagation keeps three obvious clusters distinct', () => {
  const graph = new SocialGraph();
  const clusters = [
    ['a1', 'a2', 'a3', 'a4'],
    ['b1', 'b2', 'b3', 'b4'],
    ['c1', 'c2', 'c3', 'c4'],
  ];
  for (const cluster of clusters) {
    for (let i = 0; i < cluster.length; i += 1) {
      for (let j = i + 1; j < cluster.length; j += 1) {
        graph.addEdge(cluster[i], cluster[j]);
      }
    }
  }

  const labels = labelPropagation(graph, { seed: 3 });
  const communities = communitiesFromLabels(labels);
  assert.strictEqual(communities.length, 3);
  for (const cluster of clusters) {
    const clusterLabels = new Set(cluster.map((member) => labels.get(member)));
    assert.strictEqual(clusterLabels.size, 1);
  }
});

test('friend recommendations rank by mutual friend count', () => {
  const graph = new SocialGraph();
  graph.addEdge('alice', 'bob');
  graph.addEdge('alice', 'carol');
  graph.addEdge('bob', 'dave');
  graph.addEdge('carol', 'dave');
  graph.addEdge('carol', 'eve');

  const recs = recommendFriends(graph, 'alice', { topN: 5 });
  const daveEntry = recs.find((rec) => rec.id === 'dave');
  const eveEntry = recs.find((rec) => rec.id === 'eve');

  assert.ok(daveEntry, 'dave should be recommended');
  assert.strictEqual(daveEntry.mutualFriends, 2);
  assert.ok(eveEntry, 'eve should be recommended');
  assert.strictEqual(eveEntry.mutualFriends, 1);
  assert.ok(recs.indexOf(daveEntry) < recs.indexOf(eveEntry));
});

test('friend recommendations exclude already-connected pairs and the user', () => {
  const graph = new SocialGraph();
  graph.addEdge('alice', 'bob');
  graph.addEdge('alice', 'carol');
  graph.addEdge('bob', 'carol');
  graph.addEdge('bob', 'dave');
  graph.addEdge('carol', 'dave');

  const recs = recommendFriends(graph, 'alice', { topN: 5 });
  const ids = recs.map((rec) => rec.id);
  assert.ok(!ids.includes('bob'));
  assert.ok(!ids.includes('carol'));
  assert.ok(!ids.includes('alice'));
  assert.ok(ids.includes('dave'));
});

test('friend recommendations respect topN limit', () => {
  const graph = new SocialGraph();
  graph.addEdge('hub', 'friend1');
  graph.addEdge('hub', 'friend2');
  for (let i = 1; i <= 6; i += 1) {
    graph.addEdge(`friend${i}`, `candidate${i}`);
    graph.addEdge('hub', `friend${i}`);
  }
  const recs = recommendFriends(graph, 'hub', { topN: 2 });
  assert.strictEqual(recs.length, 2);
});

test('recommendFriends throws for an unknown user', () => {
  const graph = new SocialGraph();
  graph.addEdge('a', 'b');
  assert.throws(() => recommendFriends(graph, 'ghost'), RangeError);
});

test('graph rejects self-loops and duplicate nodes are idempotent', () => {
  const graph = new SocialGraph();
  assert.throws(() => graph.addEdge('a', 'a'), RangeError);
  graph.addNode('a');
  graph.addNode('a');
  assert.strictEqual(graph.size(), 1);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
