'use strict';

const assert = require('assert');
const {
  LoggingMetricsPipeline,
  InMemorySink,
  aggregateMetrics,
  REJECT_NEW,
} = require('./pipeline.js');

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

function fakeTimer() {
  return {
    scheduled: [],
    setIntervalFn(fn, ms) {
      const handle = { fn, ms, cancelled: false };
      this.scheduled.push(handle);
      return handle;
    },
    clearIntervalFn(handle) {
      handle.cancelled = true;
    },
    tick() {
      for (const handle of this.scheduled) {
        if (!handle.cancelled) {
          handle.fn();
        }
      }
    },
  };
}

test('flush triggers automatically once batch-size threshold is hit', () => {
  const sink = new InMemorySink();
  const timer = fakeTimer();
  const pipeline = new LoggingMetricsPipeline(sink, {
    batchSize: 3,
    flushIntervalMs: 1000,
    setIntervalFn: timer.setIntervalFn.bind(timer),
    clearIntervalFn: timer.clearIntervalFn.bind(timer),
  });

  pipeline.ingest({ type: 'log', message: 'a' });
  pipeline.ingest({ type: 'log', message: 'b' });
  assert.strictEqual(sink.batches.length, 0);
  pipeline.ingest({ type: 'log', message: 'c' });

  assert.strictEqual(sink.batches.length, 1);
  assert.strictEqual(sink.batches[0].length, 3);
  assert.strictEqual(pipeline.getStats().bufferedCount, 0);
  pipeline.stop();
});

test('flush triggers on the interval timer even under the batch-size threshold', () => {
  const sink = new InMemorySink();
  const timer = fakeTimer();
  const pipeline = new LoggingMetricsPipeline(sink, {
    batchSize: 100,
    flushIntervalMs: 50,
    setIntervalFn: timer.setIntervalFn.bind(timer),
    clearIntervalFn: timer.clearIntervalFn.bind(timer),
  });

  pipeline.ingest({ type: 'log', message: 'a' });
  pipeline.ingest({ type: 'log', message: 'b' });
  assert.strictEqual(sink.batches.length, 0);

  timer.tick();

  assert.strictEqual(sink.batches.length, 1);
  assert.strictEqual(sink.batches[0].length, 2);
  pipeline.stop();
});

test('drop-oldest backpressure policy evicts the oldest buffered event on overflow', () => {
  const sink = new InMemorySink();
  const timer = fakeTimer();
  const pipeline = new LoggingMetricsPipeline(sink, {
    batchSize: 100,
    maxBufferSize: 3,
    flushIntervalMs: 1000,
    setIntervalFn: timer.setIntervalFn.bind(timer),
    clearIntervalFn: timer.clearIntervalFn.bind(timer),
  });

  assert.strictEqual(pipeline.ingest({ type: 'log', message: '1' }), true);
  assert.strictEqual(pipeline.ingest({ type: 'log', message: '2' }), true);
  assert.strictEqual(pipeline.ingest({ type: 'log', message: '3' }), true);
  assert.strictEqual(pipeline.ingest({ type: 'log', message: '4' }), true);

  timer.tick();

  assert.strictEqual(sink.batches[0].length, 3);
  assert.deepStrictEqual(
    sink.batches[0].map((e) => e.message),
    ['2', '3', '4'],
  );
  assert.strictEqual(pipeline.getStats().droppedCount, 1);
  pipeline.stop();
});

test('reject-new backpressure policy refuses new events once the buffer is full', () => {
  const sink = new InMemorySink();
  const timer = fakeTimer();
  const pipeline = new LoggingMetricsPipeline(sink, {
    batchSize: 100,
    maxBufferSize: 2,
    flushIntervalMs: 1000,
    backpressurePolicy: REJECT_NEW,
    setIntervalFn: timer.setIntervalFn.bind(timer),
    clearIntervalFn: timer.clearIntervalFn.bind(timer),
  });

  assert.strictEqual(pipeline.ingest({ type: 'log', message: '1' }), true);
  assert.strictEqual(pipeline.ingest({ type: 'log', message: '2' }), true);
  assert.strictEqual(pipeline.ingest({ type: 'log', message: '3' }), false);

  timer.tick();

  assert.strictEqual(sink.batches[0].length, 2);
  assert.deepStrictEqual(
    sink.batches[0].map((e) => e.message),
    ['1', '2'],
  );
  assert.strictEqual(pipeline.getStats().rejectedCount, 1);
  pipeline.stop();
});

test('aggregateMetrics computes count/sum/min/max/avg per metric name', () => {
  const events = [
    { type: 'metric', name: 'latency_ms', value: 10 },
    { type: 'metric', name: 'latency_ms', value: 30 },
    { type: 'metric', name: 'latency_ms', value: 20 },
    { type: 'metric', name: 'queue_depth', value: 5 },
    { type: 'log', message: 'not a metric' },
  ];

  const result = aggregateMetrics(events);

  assert.deepStrictEqual(result.latency_ms, { count: 3, sum: 60, min: 10, max: 30, avg: 20 });
  assert.deepStrictEqual(result.queue_depth, { count: 1, sum: 5, min: 5, max: 5, avg: 5 });
  assert.strictEqual(Object.keys(result).length, 2);
});

test('pipeline records aggregates for the batch on flush', () => {
  const sink = new InMemorySink();
  const timer = fakeTimer();
  const pipeline = new LoggingMetricsPipeline(sink, {
    batchSize: 2,
    flushIntervalMs: 1000,
    setIntervalFn: timer.setIntervalFn.bind(timer),
    clearIntervalFn: timer.clearIntervalFn.bind(timer),
  });

  pipeline.ingest({ type: 'metric', name: 'cpu', value: 40 });
  pipeline.ingest({ type: 'metric', name: 'cpu', value: 60 });

  const aggregates = pipeline.getLastAggregates();
  assert.deepStrictEqual(aggregates.cpu, { count: 2, sum: 100, min: 40, max: 60, avg: 50 });
  pipeline.stop();
});

test('flush is a no-op when the buffer is empty', () => {
  const sink = new InMemorySink();
  const timer = fakeTimer();
  const pipeline = new LoggingMetricsPipeline(sink, {
    setIntervalFn: timer.setIntervalFn.bind(timer),
    clearIntervalFn: timer.clearIntervalFn.bind(timer),
  });

  const result = pipeline.flush();
  assert.strictEqual(result, null);
  assert.strictEqual(sink.batches.length, 0);
  pipeline.stop();
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
