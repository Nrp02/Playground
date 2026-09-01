'use strict';

class PropertyGraph {
  constructor() {
    this.nodes = new Map();
    this.edges = new Map();
    this.outAdj = new Map();
    this.inAdj = new Map();
    this.labelIndex = new Map();
    this.propertyIndexes = new Map();
    this._nextNodeId = 1;
    this._nextEdgeId = 1;
  }

  addNode({ id, labels = [], properties = {} } = {}) {
    const nodeId = id !== undefined && id !== null ? id : `n${this._nextNodeId++}`;
    if (this.nodes.has(nodeId)) throw new Error(`node ${nodeId} already exists`);
    const labelSet = new Set(labels);
    const node = { id: nodeId, labels: labelSet, properties: { ...properties } };
    this.nodes.set(nodeId, node);
    this.outAdj.set(nodeId, new Map());
    this.inAdj.set(nodeId, new Map());
    for (const label of labelSet) {
      this._addToLabelIndex(label, nodeId);
      this._indexAllProperties(label, nodeId, node.properties);
    }
    return node;
  }

  _addToLabelIndex(label, nodeId) {
    if (!this.labelIndex.has(label)) this.labelIndex.set(label, new Set());
    this.labelIndex.get(label).add(nodeId);
  }

  _indexAllProperties(label, nodeId, properties) {
    const propMap = this.propertyIndexes.get(label);
    if (!propMap) return;
    for (const prop of propMap.keys()) {
      if (Object.prototype.hasOwnProperty.call(properties, prop)) {
        this._addToPropertyIndex(label, prop, properties[prop], nodeId);
      }
    }
  }

  _addToPropertyIndex(label, prop, value, nodeId) {
    const propMap = this.propertyIndexes.get(label);
    if (!propMap) return;
    const valueMap = propMap.get(prop);
    if (!valueMap) return;
    if (!valueMap.has(value)) valueMap.set(value, new Set());
    valueMap.get(value).add(nodeId);
  }

  _removeFromPropertyIndex(label, prop, value, nodeId) {
    const propMap = this.propertyIndexes.get(label);
    if (!propMap) return;
    const valueMap = propMap.get(prop);
    if (!valueMap) return;
    const set = valueMap.get(value);
    if (!set) return;
    set.delete(nodeId);
    if (set.size === 0) valueMap.delete(value);
  }

  createIndex(label, property) {
    if (!this.propertyIndexes.has(label)) this.propertyIndexes.set(label, new Map());
    const propMap = this.propertyIndexes.get(label);
    if (propMap.has(property)) return;
    const valueMap = new Map();
    propMap.set(property, valueMap);
    const nodeIds = this.labelIndex.get(label);
    if (nodeIds) {
      for (const nodeId of nodeIds) {
        const node = this.nodes.get(nodeId);
        if (node && Object.prototype.hasOwnProperty.call(node.properties, property)) {
          const value = node.properties[property];
          if (!valueMap.has(value)) valueMap.set(value, new Set());
          valueMap.get(value).add(nodeId);
        }
      }
    }
  }

  dropIndex(label, property) {
    const propMap = this.propertyIndexes.get(label);
    if (propMap) propMap.delete(property);
  }

  hasIndex(label, property) {
    const propMap = this.propertyIndexes.get(label);
    return !!(propMap && propMap.has(property));
  }

  indexCardinality(label, property) {
    const propMap = this.propertyIndexes.get(label);
    if (!propMap || !propMap.has(property)) return null;
    return propMap.get(property).size;
  }

  updateNode(nodeId, properties) {
    const node = this.nodes.get(nodeId);
    if (!node) throw new Error(`node ${nodeId} not found`);
    for (const [key, newValue] of Object.entries(properties)) {
      const hadKey = Object.prototype.hasOwnProperty.call(node.properties, key);
      const oldValue = node.properties[key];
      if (hadKey && oldValue === newValue) continue;
      if (hadKey) {
        for (const label of node.labels) {
          this._removeFromPropertyIndex(label, key, oldValue, nodeId);
        }
      }
      node.properties[key] = newValue;
      for (const label of node.labels) {
        this._addToPropertyIndex(label, key, newValue, nodeId);
      }
    }
    return node;
  }

  addLabel(nodeId, label) {
    const node = this.nodes.get(nodeId);
    if (!node) throw new Error(`node ${nodeId} not found`);
    if (node.labels.has(label)) return node;
    node.labels.add(label);
    this._addToLabelIndex(label, nodeId);
    this._indexAllProperties(label, nodeId, node.properties);
    return node;
  }

  deleteNode(nodeId) {
    const node = this.nodes.get(nodeId);
    if (!node) return false;
    const out = this.outAdj.get(nodeId);
    for (const edgeIds of out.values()) {
      for (const edgeId of Array.from(edgeIds)) this.deleteEdge(edgeId);
    }
    const inc = this.inAdj.get(nodeId);
    for (const edgeIds of inc.values()) {
      for (const edgeId of Array.from(edgeIds)) this.deleteEdge(edgeId);
    }
    for (const label of node.labels) {
      const set = this.labelIndex.get(label);
      if (set) {
        set.delete(nodeId);
        if (set.size === 0) this.labelIndex.delete(label);
      }
      const propMap = this.propertyIndexes.get(label);
      if (propMap) {
        for (const prop of propMap.keys()) {
          if (Object.prototype.hasOwnProperty.call(node.properties, prop)) {
            this._removeFromPropertyIndex(label, prop, node.properties[prop], nodeId);
          }
        }
      }
    }
    this.nodes.delete(nodeId);
    this.outAdj.delete(nodeId);
    this.inAdj.delete(nodeId);
    return true;
  }

  addEdge({ id, type, from, to, properties = {} } = {}) {
    if (!this.nodes.has(from)) throw new Error(`node ${from} not found`);
    if (!this.nodes.has(to)) throw new Error(`node ${to} not found`);
    const edgeId = id !== undefined && id !== null ? id : `e${this._nextEdgeId++}`;
    if (this.edges.has(edgeId)) throw new Error(`edge ${edgeId} already exists`);
    const edge = { id: edgeId, type, from, to, properties: { ...properties } };
    this.edges.set(edgeId, edge);
    this._addToAdj(this.outAdj, from, type, edgeId);
    this._addToAdj(this.inAdj, to, type, edgeId);
    return edge;
  }

  _addToAdj(adj, nodeId, type, edgeId) {
    const map = adj.get(nodeId);
    if (!map.has(type)) map.set(type, new Set());
    map.get(type).add(edgeId);
  }

  _removeFromAdj(adj, nodeId, type, edgeId) {
    const map = adj.get(nodeId);
    if (!map) return;
    const set = map.get(type);
    if (!set) return;
    set.delete(edgeId);
    if (set.size === 0) map.delete(type);
  }

  updateEdge(edgeId, properties) {
    const edge = this.edges.get(edgeId);
    if (!edge) throw new Error(`edge ${edgeId} not found`);
    Object.assign(edge.properties, properties);
    return edge;
  }

  deleteEdge(edgeId) {
    const edge = this.edges.get(edgeId);
    if (!edge) return false;
    this._removeFromAdj(this.outAdj, edge.from, edge.type, edgeId);
    this._removeFromAdj(this.inAdj, edge.to, edge.type, edgeId);
    this.edges.delete(edgeId);
    return true;
  }

  getNode(nodeId) {
    return this.nodes.get(nodeId);
  }

  getEdge(edgeId) {
    return this.edges.get(edgeId);
  }

  nodeIdsByLabel(label) {
    return this.labelIndex.get(label) || new Set();
  }

  nodeIdsByIndex(label, property, value) {
    const propMap = this.propertyIndexes.get(label);
    if (!propMap || !propMap.has(property)) return null;
    const valueMap = propMap.get(property);
    return valueMap.get(value) || new Set();
  }

  allNodeIds() {
    return Array.from(this.nodes.keys());
  }

  outEdgeIds(nodeId, edgeType) {
    const map = this.outAdj.get(nodeId);
    if (!map) return [];
    if (edgeType !== undefined) return Array.from(map.get(edgeType) || []);
    const result = [];
    for (const set of map.values()) result.push(...set);
    return result;
  }

  inEdgeIds(nodeId, edgeType) {
    const map = this.inAdj.get(nodeId);
    if (!map) return [];
    if (edgeType !== undefined) return Array.from(map.get(edgeType) || []);
    const result = [];
    for (const set of map.values()) result.push(...set);
    return result;
  }

  outDegree(nodeId, edgeType) {
    return this.outEdgeIds(nodeId, edgeType).length;
  }

  inDegree(nodeId, edgeType) {
    return this.inEdgeIds(nodeId, edgeType).length;
  }

  transaction(fn) {
    const snapshot = this._snapshot();
    try {
      return fn();
    } catch (err) {
      this._restore(snapshot);
      throw err;
    }
  }

  _snapshot() {
    return structuredClone({
      nodes: this.nodes,
      edges: this.edges,
      outAdj: this.outAdj,
      inAdj: this.inAdj,
      labelIndex: this.labelIndex,
      propertyIndexes: this.propertyIndexes,
      nextNodeId: this._nextNodeId,
      nextEdgeId: this._nextEdgeId,
    });
  }

  _restore(snapshot) {
    this.nodes = snapshot.nodes;
    this.edges = snapshot.edges;
    this.outAdj = snapshot.outAdj;
    this.inAdj = snapshot.inAdj;
    this.labelIndex = snapshot.labelIndex;
    this.propertyIndexes = snapshot.propertyIndexes;
    this._nextNodeId = snapshot.nextNodeId;
    this._nextEdgeId = snapshot.nextEdgeId;
  }
}

module.exports = { PropertyGraph };
