'use strict';

const net = require('net');
const { MESSAGE_TYPES, encodeFrame, FrameDecoder } = require('./framing.js');

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

class RpcClient {
  constructor({ host = '127.0.0.1', port, maxFrameSize } = {}) {
    this._host = host;
    this._port = port;
    this._maxFrameSize = maxFrameSize;
    this._socket = null;
    this._decoder = null;
    this._connected = false;
    this._connecting = null;
    this._nextCorrId = 1;
    this._pending = new Map();
  }

  _ensureConnected() {
    if (this._connected) return Promise.resolve();
    if (this._connecting) return this._connecting;
    this._connecting = new Promise((resolve, reject) => {
      const socket = net.createConnection({ host: this._host, port: this._port });
      const onError = (err) => {
        socket.removeListener('connect', onConnect);
        this._connecting = null;
        reject(err);
      };
      const onConnect = () => {
        socket.removeListener('error', onError);
        this._socket = socket;
        this._decoder = new FrameDecoder(this._maxFrameSize);
        this._connected = true;
        this._connecting = null;
        socket.on('data', (chunk) => this._onData(chunk));
        socket.on('close', () => this._onClose());
        socket.on('error', () => {});
        resolve();
      };
      socket.once('connect', onConnect);
      socket.once('error', onError);
    });
    return this._connecting;
  }

  _onData(chunk) {
    try {
      this._decoder.append(chunk);
      let frame;
      while ((frame = this._decoder.decodeNext()) !== null) {
        this._handleFrame(frame);
      }
    } catch (err) {
      this._socket.destroy();
    }
  }

  _handleFrame(frame) {
    const entry = this._pending.get(frame.corrId);
    if (!entry) return;
    clearTimeout(entry.timer);
    this._pending.delete(frame.corrId);
    if (frame.type === MESSAGE_TYPES.RESPONSE) {
      entry.resolve(JSON.parse(frame.payload.toString('utf8')));
    } else {
      let message = 'rpc error';
      try {
        message = JSON.parse(frame.payload.toString('utf8')).message || message;
      } catch (err) {}
      entry.reject(new Error(message));
    }
  }

  _onClose() {
    this._connected = false;
    this._socket = null;
    this._decoder = null;
    for (const entry of this._pending.values()) {
      clearTimeout(entry.timer);
      entry.reject(new Error('connection closed'));
    }
    this._pending.clear();
  }

  async call(method, params, { timeoutMs = 2000, retries = 2, backoffMs = 50 } = {}) {
    let lastErr;
    for (let attempt = 0; attempt <= retries; attempt++) {
      try {
        await this._ensureConnected();
      } catch (err) {
        lastErr = err;
        if (attempt < retries) {
          await sleep(backoffMs * Math.pow(2, attempt));
          continue;
        }
        throw err;
      }
      return this._callOnce(method, params, timeoutMs);
    }
    throw lastErr;
  }

  _callOnce(method, params, timeoutMs) {
    return new Promise((resolve, reject) => {
      const corrId = this._nextCorrId++;
      const payload = Buffer.from(JSON.stringify({ method, params }));
      const timer = setTimeout(() => {
        this._pending.delete(corrId);
        reject(new Error(`rpc call timed out: ${method}`));
      }, timeoutMs);
      this._pending.set(corrId, { resolve, reject, timer });
      try {
        this._socket.write(encodeFrame(MESSAGE_TYPES.REQUEST, corrId, payload));
      } catch (err) {
        clearTimeout(timer);
        this._pending.delete(corrId);
        reject(err);
      }
    });
  }

  close() {
    return new Promise((resolve) => {
      if (!this._socket) {
        resolve();
        return;
      }
      this._socket.once('close', () => resolve());
      this._socket.end();
    });
  }
}

module.exports = { RpcClient };
