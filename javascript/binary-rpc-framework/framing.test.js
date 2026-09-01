'use strict';

const assert = require('assert');
const crypto = require('crypto');
const { MESSAGE_TYPES, encodeFrame, FrameDecoder } = require('./framing.js');

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
    console.log(`        ${err.message}`);
  }
}

function randomSplits(buf) {
  const parts = [];
  let offset = 0;
  while (offset < buf.length) {
    const remaining = buf.length - offset;
    const size = remaining === 1 ? 1 : 1 + Math.floor(Math.random() * remaining);
    parts.push(buf.subarray(offset, offset + size));
    offset += size;
  }
  return parts;
}

async function main() {
  await test('round-trips an empty payload', () => {
    const encoded = encodeFrame(MESSAGE_TYPES.REQUEST, 7, Buffer.alloc(0));
    const decoder = new FrameDecoder();
    decoder.append(encoded);
    const frame = decoder.decodeNext();
    assert.strictEqual(frame.type, MESSAGE_TYPES.REQUEST);
    assert.strictEqual(frame.corrId, 7);
    assert.strictEqual(frame.payload.length, 0);
    assert.strictEqual(decoder.decodeNext(), null);
  });

  await test('round-trips randomized payload sizes', () => {
    const sizes = [1, 2, 17, 255, 4096, 65536, 300000];
    for (const size of sizes) {
      const payload = crypto.randomBytes(size);
      const corrId = Math.floor(Math.random() * 0xffffffff);
      const encoded = encodeFrame(MESSAGE_TYPES.RESPONSE, corrId, payload);
      const decoder = new FrameDecoder();
      decoder.append(encoded);
      const frame = decoder.decodeNext();
      assert.strictEqual(frame.type, MESSAGE_TYPES.RESPONSE);
      assert.strictEqual(frame.corrId, corrId);
      assert.ok(frame.payload.equals(payload));
    }
  });

  await test('decodes multiple frames arriving in a single chunk', () => {
    const frames = [
      encodeFrame(MESSAGE_TYPES.REQUEST, 1, Buffer.from('one')),
      encodeFrame(MESSAGE_TYPES.REQUEST, 2, Buffer.from('two')),
      encodeFrame(MESSAGE_TYPES.REQUEST, 3, Buffer.from('three')),
    ];
    const decoder = new FrameDecoder();
    decoder.append(Buffer.concat(frames));
    const decoded = [];
    let frame;
    while ((frame = decoder.decodeNext()) !== null) {
      decoded.push(frame);
    }
    assert.strictEqual(decoded.length, 3);
    assert.strictEqual(decoded[0].payload.toString(), 'one');
    assert.strictEqual(decoded[1].payload.toString(), 'two');
    assert.strictEqual(decoded[2].payload.toString(), 'three');
  });

  await test('decoder yields identical frames under random chunk splits', () => {
    const originalFrames = [];
    const rawBuffers = [];
    for (let i = 0; i < 20; i++) {
      const payload = crypto.randomBytes(Math.floor(Math.random() * 500));
      const corrId = i;
      originalFrames.push({ corrId, payload });
      rawBuffers.push(encodeFrame(MESSAGE_TYPES.REQUEST, corrId, payload));
    }
    const wholeStream = Buffer.concat(rawBuffers);
    for (let trial = 0; trial < 25; trial++) {
      const chunks = randomSplits(wholeStream);
      const decoder = new FrameDecoder();
      const decoded = [];
      for (const chunk of chunks) {
        decoder.append(chunk);
        let frame;
        while ((frame = decoder.decodeNext()) !== null) {
          decoded.push(frame);
        }
      }
      assert.strictEqual(decoded.length, originalFrames.length);
      for (let i = 0; i < decoded.length; i++) {
        assert.strictEqual(decoded[i].corrId, originalFrames[i].corrId);
        assert.ok(decoded[i].payload.equals(originalFrames[i].payload));
      }
    }
  });

  await test('returns null on a partial frame and resumes once complete', () => {
    const encoded = encodeFrame(MESSAGE_TYPES.RESPONSE, 42, Buffer.from('hello world'));
    const decoder = new FrameDecoder();
    decoder.append(encoded.subarray(0, 3));
    assert.strictEqual(decoder.decodeNext(), null);
    decoder.append(encoded.subarray(3, 6));
    assert.strictEqual(decoder.decodeNext(), null);
    decoder.append(encoded.subarray(6));
    const frame = decoder.decodeNext();
    assert.strictEqual(frame.payload.toString(), 'hello world');
  });

  await test('rejects an oversized frame instead of buffering it', () => {
    const decoder = new FrameDecoder(1024);
    const header = Buffer.alloc(4);
    header.writeUInt32BE(5000, 0);
    decoder.append(header);
    assert.throws(() => decoder.decodeNext(), /exceeds maximum size/);
  });

  await test('rejects a corrupt frame whose body is smaller than the header', () => {
    const decoder = new FrameDecoder();
    const header = Buffer.alloc(4);
    header.writeUInt32BE(2, 0);
    decoder.append(header);
    decoder.append(Buffer.from([0, 0]));
    assert.throws(() => decoder.decodeNext(), /corrupt frame/);
  });

  console.log(`\n${passCount} passed, ${failCount} failed`);
  process.exit(failCount === 0 ? 0 : 1);
}

main();
