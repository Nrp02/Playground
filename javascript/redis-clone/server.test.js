'use strict';

const assert = require('assert');
const net = require('net');
const { RespDecoder, encodeArray } = require('./resp.js');
const { RedisServer } = require('./server.js');

let passCount = 0;
let failCount = 0;
let pendingAsync = 0;

function test(name, fn) {
  pendingAsync++;
  fn()
    .then(() => {
      passCount++;
      console.log(`  PASS  ${name}`);
    })
    .catch((err) => {
      failCount++;
      console.log(`  FAIL  ${name}`);
      console.log(`        ${err.message}`);
    })
    .finally(() => {
      pendingAsync--;
      if (pendingAsync === 0) {
        console.log(`\n${passCount} passed, ${failCount} failed`);
        process.exit(failCount === 0 ? 0 : 1);
      }
    });
}

function withTimeout(promise, ms, label) {
  return Promise.race([
    promise,
    new Promise((_, reject) => setTimeout(() => reject(new Error(`timed out: ${label}`)), ms)),
  ]);
}

function sendCommand(socket, decoder, args) {
  return new Promise((resolve) => {
    const wireArgs = args.map((a) => ({ type: 'bulk', value: String(a) }));
    socket.write(encodeArray(wireArgs));
    const onData = (chunk) => {
      decoder.append(chunk);
      const parsed = decoder.decodeNext();
      if (parsed !== null) {
        socket.removeListener('data', onData);
        resolve(parsed);
      }
    };
    socket.on('data', onData);
  });
}

test('server accepts a real TCP connection and answers PING/SET/GET over the wire', async () => {
  const server = new RedisServer();
  const address = await server.listen(0);
  const socket = net.createConnection(address.port, '127.0.0.1');
  try {
    await withTimeout(
      new Promise((resolve, reject) => {
        socket.once('connect', resolve);
        socket.once('error', reject);
      }),
      2000,
      'connect'
    );
    const decoder = new RespDecoder();

    const pingResult = await withTimeout(sendCommand(socket, decoder, ['PING']), 2000, 'ping');
    assert.deepStrictEqual(pingResult, { type: 'simple', value: 'PONG' });

    const setResult = await withTimeout(
      sendCommand(socket, decoder, ['SET', 'foo', 'bar']),
      2000,
      'set'
    );
    assert.deepStrictEqual(setResult, { type: 'simple', value: 'OK' });

    const getResult = await withTimeout(sendCommand(socket, decoder, ['GET', 'foo']), 2000, 'get');
    assert.deepStrictEqual(getResult, { type: 'bulk', value: 'bar' });
  } finally {
    socket.end();
    socket.destroy();
    await server.close();
  }
});

test('server handles pipelined commands split across TCP chunks', async () => {
  const server = new RedisServer();
  const address = await server.listen(0);
  const socket = net.createConnection(address.port, '127.0.0.1');
  try {
    await withTimeout(
      new Promise((resolve, reject) => {
        socket.once('connect', resolve);
        socket.once('error', reject);
      }),
      2000,
      'connect'
    );
    const decoder = new RespDecoder();

    const results = [];
    const done = new Promise((resolve) => {
      socket.on('data', (chunk) => {
        decoder.append(chunk);
        let parsed;
        while ((parsed = decoder.decodeNext()) !== null) {
          results.push(parsed);
          if (results.length === 2) {
            resolve();
          }
        }
      });
    });

    socket.write(encodeArray([{ type: 'bulk', value: 'SET' }, { type: 'bulk', value: 'a' }, { type: 'bulk', value: '1' }]));
    socket.write(encodeArray([{ type: 'bulk', value: 'GET' }, { type: 'bulk', value: 'a' }]));

    await withTimeout(done, 2000, 'pipelined commands');
    assert.deepStrictEqual(results[0], { type: 'simple', value: 'OK' });
    assert.deepStrictEqual(results[1], { type: 'bulk', value: '1' });
  } finally {
    socket.end();
    socket.destroy();
    await server.close();
  }
});
