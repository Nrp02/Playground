'use strict';

const net = require('net');
const { MESSAGE_TYPES, encodeFrame, FrameDecoder } = require('./framing.js');

class RpcServer {
  constructor({ maxFrameSize } = {}) {
    this._maxFrameSize = maxFrameSize;
    this._handlers = new Map();
    this._connections = new Set();
    this._pending = new Set();
    this._closing = false;
    this._server = net.createServer((socket) => this._handleConnection(socket));
  }

  register(method, fn) {
    this._handlers.set(method, fn);
  }

  _handleConnection(socket) {
    const decoder = new FrameDecoder(this._maxFrameSize);
    this._connections.add(socket);
    socket.on('data', (chunk) => {
      try {
        decoder.append(chunk);
        let frame;
        while ((frame = decoder.decodeNext()) !== null) {
          this._dispatch(socket, frame);
        }
      } catch (err) {
        socket.destroy();
      }
    });
    socket.on('close', () => {
      this._connections.delete(socket);
    });
    socket.on('error', () => {});
  }

  async _dispatch(socket, frame) {
    if (frame.type !== MESSAGE_TYPES.REQUEST) return;
    const corrId = frame.corrId;
    if (this._closing) {
      this._safeWrite(socket, corrId, new Error('server is shutting down'));
      return;
    }
    let request;
    try {
      request = JSON.parse(frame.payload.toString('utf8'));
    } catch (err) {
      this._safeWrite(socket, corrId, new Error('invalid request payload'));
      return;
    }
    const marker = {};
    this._pending.add(marker);
    try {
      const handler = this._handlers.get(request.method);
      if (!handler) {
        throw new Error(`unknown method: ${request.method}`);
      }
      const result = await handler(request.params);
      if (!socket.destroyed) {
        const payload = Buffer.from(JSON.stringify(result === undefined ? null : result));
        socket.write(encodeFrame(MESSAGE_TYPES.RESPONSE, corrId, payload));
      }
    } catch (err) {
      this._safeWrite(socket, corrId, err);
    } finally {
      this._pending.delete(marker);
    }
  }

  _safeWrite(socket, corrId, err) {
    if (socket.destroyed) return;
    const payload = Buffer.from(JSON.stringify({ message: err.message }));
    socket.write(encodeFrame(MESSAGE_TYPES.ERROR, corrId, payload));
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

  async close(deadlineMs = 5000) {
    this._closing = true;
    const closed = new Promise((resolve) => this._server.close(() => resolve()));
    const start = Date.now();
    while (this._pending.size > 0 && Date.now() - start < deadlineMs) {
      await new Promise((resolve) => setTimeout(resolve, 15));
    }
    for (const socket of this._connections) {
      socket.destroy();
    }
    this._connections.clear();
    await closed;
  }
}

module.exports = { RpcServer };
