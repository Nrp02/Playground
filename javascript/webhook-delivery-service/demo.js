'use strict';

const { WebhookDeliveryService, verifySignature } = require('./webhookDeliveryService.js');

function receiverVerify(secret, body, headers) {
  const valid = verifySignature(secret, body, headers['X-Webhook-Signature']);
  console.log(`  receiver: signature valid=${valid} deliveryId=${headers['X-Webhook-Id']}`);
  return valid;
}

async function main() {
  const failUntilAttempt = new Map();
  const permanentlyFailUrl = 'https://flaky-endpoint.example.com/hook';

  const transport = async ({ url, headers, body }) => {
    if (url === 'https://ok-endpoint.example.com/hook') {
      return { status: 200 };
    }
    if (url === 'https://retry-endpoint.example.com/hook') {
      const soFar = (failUntilAttempt.get(url) || 0) + 1;
      failUntilAttempt.set(url, soFar);
      if (soFar < 3) {
        return { status: 503 };
      }
      return { status: 200 };
    }
    if (url === permanentlyFailUrl) {
      throw new Error('connection refused');
    }
    throw new Error(`unknown endpoint ${url}`);
  };

  let totalDelayMs = 0;
  const service = new WebhookDeliveryService({
    transport,
    maxAttempts: 4,
    baseDelayMs: 50,
    maxDelayMs: 400,
    sleepFn: async (ms) => {
      totalDelayMs += ms;
      console.log(`  backing off ${ms}ms before next attempt`);
    },
  });

  const okSecret = 'ok-secret-abc';
  const retrySecret = 'retry-secret-def';
  const flakySecret = 'flaky-secret-ghi';

  const okId = service.register('https://ok-endpoint.example.com/hook', okSecret);
  console.log(`registered subscriber ${okId.slice(0, 8)} at ok-endpoint\n`);

  console.log('== event 1: immediate success ==');
  const event1 = { id: 'evt-1', type: 'order.created', data: { orderId: 42 } };
  const results1 = await service.deliver(event1);
  for (const r of results1) {
    console.log(`  subscriber=${r.subscriberId.slice(0, 8)} status=${r.status} attempts=${r.attempts}`);
  }
  service.unregister(okId);

  const retryId = service.register('https://retry-endpoint.example.com/hook', retrySecret);
  console.log(`\nregistered subscriber ${retryId.slice(0, 8)} at retry-endpoint`);
  console.log('== event 2: fails twice then succeeds on retry ==');
  const event2 = { id: 'evt-2', type: 'order.shipped', data: { orderId: 43 } };
  const results2 = await service.deliver(event2);
  for (const r of results2) {
    console.log(`  subscriber=${r.subscriberId.slice(0, 8)} status=${r.status} attempts=${r.attempts} totalBackoff=${totalDelayMs}ms`);
  }
  service.unregister(retryId);

  const flakyId = service.register(permanentlyFailUrl, flakySecret);
  console.log(`\nregistered subscriber ${flakyId.slice(0, 8)} at flaky-endpoint`);
  console.log('== event 3: exhausts retries, lands in dead-letter queue ==');
  const event3 = { id: 'evt-3', type: 'order.cancelled', data: { orderId: 44 } };
  const results3 = await service.deliver(event3);
  for (const r of results3) {
    console.log(`  subscriber=${r.subscriberId.slice(0, 8)} status=${r.status} attempts=${r.attempts}`);
  }
  console.log(`  dead-letter queue size: ${service.getDeadLetterQueue().length}`);
  service.unregister(flakyId);

  const okId2 = service.register('https://ok-endpoint.example.com/hook', okSecret);
  console.log(`\n== event 1 redelivered to a re-registered ok subscriber (idempotency is per event+subscriber, so this is a fresh delivery) ==`);
  const resultsFresh = await service.deliver(event1);
  for (const r of resultsFresh) {
    console.log(`  subscriber=${r.subscriberId.slice(0, 8)} deduplicated=${r.deduplicated} status=${r.status}`);
  }
  console.log('\n== event 1 redelivered again to the same subscriber (idempotent, deduplicated) ==');
  const resultsDedup = await service.deliver(event1);
  for (const r of resultsDedup) {
    console.log(`  subscriber=${r.subscriberId.slice(0, 8)} deduplicated=${r.deduplicated} status=${r.status}`);
  }

  console.log('\n== receiver-side signature verification ==');
  const okDelivery = service.getDelivery(okId2, event1.id);
  const payload = JSON.stringify(event1);
  receiverVerify(okSecret, payload, {
    'X-Webhook-Id': okDelivery.deliveryId,
    'X-Webhook-Signature': require('crypto').createHmac('sha256', okSecret).update(payload).digest('hex'),
  });

  console.log('  receiver rejects a tampered payload:');
  const tamperedPayload = JSON.stringify({ ...event1, data: { orderId: 999 } });
  receiverVerify(okSecret, tamperedPayload, {
    'X-Webhook-Id': okDelivery.deliveryId,
    'X-Webhook-Signature': require('crypto').createHmac('sha256', okSecret).update(payload).digest('hex'),
  });
}

main();
