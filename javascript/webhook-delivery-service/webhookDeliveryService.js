'use strict';

const crypto = require('crypto');

function computeSignature(secret, payload) {
  return crypto.createHmac('sha256', secret).update(payload).digest('hex');
}

function verifySignature(secret, payload, signature) {
  const expected = computeSignature(secret, payload);
  const expectedBuf = Buffer.from(expected, 'hex');
  const actualBuf = Buffer.from(signature, 'hex');
  if (expectedBuf.length !== actualBuf.length) {
    return false;
  }
  return crypto.timingSafeEqual(expectedBuf, actualBuf);
}

function defaultBackoff(attempt, baseDelayMs, maxDelayMs) {
  return Math.min(baseDelayMs * 2 ** (attempt - 1), maxDelayMs);
}

class WebhookDeliveryService {
  constructor({
    transport,
    maxAttempts = 5,
    baseDelayMs = 100,
    maxDelayMs = 5000,
    sleepFn = (ms) => new Promise((resolve) => setTimeout(resolve, ms)),
    idFn = () => crypto.randomUUID(),
  } = {}) {
    if (typeof transport !== 'function') {
      throw new TypeError('transport must be a function');
    }
    this._transport = transport;
    this._maxAttempts = maxAttempts;
    this._baseDelayMs = baseDelayMs;
    this._maxDelayMs = maxDelayMs;
    this._sleepFn = sleepFn;
    this._idFn = idFn;
    this._subscribers = new Map();
    this._deliveries = new Map();
    this._deadLetterQueue = [];
  }

  register(url, secret) {
    if (!url || !secret) {
      throw new RangeError('url and secret are required');
    }
    const subscriberId = this._idFn();
    this._subscribers.set(subscriberId, { url, secret });
    return subscriberId;
  }

  unregister(subscriberId) {
    return this._subscribers.delete(subscriberId);
  }

  subscriberCount() {
    return this._subscribers.size;
  }

  async deliver(event) {
    if (!event || !event.id) {
      throw new RangeError('event must have an id');
    }
    const records = [];
    for (const [subscriberId, subscriber] of this._subscribers) {
      records.push(await this._deliverToSubscriber(subscriberId, subscriber, event));
    }
    return records;
  }

  async _deliverToSubscriber(subscriberId, subscriber, event) {
    const dedupeKey = `${subscriberId}:${event.id}`;
    const existing = this._deliveries.get(dedupeKey);
    if (existing) {
      return { ...existing, deduplicated: true };
    }

    const deliveryId = this._idFn();
    const payload = JSON.stringify(event);
    const signature = computeSignature(subscriber.secret, payload);
    const attemptLog = [];

    let record;
    for (let attempt = 1; attempt <= this._maxAttempts; attempt += 1) {
      try {
        const response = await this._transport({
          url: subscriber.url,
          headers: {
            'X-Webhook-Id': deliveryId,
            'X-Webhook-Signature': signature,
          },
          body: payload,
        });
        const status = response && typeof response.status === 'number' ? response.status : 0;
        if (status >= 200 && status < 300) {
          attemptLog.push({ attempt, status, ok: true });
          record = {
            subscriberId,
            deliveryId,
            eventId: event.id,
            status: 'delivered',
            attempts: attempt,
            attemptLog,
            deduplicated: false,
          };
          break;
        }
        attemptLog.push({ attempt, status, ok: false });
      } catch (err) {
        attemptLog.push({ attempt, error: err.message, ok: false });
      }

      if (attempt < this._maxAttempts) {
        const delay = defaultBackoff(attempt, this._baseDelayMs, this._maxDelayMs);
        await this._sleepFn(delay);
      }
    }

    if (!record) {
      record = {
        subscriberId,
        deliveryId,
        eventId: event.id,
        status: 'dead-letter',
        attempts: this._maxAttempts,
        attemptLog,
        deduplicated: false,
      };
      this._deadLetterQueue.push(record);
    }

    this._deliveries.set(dedupeKey, record);
    return record;
  }

  getDeadLetterQueue() {
    return this._deadLetterQueue.slice();
  }

  getDelivery(subscriberId, eventId) {
    return this._deliveries.get(`${subscriberId}:${eventId}`);
  }
}

module.exports = {
  WebhookDeliveryService,
  computeSignature,
  verifySignature,
};
