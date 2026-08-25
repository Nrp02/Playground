'use strict';

const crypto = require('crypto');
const zlib = require('zlib');
const fs = require('fs');
const path = require('path');

function toBuffer(content) {
  return Buffer.isBuffer(content) ? content : Buffer.from(content);
}

function frameObject(type, content) {
  const body = toBuffer(content);
  const header = Buffer.from(`${type} ${body.length}\0`);
  return Buffer.concat([header, body]);
}

function hashObject(type, content) {
  const frame = frameObject(type, content);
  return crypto.createHash('sha1').update(frame).digest('hex');
}

function objectPath(gitDir, sha1Hex) {
  const dir = path.join(gitDir, 'objects', sha1Hex.slice(0, 2));
  const file = sha1Hex.slice(2);
  return { dir, file: path.join(dir, file) };
}

function writeObject(gitDir, type, content) {
  const sha1Hex = hashObject(type, content);
  const { dir, file } = objectPath(gitDir, sha1Hex);
  fs.mkdirSync(dir, { recursive: true });
  if (!fs.existsSync(file)) {
    const frame = frameObject(type, content);
    const compressed = zlib.deflateSync(frame);
    fs.writeFileSync(file, compressed);
  }
  return sha1Hex;
}

function readObject(gitDir, sha1Hex) {
  const { file } = objectPath(gitDir, sha1Hex);
  const compressed = fs.readFileSync(file);
  const frame = zlib.inflateSync(compressed);
  const nullIndex = frame.indexOf(0);
  const header = frame.slice(0, nullIndex).toString('utf8');
  const [type, lengthStr] = header.split(' ');
  const length = parseInt(lengthStr, 10);
  const content = frame.slice(nullIndex + 1, nullIndex + 1 + length);
  return { type, content };
}

function init(dir) {
  const gitDir = path.join(dir, '.git');
  fs.mkdirSync(path.join(gitDir, 'objects'), { recursive: true });
  fs.mkdirSync(path.join(gitDir, 'refs', 'heads'), { recursive: true });
  fs.mkdirSync(path.join(gitDir, 'refs', 'tags'), { recursive: true });
  return gitDir;
}

module.exports = {
  hashObject,
  writeObject,
  readObject,
  init,
  objectPath,
};
