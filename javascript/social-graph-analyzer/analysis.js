'use strict';

function degreeCentrality(graph, { normalized = true } = {}) {
  const nodes = graph.nodes();
  const n = nodes.length;
  const result = new Map();
  for (const node of nodes) {
    const degree = graph.degree(node);
    result.set(node, normalized && n > 1 ? degree / (n - 1) : degree);
  }
  return result;
}

function betweennessCentrality(graph, { normalized = true } = {}) {
  const nodes = graph.nodes();
  const centrality = new Map();
  for (const node of nodes) {
    centrality.set(node, 0);
  }

  for (const source of nodes) {
    const stack = [];
    const predecessors = new Map();
    const sigma = new Map();
    const distance = new Map();
    for (const node of nodes) {
      predecessors.set(node, []);
      sigma.set(node, 0);
      distance.set(node, -1);
    }
    sigma.set(source, 1);
    distance.set(source, 0);

    const queue = [source];
    let head = 0;
    while (head < queue.length) {
      const current = queue[head];
      head += 1;
      stack.push(current);
      for (const neighbor of graph.neighbors(current)) {
        if (distance.get(neighbor) < 0) {
          distance.set(neighbor, distance.get(current) + 1);
          queue.push(neighbor);
        }
        if (distance.get(neighbor) === distance.get(current) + 1) {
          sigma.set(neighbor, sigma.get(neighbor) + sigma.get(current));
          predecessors.get(neighbor).push(current);
        }
      }
    }

    const delta = new Map();
    for (const node of nodes) {
      delta.set(node, 0);
    }
    while (stack.length > 0) {
      const w = stack.pop();
      for (const v of predecessors.get(w)) {
        const contribution = (sigma.get(v) / sigma.get(w)) * (1 + delta.get(w));
        delta.set(v, delta.get(v) + contribution);
      }
      if (w !== source) {
        centrality.set(w, centrality.get(w) + delta.get(w));
      }
    }
  }

  const n = nodes.length;
  for (const node of nodes) {
    centrality.set(node, centrality.get(node) / 2);
  }

  if (normalized && n > 2) {
    const scale = (n - 1) * (n - 2) / 2;
    for (const node of nodes) {
      centrality.set(node, centrality.get(node) / scale);
    }
  }

  return centrality;
}

function labelPropagation(graph, { maxIterations = 100, seed } = {}) {
  const labels = new Map();
  for (const node of graph.nodes()) {
    labels.set(node, node);
  }

  const random = createRandom(seed);
  const nodes = graph.nodes();

  for (let iteration = 0; iteration < maxIterations; iteration += 1) {
    const order = shuffle(nodes.slice(), random);
    let changed = false;

    for (const node of order) {
      const neighbors = graph.neighbors(node);
      if (neighbors.length === 0) {
        continue;
      }
      const counts = new Map();
      for (const neighbor of neighbors) {
        const label = labels.get(neighbor);
        counts.set(label, (counts.get(label) || 0) + 1);
      }

      let bestLabels = [];
      let bestCount = -1;
      for (const [label, count] of counts) {
        if (count > bestCount) {
          bestCount = count;
          bestLabels = [label];
        } else if (count === bestCount) {
          bestLabels.push(label);
        }
      }
      bestLabels.sort();
      const chosen = bestLabels[Math.floor(random() * bestLabels.length)];

      if (chosen !== labels.get(node)) {
        labels.set(node, chosen);
        changed = true;
      }
    }

    if (!changed) {
      break;
    }
  }

  return labels;
}

function communitiesFromLabels(labels) {
  const groups = new Map();
  for (const [node, label] of labels) {
    if (!groups.has(label)) {
      groups.set(label, []);
    }
    groups.get(label).push(node);
  }
  return Array.from(groups.values());
}

function recommendFriends(graph, userId, { topN = 5 } = {}) {
  if (!graph.hasNode(userId)) {
    throw new RangeError(`unknown node: ${userId}`);
  }

  const directFriends = new Set(graph.neighbors(userId));
  const mutualCounts = new Map();

  for (const friend of directFriends) {
    for (const candidate of graph.neighbors(friend)) {
      if (candidate === userId || directFriends.has(candidate)) {
        continue;
      }
      mutualCounts.set(candidate, (mutualCounts.get(candidate) || 0) + 1);
    }
  }

  const recommendations = Array.from(mutualCounts.entries()).map(([id, mutualFriends]) => ({
    id,
    mutualFriends,
  }));

  recommendations.sort((a, b) => {
    if (b.mutualFriends !== a.mutualFriends) {
      return b.mutualFriends - a.mutualFriends;
    }
    return a.id < b.id ? -1 : a.id > b.id ? 1 : 0;
  });

  return recommendations.slice(0, topN);
}

function createRandom(seed) {
  if (seed === undefined) {
    return Math.random;
  }
  let state = seed % 2147483647;
  if (state <= 0) {
    state += 2147483646;
  }
  return function next() {
    state = (state * 16807) % 2147483647;
    return (state - 1) / 2147483646;
  };
}

function shuffle(array, random) {
  for (let i = array.length - 1; i > 0; i -= 1) {
    const j = Math.floor(random() * (i + 1));
    const tmp = array[i];
    array[i] = array[j];
    array[j] = tmp;
  }
  return array;
}

module.exports = {
  degreeCentrality,
  betweennessCentrality,
  labelPropagation,
  communitiesFromLabels,
  recommendFriends,
};
