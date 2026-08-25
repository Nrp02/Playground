'use strict';

const assert = require('assert');
const {
  encodeSimpleString,
  encodeError,
  encodeInteger,
  encodeBulkString,
  encodeArray,
  RespDecoder,
} = require('./resp.js');

let passCount = 0;
let failCount = 0;

function test(name, fn) {
  try {
    fn();
    passCount++;
    console.log(`  PASS  ${name}`);
  } catch (err) {
    failCount++;
    console.log(`  FAIL  ${name}`);
    console.log(`        ${err.message}`);
  }
}

test('encodes a simple string', () => {
  assert.strictEqual(encodeSimpleString('OK'), '+OK\r\n');
});

test('encodes an error', () => {
  assert.strictEqual(encodeError('ERR bad thing'), '-ERR bad thing\r\n');
});

test('encodes an integer', () => {
  assert.strictEqual(encodeInteger(123), ':123\r\n');
});

test('encodes a bulk string', () => {
  assert.strictEqual(encodeBulkString('hello'), '$5\r\nhello\r\n');
});

test('encodes a null bulk string', () => {
  assert.strictEqual(encodeBulkString(null), '$-1\r\n');
});

test('encodes an array of bulk strings', () => {
  const encoded = encodeArray([
    { type: 'bulk', value: 'foo' },
    { type: 'bulk', value: 'bar' },
  ]);
  assert.strictEqual(encoded, '*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n');
});

test('decodes a simple string', () => {
  const decoder = new RespDecoder();
  decoder.append(Buffer.from('+OK\r\n'));
  const result = decoder.decodeNext();
  assert.deepStrictEqual(result, { type: 'simple', value: 'OK' });
});

test('decodes an error', () => {
  const decoder = new RespDecoder();
  decoder.append(Buffer.from('-ERR broke\r\n'));
  const result = decoder.decodeNext();
  assert.deepStrictEqual(result, { type: 'error', value: 'ERR broke' });
});

test('decodes an integer', () => {
  const decoder = new RespDecoder();
  decoder.append(Buffer.from(':42\r\n'));
  const result = decoder.decodeNext();
  assert.deepStrictEqual(result, { type: 'integer', value: 42 });
});

test('decodes a bulk string', () => {
  const decoder = new RespDecoder();
  decoder.append(Buffer.from('$5\r\nhello\r\n'));
  const result = decoder.decodeNext();
  assert.deepStrictEqual(result, { type: 'bulk', value: 'hello' });
});

test('decodes a null bulk string', () => {
  const decoder = new RespDecoder();
  decoder.append(Buffer.from('$-1\r\n'));
  const result = decoder.decodeNext();
  assert.deepStrictEqual(result, { type: 'bulk', value: null });
});

test('decodes an array of bulk strings representing a client command', () => {
  const decoder = new RespDecoder();
  decoder.append(Buffer.from('*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n'));
  const result = decoder.decodeNext();
  assert.deepStrictEqual(result, {
    type: 'array',
    value: [
      { type: 'bulk', value: 'foo' },
      { type: 'bulk', value: 'bar' },
    ],
  });
});

test('returns null when the buffer holds an incomplete frame', () => {
  const decoder = new RespDecoder();
  decoder.append(Buffer.from('$5\r\nhel'));
  assert.strictEqual(decoder.decodeNext(), null);
  decoder.append(Buffer.from('lo\r\n'));
  const result = decoder.decodeNext();
  assert.deepStrictEqual(result, { type: 'bulk', value: 'hello' });
});

test('decodes multiple frames appended across separate chunks', () => {
  const decoder = new RespDecoder();
  decoder.append(Buffer.from('+OK\r\n:1\r\n'));
  assert.deepStrictEqual(decoder.decodeNext(), { type: 'simple', value: 'OK' });
  assert.deepStrictEqual(decoder.decodeNext(), { type: 'integer', value: 1 });
  assert.strictEqual(decoder.decodeNext(), null);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
