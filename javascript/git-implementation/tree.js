'use strict';

const { hashObject, writeObject, readObject } = require('./objects.js');

function sortKey(entry) {
  return entry.mode === '40000' ? `${entry.name}/` : entry.name;
}

function sortEntries(entries) {
  return [...entries].sort((a, b) => {
    const ka = sortKey(a);
    const kb = sortKey(b);
    if (ka < kb) return -1;
    if (ka > kb) return 1;
    return 0;
  });
}

function buildTreeContent(entries) {
  const sorted = sortEntries(entries);
  const parts = sorted.map((entry) => {
    const header = Buffer.from(`${entry.mode} ${entry.name}\0`);
    const shaBytes = Buffer.from(entry.sha1Hex, 'hex');
    return Buffer.concat([header, shaBytes]);
  });
  return Buffer.concat(parts);
}

function hashTree(entries) {
  return hashObject('tree', buildTreeContent(entries));
}

function writeTree(gitDir, entries) {
  return writeObject(gitDir, 'tree', buildTreeContent(entries));
}

function parseTreeContent(content) {
  const entries = [];
  let offset = 0;
  while (offset < content.length) {
    const spaceIndex = content.indexOf(0x20, offset);
    const mode = content.slice(offset, spaceIndex).toString('utf8');
    const nullIndex = content.indexOf(0, spaceIndex + 1);
    const name = content.slice(spaceIndex + 1, nullIndex).toString('utf8');
    const shaBytes = content.slice(nullIndex + 1, nullIndex + 21);
    entries.push({ mode, name, sha1Hex: shaBytes.toString('hex') });
    offset = nullIndex + 21;
  }
  return entries;
}

function readTree(gitDir, sha1Hex) {
  const { type, content } = readObject(gitDir, sha1Hex);
  if (type !== 'tree') {
    throw new Error(`object ${sha1Hex} is not a tree (found ${type})`);
  }
  return parseTreeContent(content);
}

module.exports = {
  sortEntries,
  buildTreeContent,
  hashTree,
  writeTree,
  parseTreeContent,
  readTree,
};
