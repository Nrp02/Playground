'use strict';

const net = require('net');
const { RespDecoder, encodeValue, encodeError } = require('./resp.js');
const { CommandHandler } = require('./store.js');

class RedisServer {
  constructor(commandHandler = new CommandHandler()) {
    this.commandHandler = commandHandler;
    this._server = net.createServer((socket) => this._handleConnection(socket));
  }

  _handleConnection(socket) {
    const decoder = new RespDecoder();
    socket.on('data', (chunk) => {
      decoder.append(chunk);
      let parsed;
      while ((parsed = decoder.decodeNext()) !== null) {
        this._handleRequest(socket, parsed);
      }
    });
    socket.on('error', () => {});
  }

  _handleRequest(socket, parsed) {
    if (parsed.type !== 'array' || !Array.isArray(parsed.value)) {
      socket.write(encodeError('ERR expected array of bulk strings'));
      return;
    }
    const argv = parsed.value.map((item) => item.value);
    const result = this.commandHandler.execute(argv);
    socket.write(encodeValue(result));
  }

  listen(port = 0, host = '127.0.0.1') {
    return new Promise((resolve, reject) => {
      this._server.once('error', reject);
      this._server.listen(port, host, () => {
        this._server.removeListener('error', reject);
        resolve(this._server.address());
      });
    });
  }

  close() {
    return new Promise((resolve) => {
      this._server.close(() => resolve());
    });
  }
}

module.exports = { RedisServer };
