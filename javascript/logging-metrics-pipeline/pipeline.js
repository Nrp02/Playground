'use strict';

const DROP_OLDEST = 'drop-oldest';
const REJECT_NEW = 'reject-new';

function aggregateMetrics(events) {
  const stats = new Map();
  for (const event of events) {
    if (event.type !== 'metric' || typeof event.value !== 'number') {
      continue;
    }
    let entry = stats.get(event.name);
    if (!entry) {
      entry = { count: 0, sum: 0, min: Infinity, max: -Infinity };
      stats.set(event.name, entry);
    }
    entry.count += 1;
    entry.sum += event.value;
    entry.min = Math.min(entry.min, event.value);
    entry.max = Math.max(entry.max, event.value);
  }
  const result = {};
  for (const [name, entry] of stats) {
    result[name] = {
      count: entry.count,
      sum: entry.sum,
      min: entry.min,
      max: entry.max,
      avg: entry.sum / entry.count,
    };
  }
  return result;
}

class InMemorySink {
  constructor() {
    this.batches = [];
  }

  write(batch) {
    this.batches.push(batch);
  }
}

class LoggingMetricsPipeline {
  constructor(sink, options = {}) {
    if (!sink || typeof sink.write !== 'function') {
      throw new TypeError('sink must implement write(batch)');
    }
    this.sink = sink;
    this.batchSize = options.batchSize || 100;
    this.flushIntervalMs = options.flushIntervalMs || 1000;
    this.maxBufferSize = options.maxBufferSize || 1000;
    this.backpressurePolicy = options.backpressurePolicy === REJECT_NEW ? REJECT_NEW : DROP_OLDEST;

    this._buffer = [];
    this._droppedCount = 0;
    this._rejectedCount = 0;
    this._flushCount = 0;
    this._lastAggregates = {};

    this._setIntervalFn = options.setIntervalFn || setInterval;
    this._clearIntervalFn = options.clearIntervalFn || clearInterval;
    this._timer = this._setIntervalFn(() => this.flush(), this.flushIntervalMs);
    if (this._timer && typeof this._timer.unref === 'function') {
      this._timer.unref();
    }
  }

  ingest(event) {
    if (this._buffer.length >= this.maxBufferSize) {
      if (this.backpressurePolicy === REJECT_NEW) {
        this._rejectedCount += 1;
        return false;
      }
      this._buffer.shift();
      this._droppedCount += 1;
    }

    this._buffer.push(event);

    if (this._buffer.length >= this.batchSize) {
      this.flush();
    }
    return true;
  }

  flush() {
    if (this._buffer.length === 0) {
      return null;
    }
    const batch = this._buffer;
    this._buffer = [];
    this._flushCount += 1;
    this._lastAggregates = aggregateMetrics(batch);
    this.sink.write(batch);
    return batch;
  }

  getLastAggregates() {
    return this._lastAggregates;
  }

  getStats() {
    return {
      bufferedCount: this._buffer.length,
      droppedCount: this._droppedCount,
      rejectedCount: this._rejectedCount,
      flushCount: this._flushCount,
    };
  }

  stop() {
    this._clearIntervalFn(this._timer);
  }
}

module.exports = {
  LoggingMetricsPipeline,
  InMemorySink,
  aggregateMetrics,
  DROP_OLDEST,
  REJECT_NEW,
};
