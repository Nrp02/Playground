'use strict';

const { RpcServer } = require('./server.js');
const { RpcClient } = require('./client.js');

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function main() {
  const server = new RpcServer();
  server.register('add', async ({ a, b, delay }) => {
    await sleep(delay);
    return { sum: a + b, delay };
  });
  server.register('boom', async () => {
    throw new Error('handler exploded on purpose');
  });
  server.register('neverResponds', async () => {
    await sleep(5000);
    return 'too late';
  });

  const address = await server.listen(0);
  const client = new RpcClient({ port: address.port });

  console.log('firing 50 concurrent calls with varying latency...');
  const completionOrder = [];
  const calls = [];
  for (let i = 0; i < 50; i++) {
    const delay = (49 - i) % 20;
    calls.push(
      client.call('add', { a: i, b: i * 2, delay }).then((res) => {
        completionOrder.push(i);
        return { i, res };
      })
    );
  }
  const results = await Promise.all(calls);
  for (const { i, res } of results) {
    if (res.sum !== i * 3) {
      throw new Error('incorrect concurrent result');
    }
  }
  const outOfOrder = completionOrder.some((i, position) => i !== position);
  console.log(`all 50 concurrent calls resolved correctly (out-of-order arrival observed: ${outOfOrder})`);

  console.log('calling a handler that throws...');
  try {
    await client.call('boom', {});
    throw new Error('expected boom to reject');
  } catch (err) {
    console.log(`caller correctly rejected: ${err.message}`);
  }

  console.log('calling a handler that will time out...');
  try {
    await client.call('neverResponds', {}, { timeoutMs: 150, retries: 0 });
    throw new Error('expected timeout');
  } catch (err) {
    console.log(`caller correctly timed out: ${err.message}`);
  }

  console.log('starting graceful shutdown while an in-flight call is running...');
  const inFlight = client.call('add', { a: 100, b: 1, delay: 40 });
  await sleep(5);
  const shutdownPromise = server.close(2000);
  const inFlightResult = await inFlight;
  console.log(`in-flight call completed during drain: ${JSON.stringify(inFlightResult)}`);
  await shutdownPromise;
  console.log('server shut down gracefully');

  await client.close();
  console.log('demo complete');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
