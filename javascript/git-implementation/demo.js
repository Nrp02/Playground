'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { init, hashObject, writeObject, readObject } = require('./objects.js');
const { writeTree, readTree } = require('./tree.js');
const { writeCommit, readCommit } = require('./commit.js');

function main() {
  const workDir = fs.mkdtempSync(path.join(os.tmpdir(), 'git-implementation-demo-'));
  try {
    const gitDir = init(workDir);
    console.log(`initialized repo at ${gitDir}`);

    const knownHash = hashObject('blob', 'hello world\n');
    console.log(`known blob hash for "hello world\\n": ${knownHash}`);

    const readmeSha = writeObject(gitDir, 'blob', 'hello from git-implementation demo\n');
    const notesSha = writeObject(gitDir, 'blob', 'some notes about this playground project\n');
    console.log(`wrote blob README.md -> ${readmeSha}`);
    console.log(`wrote blob NOTES.md -> ${notesSha}`);

    const treeSha = writeTree(gitDir, [
      { mode: '100644', name: 'README.md', sha1Hex: readmeSha },
      { mode: '100644', name: 'NOTES.md', sha1Hex: notesSha },
    ]);
    console.log(`wrote tree -> ${treeSha}`);

    const author = 'Demo User <demo@example.com> 1700000000 +0000';
    const commitSha = writeCommit(gitDir, {
      treeSha1: treeSha,
      author,
      committer: author,
      message: 'Initial commit from git-implementation demo\n',
    });
    console.log(`wrote commit -> ${commitSha}`);

    const readBackReadme = readObject(gitDir, readmeSha);
    const readBackNotes = readObject(gitDir, notesSha);
    const readBackTree = readTree(gitDir, treeSha);
    const readBackCommit = readCommit(gitDir, commitSha);

    console.log('read back README.md:', readBackReadme.content.toString('utf8').trim());
    console.log('read back NOTES.md:', readBackNotes.content.toString('utf8').trim());
    console.log('read back tree entries:', readBackTree.map((e) => e.name).join(', '));
    console.log('read back commit tree:', readBackCommit.tree);
    console.log('read back commit message:', readBackCommit.message.trim());

    const roundTripOk =
      readBackReadme.content.toString('utf8') === 'hello from git-implementation demo\n' &&
      readBackNotes.content.toString('utf8') === 'some notes about this playground project\n' &&
      readBackTree.length === 2 &&
      readBackTree[0].name === 'NOTES.md' &&
      readBackTree[1].name === 'README.md' &&
      readBackCommit.tree === treeSha;

    console.log(`full round trip ok: ${roundTripOk}`);
  } finally {
    fs.rmSync(workDir, { recursive: true, force: true });
    console.log(`cleaned up ${workDir}`);
  }
}

main();
