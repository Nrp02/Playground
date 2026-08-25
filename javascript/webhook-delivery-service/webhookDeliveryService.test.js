'use strict';

const assert = require('assert');
const crypto = require('crypto');
const { WebhookDeliveryService, computeSignature, verifySignature } = require('./webhookDeliveryService.js');

let passCount = 0;
let failCount = 0;

function test(name, fn) {
  return fn()
    .then(() => {
      passCount++;
      console.log(`  PASS  ${name}`);
    })
    .catch((err) => {
      failCount++;
      console.log(`  FAIL  ${name}`);
      console.log(`        ${err.message}`);
    });
}

function noopSleep() {
  return Promise.resolve();
}

async function run() {
  await test('successful delivery on first attempt', async () => {
    const service = new WebhookDeliveryService({
      transport: async () => ({ status: 200 }),
      sleepFn: noopSleep,
    });
    const subId = service.register('https://example.com/hook', 'secret1');
    const [record] = await service.deliver({ id: 'evt-1', type: 'x' });
    assert.strictEqual(record.status, 'delivered');
    assert.strictEqual(record.attempts, 1);
    assert.strictEqual(record.subscriberId, subId);
  });

  await test('retries with exponential backoff then succeeds', async () => {
    let calls = 0;
    const delays = [];
    const service = new WebhookDeliveryService({
      transport: async () => {
        calls += 1;
        if (calls < 3) {
          return { status: 500 };
        }
        return { status: 200 };
      },
      maxAttempts: 5,
      baseDelayMs: 10,
      maxDelayMs: 1000,
      sleepFn: async (ms) => {
        delays.push(ms);
      },
    });
    service.register('https://example.com/hook', 'secret1');
    const [record] = await service.deliver({ id: 'evt-2', type: 'x' });
    assert.strictEqual(record.status, 'delivered');
    assert.strictEqual(record.attempts, 3);
    assert.strictEqual(calls, 3);
    assert.deepStrictEqual(delays, [10, 20]);
  });

  await test('exhausts retries and lands in dead-letter queue', async () => {
    const delays = [];
    const service = new WebhookDeliveryService({
      transport: async () => ({ status: 500 }),
      maxAttempts: 4,
      baseDelayMs: 5,
      maxDelayMs: 100,
      sleepFn: async (ms) => {
        delays.push(ms);
      },
    });
    service.register('https://example.com/hook', 'secret1');
    const [record] = await service.deliver({ id: 'evt-3', type: 'x' });
    assert.strictEqual(record.status, 'dead-letter');
    assert.strictEqual(record.attempts, 4);
    assert.deepStrictEqual(delays, [5, 10, 20]);
    assert.strictEqual(service.getDeadLetterQueue().length, 1);
    assert.strictEqual(service.getDeadLetterQueue()[0].deliveryId, record.deliveryId);
  });

  await test('backoff delay is capped at maxDelayMs', async () => {
    const delays = [];
    const service = new WebhookDeliveryService({
      transport: async () => ({ status: 500 }),
      maxAttempts: 6,
      baseDelayMs: 100,
      maxDelayMs: 250,
      sleepFn: async (ms) => {
        delays.push(ms);
      },
    });
    service.register('https://example.com/hook', 'secret1');
    await service.deliver({ id: 'evt-4', type: 'x' });
    assert.deepStrictEqual(delays, [100, 200, 250, 250, 250]);
  });

  await test('valid HMAC signature verifies correctly', async () => {
    const secret = 'top-secret';
    const payload = JSON.stringify({ id: 'evt-5', data: 1 });
    const signature = computeSignature(secret, payload);
    assert.strictEqual(verifySignature(secret, payload, signature), true);
  });

  await test('tampered payload fails signature verification', async () => {
    const secret = 'top-secret';
    const payload = JSON.stringify({ id: 'evt-6', data: 1 });
    const signature = computeSignature(secret, payload);
    const tampered = JSON.stringify({ id: 'evt-6', data: 2 });
    assert.strictEqual(verifySignature(secret, tampered, signature), false);
  });

  await test('wrong secret fails signature verification', async () => {
    const payload = JSON.stringify({ id: 'evt-7', data: 1 });
    const signature = computeSignature('secret-a', payload);
    assert.strictEqual(verifySignature('secret-b', payload, signature), false);
  });

  await test('delivery uses HMAC derived from subscriber secret and payload', async () => {
    let capturedHeaders;
    let capturedBody;
    const secret = 'subscriber-secret';
    const service = new WebhookDeliveryService({
      transport: async ({ headers, body }) => {
        capturedHeaders = headers;
        capturedBody = body;
        return { status: 200 };
      },
      sleepFn: noopSleep,
    });
    service.register('https://example.com/hook', secret);
    const event = { id: 'evt-8', type: 'x' };
    await service.deliver(event);
    const expectedSignature = crypto.createHmac('sha256', secret).update(capturedBody).digest('hex');
    assert.strictEqual(capturedHeaders['X-Webhook-Signature'], expectedSignature);
    assert.strictEqual(capturedBody, JSON.stringify(event));
  });

  await test('redelivering the same event id is deduplicated via delivery id', async () => {
    let calls = 0;
    const service = new WebhookDeliveryService({
      transport: async () => {
        calls += 1;
        return { status: 200 };
      },
      sleepFn: noopSleep,
    });
    service.register('https://example.com/hook', 'secret1');
    const event = { id: 'evt-9', type: 'x' };
    const [first] = await service.deliver(event);
    const [second] = await service.deliver(event);
    assert.strictEqual(first.deduplicated, false);
    assert.strictEqual(second.deduplicated, true);
    assert.strictEqual(second.deliveryId, first.deliveryId);
    assert.strictEqual(calls, 1);
  });

  await test('dead-lettered delivery is also deduplicated on redelivery', async () => {
    const service = new WebhookDeliveryService({
      transport: async () => ({ status: 500 }),
      maxAttempts: 2,
      baseDelayMs: 1,
      maxDelayMs: 5,
      sleepFn: noopSleep,
    });
    service.register('https://example.com/hook', 'secret1');
    const event = { id: 'evt-10', type: 'x' };
    const [first] = await service.deliver(event);
    const [second] = await service.deliver(event);
    assert.strictEqual(first.status, 'dead-letter');
    assert.strictEqual(second.deduplicated, true);
    assert.strictEqual(service.getDeadLetterQueue().length, 1);
  });

  await test('different subscribers get independent delivery ids for the same event', async () => {
    const service = new WebhookDeliveryService({
      transport: async () => ({ status: 200 }),
      sleepFn: noopSleep,
    });
    service.register('https://a.example.com/hook', 'secret-a');
    service.register('https://b.example.com/hook', 'secret-b');
    const records = await service.deliver({ id: 'evt-11', type: 'x' });
    assert.strictEqual(records.length, 2);
    assert.notStrictEqual(records[0].deliveryId, records[1].deliveryId);
  });

  await test('a network-error rejection is treated as a retryable failure', async () => {
    let calls = 0;
    const service = new WebhookDeliveryService({
      transport: async () => {
        calls += 1;
        if (calls === 1) {
          throw new Error('ECONNRESET');
        }
        return { status: 200 };
      },
      maxAttempts: 3,
      baseDelayMs: 1,
      maxDelayMs: 5,
      sleepFn: noopSleep,
    });
    service.register('https://example.com/hook', 'secret1');
    const [record] = await service.deliver({ id: 'evt-12', type: 'x' });
    assert.strictEqual(record.status, 'delivered');
    assert.strictEqual(record.attempts, 2);
  });

  await test('register rejects missing url or secret', async () => {
    const service = new WebhookDeliveryService({
      transport: async () => ({ status: 200 }),
      sleepFn: noopSleep,
    });
    assert.throws(() => service.register('', 'secret'), RangeError);
    assert.throws(() => service.register('https://example.com', ''), RangeError);
  });

  console.log(`\n${passCount} passed, ${failCount} failed`);
  process.exit(failCount === 0 ? 0 : 1);
}

run();
