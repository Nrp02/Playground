'use strict';

class RedisStore {
  constructor(now = () => Date.now()) {
    this._now = now;
    this._data = new Map();
    this._expiresAt = new Map();
  }

  _isExpired(key) {
    const expiresAt = this._expiresAt.get(key);
    if (expiresAt === undefined) {
      return false;
    }
    return this._now() >= expiresAt;
  }

  _evictIfExpired(key) {
    if (this._isExpired(key)) {
      this._data.delete(key);
      this._expiresAt.delete(key);
      return true;
    }
    return false;
  }

  set(key, value) {
    this._data.set(key, value);
    this._expiresAt.delete(key);
  }

  get(key) {
    this._evictIfExpired(key);
    if (!this._data.has(key)) {
      return null;
    }
    return this._data.get(key);
  }

  del(key) {
    this._evictIfExpired(key);
    const existed = this._data.has(key);
    this._data.delete(key);
    this._expiresAt.delete(key);
    return existed ? 1 : 0;
  }

  expire(key, seconds) {
    this._evictIfExpired(key);
    if (!this._data.has(key)) {
      return 0;
    }
    this._expiresAt.set(key, this._now() + seconds * 1000);
    return 1;
  }

  ttl(key) {
    this._evictIfExpired(key);
    if (!this._data.has(key)) {
      return -2;
    }
    const expiresAt = this._expiresAt.get(key);
    if (expiresAt === undefined) {
      return -1;
    }
    const remainingMs = expiresAt - this._now();
    return Math.max(0, Math.ceil(remainingMs / 1000));
  }
}

class CommandHandler {
  constructor(store = new RedisStore()) {
    this.store = store;
    this._commands = {
      PING: this._ping.bind(this),
      SET: this._set.bind(this),
      GET: this._get.bind(this),
      DEL: this._del.bind(this),
      EXPIRE: this._expire.bind(this),
      TTL: this._ttl.bind(this),
    };
  }

  execute(argv) {
    if (!Array.isArray(argv) || argv.length === 0) {
      return { type: 'error', value: 'ERR empty command' };
    }
    const name = String(argv[0]).toUpperCase();
    const handler = this._commands[name];
    if (!handler) {
      return { type: 'error', value: `ERR unknown command '${name}'` };
    }
    try {
      return handler(argv.slice(1));
    } catch (err) {
      return { type: 'error', value: `ERR ${err.message}` };
    }
  }

  _ping(args) {
    if (args.length === 0) {
      return { type: 'simple', value: 'PONG' };
    }
    return { type: 'bulk', value: String(args[0]) };
  }

  _set(args) {
    if (args.length < 2) {
      return { type: 'error', value: "ERR wrong number of arguments for 'SET' command" };
    }
    const [key, value] = args;
    this.store.set(String(key), String(value));
    return { type: 'simple', value: 'OK' };
  }

  _get(args) {
    if (args.length !== 1) {
      return { type: 'error', value: "ERR wrong number of arguments for 'GET' command" };
    }
    const value = this.store.get(String(args[0]));
    return { type: 'bulk', value };
  }

  _del(args) {
    if (args.length < 1) {
      return { type: 'error', value: "ERR wrong number of arguments for 'DEL' command" };
    }
    let count = 0;
    for (const key of args) {
      count += this.store.del(String(key));
    }
    return { type: 'integer', value: count };
  }

  _expire(args) {
    if (args.length !== 2) {
      return { type: 'error', value: "ERR wrong number of arguments for 'EXPIRE' command" };
    }
    const seconds = Number(args[1]);
    if (!Number.isFinite(seconds)) {
      return { type: 'error', value: 'ERR value is not an integer or out of range' };
    }
    const result = this.store.expire(String(args[0]), seconds);
    return { type: 'integer', value: result };
  }

  _ttl(args) {
    if (args.length !== 1) {
      return { type: 'error', value: "ERR wrong number of arguments for 'TTL' command" };
    }
    const result = this.store.ttl(String(args[0]));
    return { type: 'integer', value: result };
  }
}

module.exports = { RedisStore, CommandHandler };
