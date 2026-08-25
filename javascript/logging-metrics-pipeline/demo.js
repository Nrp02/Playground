'use strict';

const { LoggingMetricsPipeline, InMemorySink, REJECT_NEW } = require('./pipeline.js');

function scenarioBatchAndInterval() {
  console.log('--- batch-size and interval flushing ---');
  const sink = new InMemorySink();
  const pipeline = new LoggingMetricsPipeline(sink, {
    batchSize: 5,
    flushIntervalMs: 150,
  });

  for (let i = 0; i < 12; i += 1) {
    pipeline.ingest({ type: 'log', level: 'info', message: `request ${i} handled` });
  }
  console.log(`flushed by batch-size so far: ${sink.batches.length} batch(es)`);

  setTimeout(() => {
    console.log(`after interval tick: ${sink.batches.length} batch(es)`);
    pipeline.stop();
    scenarioBackpressure();
  }, 300);
}

function scenarioBackpressure() {
  console.log('\n--- backpressure under a flood of events ---');
  const sink = new InMemorySink();
  const pipeline = new LoggingMetricsPipeline(sink, {
    batchSize: 1000,
    flushIntervalMs: 10000,
    maxBufferSize: 10,
    backpressurePolicy: REJECT_NEW,
  });

  let accepted = 0;
  let rejected = 0;
  for (let i = 0; i < 50; i += 1) {
    const ok = pipeline.ingest({ type: 'log', level: 'warn', message: `flood event ${i}` });
    if (ok) {
      accepted += 1;
    } else {
      rejected += 1;
    }
  }

  console.log(`accepted: ${accepted}, rejected: ${rejected}`);
  console.log('stats:', pipeline.getStats());
  pipeline.stop();
  scenarioAggregation();
}

function scenarioAggregation() {
  console.log('\n--- metric aggregation over a batch window ---');
  const sink = new InMemorySink();
  const pipeline = new LoggingMetricsPipeline(sink, {
    batchSize: 6,
    flushIntervalMs: 10000,
  });

  const latencies = [12, 45, 8, 33, 21];
  for (const value of latencies) {
    pipeline.ingest({ type: 'metric', name: 'request_latency_ms', value });
  }
  pipeline.ingest({ type: 'metric', name: 'queue_depth', value: 4 });

  console.log('aggregates for the flushed batch:', pipeline.getLastAggregates());
  pipeline.stop();
}

scenarioBatchAndInterval();
