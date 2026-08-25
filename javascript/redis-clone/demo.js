'use strict';

const net = require('net');
const { RespDecoder, encodeArray } = require('./resp.js');
const { RedisServer } = require('./server.js');

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

function describe(parsed) {
  if (parsed.type === 'bulk' && parsed.value === null) {
    return '(nil)';
  }
  if (parsed.type === 'error') {
    return `(error) ${parsed.value}`;
  }
  return String(parsed.value);
}

async function main() {
  const server = new RedisServer();
  const address = await server.listen(0);

  const socket = net.createConnection(address.port, '127.0.0.1');
  await new Promise((resolve) => socket.once('connect', resolve));
  const decoder = new RespDecoder();

  const commands = [
    ['PING'],
    ['SET', 'greeting', 'hello world'],
    ['GET', 'greeting'],
    ['EXPIRE', 'greeting', '1'],
    ['TTL', 'greeting'],
    ['GET', 'missing'],
    ['DEL', 'greeting'],
    ['GET', 'greeting'],
  ];

  for (const cmd of commands) {
    const parsed = await sendCommand(socket, decoder, cmd);
    console.log(`> ${cmd.join(' ')}`);
    console.log(`< ${describe(parsed)}`);
  }

  socket.end();
  await new Promise((resolve) => socket.once('close', resolve));
  await server.close();
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
