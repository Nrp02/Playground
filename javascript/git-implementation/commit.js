'use strict';

const { writeObject, readObject } = require('./objects.js');

function buildCommitContent({ treeSha1, parentSha1, author, committer, message }) {
  const lines = [];
  lines.push(`tree ${treeSha1}`);
  if (parentSha1) {
    lines.push(`parent ${parentSha1}`);
  }
  lines.push(`author ${author}`);
  lines.push(`committer ${committer || author}`);
  lines.push('');
  lines.push(message);
  return Buffer.from(lines.join('\n'));
}

function writeCommit(gitDir, options) {
  return writeObject(gitDir, 'commit', buildCommitContent(options));
}

function parseCommitContent(content) {
  const text = content.toString('utf8');
  const separatorIndex = text.indexOf('\n\n');
  const headerText = separatorIndex === -1 ? text : text.slice(0, separatorIndex);
  const message = separatorIndex === -1 ? '' : text.slice(separatorIndex + 2);
  const headerLines = headerText.split('\n').filter((line) => line.length > 0);
  const commit = { tree: null, parent: null, author: null, committer: null, message };
  for (const line of headerLines) {
    const spaceIndex = line.indexOf(' ');
    const key = line.slice(0, spaceIndex);
    const value = line.slice(spaceIndex + 1);
    if (key === 'tree') commit.tree = value;
    else if (key === 'parent') commit.parent = value;
    else if (key === 'author') commit.author = value;
    else if (key === 'committer') commit.committer = value;
  }
  return commit;
}

function readCommit(gitDir, sha1Hex) {
  const { type, content } = readObject(gitDir, sha1Hex);
  if (type !== 'commit') {
    throw new Error(`object ${sha1Hex} is not a commit (found ${type})`);
  }
  return parseCommitContent(content);
}

module.exports = {
  buildCommitContent,
  writeCommit,
  parseCommitContent,
  readCommit,
};
