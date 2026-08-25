'use strict';

const { SocialGraph } = require('./graph.js');
const {
  degreeCentrality,
  betweennessCentrality,
  labelPropagation,
  communitiesFromLabels,
  recommendFriends,
} = require('./analysis.js');

function buildSampleGraph() {
  const graph = new SocialGraph();

  const clusterA = ['alice', 'bob', 'carol', 'dave', 'erin'];
  const clusterB = ['frank', 'grace', 'heidi', 'ivan', 'judy'];
  const clusterC = ['kate', 'leo', 'mallory', 'nina', 'oscar'];

  for (const cluster of [clusterA, clusterB, clusterC]) {
    for (let i = 0; i < cluster.length; i += 1) {
      for (let j = i + 1; j < cluster.length; j += 1) {
        if (Math.random() < 0.7 || j === i + 1) {
          graph.addEdge(cluster[i], cluster[j]);
        }
      }
    }
  }

  graph.addEdge('bob', 'frank');
  graph.addEdge('grace', 'kate');
  graph.addEdge('erin', 'leo');

  return graph;
}

function main() {
  const graph = buildSampleGraph();

  console.log(`graph built: ${graph.size()} nodes, ${graph.edgeCount()} edges\n`);

  const degree = degreeCentrality(graph);
  console.log('degree centrality (sample):');
  for (const node of ['alice', 'bob', 'frank', 'kate']) {
    console.log(`  ${node}: ${degree.get(node).toFixed(3)}`);
  }

  console.log('\nbetweenness centrality (sample):');
  const betweenness = betweennessCentrality(graph);
  for (const node of ['alice', 'bob', 'frank', 'kate']) {
    console.log(`  ${node}: ${betweenness.get(node).toFixed(3)}`);
  }

  console.log('\ncommunity detection (label propagation):');
  const labels = labelPropagation(graph, { seed: 42 });
  const communities = communitiesFromLabels(labels);
  communities
    .sort((a, b) => b.length - a.length)
    .forEach((members, index) => {
      console.log(`  community ${index + 1}: ${members.sort().join(', ')}`);
    });

  console.log('\nfriend recommendations:');
  for (const user of ['alice', 'grace']) {
    const recs = recommendFriends(graph, user, { topN: 3 });
    console.log(`  for ${user}:`);
    if (recs.length === 0) {
      console.log('    (no recommendations)');
    }
    for (const rec of recs) {
      console.log(`    ${rec.id} (${rec.mutualFriends} mutual friends)`);
    }
  }
}

main();
