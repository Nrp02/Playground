'use strict';

const { PropertyGraph } = require('./graph.js');

function matchesPredicateMap(properties, where, extra) {
  for (const key of Object.keys(where)) {
    const cond = where[key];
    const value = properties[key];
    if (typeof cond === 'function') {
      if (!cond(value, extra)) return false;
    } else if (value !== cond) {
      return false;
    }
  }
  return true;
}

function pickKeys(obj, keys) {
  const result = {};
  for (const key of keys) result[key] = obj[key];
  return result;
}

class QueryBuilder {
  constructor(graph) {
    this.graph = graph;
    this._matchSpec = null;
    this._steps = [];
    this._limitCount = null;
    this._returnFn = null;
  }

  match({ label, where } = {}) {
    this._matchSpec = { label: label !== undefined ? label : null, where: where || null };
    return this;
  }

  out(edgeType, { where } = {}) {
    this._steps.push({ kind: 'traverse', direction: 'out', edgeType, where: where || null });
    return this;
  }

  in(edgeType, { where } = {}) {
    this._steps.push({ kind: 'traverse', direction: 'in', edgeType, where: where || null });
    return this;
  }

  where(fn) {
    this._steps.push({ kind: 'filter', fn });
    return this;
  }

  limit(n) {
    this._limitCount = n;
    return this;
  }

  return(fn) {
    this._returnFn = fn;
    return this;
  }

  _planMatch() {
    const graph = this.graph;
    const label = this._matchSpec ? this._matchSpec.label : null;
    const where = this._matchSpec ? this._matchSpec.where : null;
    const totalNodes = graph.nodes.size;

    if (label && where) {
      let best = null;
      for (const prop of Object.keys(where)) {
        if (typeof where[prop] === 'function') continue;
        if (!graph.hasIndex(label, prop)) continue;
        const ids = graph.nodeIdsByIndex(label, prop, where[prop]);
        const size = ids ? ids.size : 0;
        if (best === null || size < best.size) best = { prop, ids: ids || new Set(), size };
      }
      if (best) {
        const remainingKeys = Object.keys(where).filter((k) => k !== best.prop);
        const steps = [{ type: 'indexLookup', label, property: best.prop, value: where[best.prop], estimated: best.size }];
        if (remainingKeys.length > 0) steps.push({ type: 'filter', keys: remainingKeys });
        return {
          steps,
          candidateIds: Array.from(best.ids),
          requireLabel: null,
          filterWhere: remainingKeys.length > 0 ? pickKeys(where, remainingKeys) : null,
          estimated: best.size,
        };
      }
    }

    if (label && graph.labelIndex.has(label)) {
      const ids = graph.labelIndex.get(label);
      const steps = [{ type: 'labelScan', label, estimated: ids.size }];
      if (where) steps.push({ type: 'filter', keys: Object.keys(where) });
      return {
        steps,
        candidateIds: Array.from(ids),
        requireLabel: null,
        filterWhere: where,
        estimated: ids.size,
      };
    }

    const steps = [{ type: 'fullScan', estimated: totalNodes }];
    if (label || where) steps.push({ type: 'filter', keys: [...(label ? ['__label'] : []), ...(where ? Object.keys(where) : [])] });
    return {
      steps,
      candidateIds: graph.allNodeIds(),
      requireLabel: label,
      filterWhere: where,
      estimated: totalNodes,
    };
  }

  _matchCandidates(plan) {
    const graph = this.graph;
    let candidates = plan.candidateIds.map((id) => graph.getNode(id)).filter(Boolean);
    if (plan.requireLabel) {
      candidates = candidates.filter((node) => node.labels.has(plan.requireLabel));
    }
    if (plan.filterWhere) {
      candidates = candidates.filter((node) => matchesPredicateMap(node.properties, plan.filterWhere, node));
    }
    return candidates;
  }

  _runTraversal(startNodes) {
    const graph = this.graph;
    let current = startNodes.map((node) => node.id);
    for (const step of this._steps) {
      if (step.kind === 'filter') {
        current = current.filter((id) => {
          const node = graph.getNode(id);
          return node ? step.fn(node) : false;
        });
        continue;
      }
      const next = [];
      for (const nodeId of current) {
        const edgeIds = step.direction === 'out' ? graph.outEdgeIds(nodeId, step.edgeType) : graph.inEdgeIds(nodeId, step.edgeType);
        for (const edgeId of edgeIds) {
          const edge = graph.getEdge(edgeId);
          if (!edge) continue;
          if (step.where && !matchesPredicateMap(edge.properties, step.where, edge)) continue;
          const neighborId = step.direction === 'out' ? edge.to : edge.from;
          if (!graph.nodes.has(neighborId)) continue;
          next.push(neighborId);
        }
      }
      current = next;
    }
    const seen = new Set();
    const result = [];
    for (const id of current) {
      if (seen.has(id)) continue;
      seen.add(id);
      const node = graph.getNode(id);
      if (node) result.push(node);
    }
    return result;
  }

  run() {
    const plan = this._planMatch();
    const candidates = this._matchCandidates(plan);
    let rows = this._runTraversal(candidates);
    if (this._limitCount !== null) rows = rows.slice(0, this._limitCount);
    if (this._returnFn) return rows.map((node) => this._returnFn(node));
    return rows;
  }

  explain() {
    const plan = this._planMatch();
    const candidates = this._matchCandidates(plan);
    const rows = this._runTraversal(candidates);
    return {
      plan: plan.steps,
      estimatedRows: plan.estimated,
      actualRowsScanned: plan.candidateIds.length,
      resultRows: rows.length,
    };
  }
}

function shortestPath(graph, fromId, toId, { edgeTypes = null, maxDepth = Infinity } = {}) {
  if (!graph.nodes.has(fromId) || !graph.nodes.has(toId)) return null;
  if (fromId === toId) return { path: [fromId], length: 0 };

  let visitedForward = new Map([[fromId, [fromId]]]);
  let frontierForward = new Map(visitedForward);
  let visitedBackward = new Map([[toId, [toId]]]);
  let frontierBackward = new Map(visitedBackward);

  let depth = 0;
  while (frontierForward.size > 0 && frontierBackward.size > 0 && depth < maxDepth) {
    depth += 1;
    if (frontierForward.size <= frontierBackward.size) {
      const next = new Map();
      for (const [nodeId, path] of frontierForward) {
        const edgeIds = graph.outEdgeIds(nodeId);
        for (const edgeId of edgeIds) {
          const edge = graph.getEdge(edgeId);
          if (!edge) continue;
          if (edgeTypes && !edgeTypes.includes(edge.type)) continue;
          const neighborId = edge.to;
          if (visitedForward.has(neighborId)) continue;
          const newPath = [...path, neighborId];
          visitedForward.set(neighborId, newPath);
          next.set(neighborId, newPath);
          if (visitedBackward.has(neighborId)) {
            const backPath = visitedBackward.get(neighborId);
            const fullPath = newPath.concat(backPath.slice(1));
            return { path: fullPath, length: fullPath.length - 1 };
          }
        }
      }
      frontierForward = next;
    } else {
      const next = new Map();
      for (const [nodeId, path] of frontierBackward) {
        const edgeIds = graph.inEdgeIds(nodeId);
        for (const edgeId of edgeIds) {
          const edge = graph.getEdge(edgeId);
          if (!edge) continue;
          if (edgeTypes && !edgeTypes.includes(edge.type)) continue;
          const neighborId = edge.from;
          if (visitedBackward.has(neighborId)) continue;
          const newPath = [neighborId, ...path];
          visitedBackward.set(neighborId, newPath);
          next.set(neighborId, newPath);
          if (visitedForward.has(neighborId)) {
            const forwardPath = visitedForward.get(neighborId);
            const fullPath = forwardPath.concat(newPath.slice(1));
            return { path: fullPath, length: fullPath.length - 1 };
          }
        }
      }
      frontierBackward = next;
    }
  }
  return null;
}

function neighbors(graph, id, { direction = 'out', edgeType, depth = 1 } = {}) {
  if (!graph.nodes.has(id)) return [];
  let frontier = new Set([id]);
  const visited = new Set([id]);
  for (let d = 0; d < depth; d += 1) {
    const next = new Set();
    for (const nodeId of frontier) {
      const edgeIds =
        direction === 'out'
          ? graph.outEdgeIds(nodeId, edgeType)
          : direction === 'in'
          ? graph.inEdgeIds(nodeId, edgeType)
          : [...graph.outEdgeIds(nodeId, edgeType), ...graph.inEdgeIds(nodeId, edgeType)];
      for (const edgeId of edgeIds) {
        const edge = graph.getEdge(edgeId);
        if (!edge) continue;
        let neighborId;
        if (direction === 'out') neighborId = edge.to;
        else if (direction === 'in') neighborId = edge.from;
        else neighborId = edge.from === nodeId ? edge.to : edge.from;
        if (visited.has(neighborId)) continue;
        visited.add(neighborId);
        next.add(neighborId);
      }
    }
    frontier = next;
    if (frontier.size === 0) break;
  }
  visited.delete(id);
  const result = [];
  for (const nodeId of visited) {
    const node = graph.getNode(nodeId);
    if (node) result.push(node);
  }
  return result;
}

class GraphDB {
  constructor() {
    this.graph = new PropertyGraph();
  }

  addNode(spec) {
    return this.graph.addNode(spec);
  }

  addEdge(spec) {
    return this.graph.addEdge(spec);
  }

  updateNode(id, properties) {
    return this.graph.updateNode(id, properties);
  }

  updateEdge(id, properties) {
    return this.graph.updateEdge(id, properties);
  }

  deleteNode(id) {
    return this.graph.deleteNode(id);
  }

  deleteEdge(id) {
    return this.graph.deleteEdge(id);
  }

  addLabel(id, label) {
    return this.graph.addLabel(id, label);
  }

  createIndex(label, property) {
    return this.graph.createIndex(label, property);
  }

  dropIndex(label, property) {
    return this.graph.dropIndex(label, property);
  }

  hasIndex(label, property) {
    return this.graph.hasIndex(label, property);
  }

  getNode(id) {
    return this.graph.getNode(id);
  }

  getEdge(id) {
    return this.graph.getEdge(id);
  }

  transaction(fn) {
    return this.graph.transaction(fn);
  }

  match(spec) {
    return new QueryBuilder(this.graph).match(spec);
  }

  shortestPath(fromId, toId, options) {
    return shortestPath(this.graph, fromId, toId, options);
  }

  neighbors(id, options) {
    return neighbors(this.graph, id, options);
  }
}

module.exports = { GraphDB, QueryBuilder, shortestPath, neighbors, matchesPredicateMap };
