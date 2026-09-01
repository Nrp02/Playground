'use strict';

const assert = require('assert');
const { RpcServer } = require('./server.js');
const { RpcClient } = require('./client.js');

let passCount = 0;
let failCount = 0;

async function test(name, fn) {
  try {
    await fn();
    passCount++;
    console.log(`  PASS  ${name}`);
  } catch (err) {
    failCount++;
    console.log(`  FAIL  ${name}`);
    console.log(`        ${err.stack || err.message}`);
  }
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function startServer(setup) {
  const server = new RpcServer();
  setup(server);
  const address = await server.listen(0);
  return { server, address };
}

async function main() {
  await test('resolves many concurrent calls correctly even when responses arrive out of order', async () => {
    const { server, address } = await startServer((s) => {
      s.register('echo', async ({ value, delay }) => {
        await sleep(delay);
        return value;
      });
    });
    const client = new RpcClient({ port: address.port });
    const delays = [];
    for (let i = 0; i < 40; i++) {
      delays.push((i * 7) % 30);
    }
    const promises = delays.map((delay, i) => client.call('echo', { value: i, delay }));
    const results = await Promise.all(promises);
    for (let i = 0; i < results.length; i++) {
      assert.strictEqual(results[i], i);
    }
    await client.close();
    await server.close();
  });

  await test('propagates handler errors as rejections without crashing the server', async () => {
    const { server, address } = await startServer((s) => {
      s.register('boom', async () => {
        throw new Error('deliberate failure');
      });
      s.register('ping', async () => 'pong');
    });
    const client = new RpcClient({ port: address.port });
    await assert.rejects(client.call('boom', {}), /deliberate failure/);
    const result = await client.call('ping', {});
    assert.strictEqual(result, 'pong');
    await client.close();
    await server.close();
  });

  await test('rejects unknown methods without crashing the server', async () => {
    const { server, address } = await startServer((s) => {
      s.register('ping', async () => 'pong');
    });
    const client = new RpcClient({ port: address.port });
    await assert.rejects(client.call('doesNotExist', {}), /unknown method/);
    const result = await client.call('ping', {});
    assert.strictEqual(result, 'pong');
    await client.close();
    await server.close();
  });

  await test('a call times out and cleans up the pending entry', async () => {
    const { server, address } = await startServer((s) => {
      s.register('slow', async () => {
        await sleep(5000);
        return 'late';
      });
    });
    const client = new RpcClient({ port: address.port });
    await assert.rejects(
      client.call('slow', {}, { timeoutMs: 100, retries: 0 }),
      /timed out/
    );
    assert.strictEqual(client._pending.size, 0);
    await client.close();
    await server.close();
  });

  await test('rejects all in-flight calls when the connection drops', async () => {
    const { server, address } = await startServer((s) => {
      s.register('hang', async () => {
        await sleep(5000);
        return 'late';
      });
    });
    const client = new RpcClient({ port: address.port });
    const inFlight = client.call('hang', {}, { timeoutMs: 10000, retries: 0 });
    await sleep(20);
    client._socket.destroy();
    await assert.rejects(inFlight, /connection closed/);
    await server.close();
  });

  await test('graceful shutdown drains in-flight calls before closing', async () => {
    const { server, address } = await startServer((s) => {
      s.register('slow', async () => {
        await sleep(150);
        return 'done';
      });
    });
    const client = new RpcClient({ port: address.port });
    const inFlight = client.call('slow', {}, { timeoutMs: 5000, retries: 0 });
    await sleep(20);
    const closePromise = server.close(2000);
    const result = await inFlight;
    assert.strictEqual(result, 'done');
    await closePromise;
    await client.close();
  });

  await test('rejects new calls received after shutdown begins', async () => {
    const { server, address } = await startServer((s) => {
      s.register('slow', async () => {
        await sleep(150);
        return 'done';
      });
    });
    const client = new RpcClient({ port: address.port });
    const inFlight = client.call('slow', {}, { timeoutMs: 5000, retries: 0 });
    await sleep(20);
    const closePromise = server.close(2000);
    await assert.rejects(
      client.call('slow', {}, { timeoutMs: 2000, retries: 0 }),
      /shutting down/
    );
    await inFlight;
    await closePromise;
    await client.close();
  });

  console.log(`\n${passCount} passed, ${failCount} failed`);
  process.exit(failCount === 0 ? 0 : 1);
}

main();
