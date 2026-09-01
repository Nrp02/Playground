'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { ObjectStorage } = require('./storage.js');
const { MultipartManager, MIN_PART_SIZE } = require('./multipart.js');

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
    console.log(`        ${err.stack}`);
  }
}

function withTempDir(fn) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'multipart-test-'));
  try {
    fn(dir);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

function makeSetup(dir) {
  const storage = new ObjectStorage(dir);
  const multipart = new MultipartManager(storage);
  storage.createBucket('b');
  return { storage, multipart };
}

test('out-of-order part assembly equals a single put of the same bytes', () => {
  withTempDir((dir) => {
    const { storage, multipart } = makeSetup(dir);
    const p1 = Buffer.alloc(MIN_PART_SIZE, 1);
    const p2 = Buffer.alloc(MIN_PART_SIZE, 2);
    const p3 = Buffer.alloc(20, 3);
    const whole = Buffer.concat([p1, p2, p3]);

    const uploadId = multipart.createMultipartUpload('b', 'obj');
    const u3 = multipart.uploadPart(uploadId, 3, p3);
    const u1 = multipart.uploadPart(uploadId, 1, p1);
    const u2 = multipart.uploadPart(uploadId, 2, p2);
    multipart.completeMultipartUpload(uploadId, [
      { partNumber: 2, etag: u2.etag },
      { partNumber: 1, etag: u1.etag },
      { partNumber: 3, etag: u3.etag },
    ]);

    storage.createBucket('control');
    storage.putObject('control', 'obj', whole);

    const assembled = storage.getObject('b', 'obj');
    const single = storage.getObject('control', 'obj');
    assert.ok(assembled.body.equals(whole));
    assert.strictEqual(assembled.body.length, single.body.length);
  });
});

test('re-uploading a part number replaces the previous chunk', () => {
  withTempDir((dir) => {
    const { storage, multipart } = makeSetup(dir);
    const uploadId = multipart.createMultipartUpload('b', 'obj');
    const first = multipart.uploadPart(uploadId, 1, Buffer.alloc(MIN_PART_SIZE, 'x'));
    const second = multipart.uploadPart(uploadId, 1, Buffer.alloc(MIN_PART_SIZE, 'y'));
    assert.notStrictEqual(first.etag, second.etag);
    assert.strictEqual(storage.chunkStore.refcount(first.etag), 0);
    const lastPart = multipart.uploadPart(uploadId, 2, Buffer.from('tail'));
    const version = multipart.completeMultipartUpload(uploadId, [
      { partNumber: 1, etag: second.etag },
      { partNumber: 2, etag: lastPart.etag },
    ]);
    assert.strictEqual(storage.getObject('b', 'obj').body.length, version.size);
  });
});

test('completeMultipartUpload rejects a mismatched etag', () => {
  withTempDir((dir) => {
    const { multipart } = makeSetup(dir);
    const uploadId = multipart.createMultipartUpload('b', 'obj');
    multipart.uploadPart(uploadId, 1, Buffer.from('only part'));
    assert.throws(() => {
      multipart.completeMultipartUpload(uploadId, [{ partNumber: 1, etag: 'wrong-etag' }]);
    }, RangeError);
  });
});

test('completeMultipartUpload rejects a missing part number', () => {
  withTempDir((dir) => {
    const { multipart } = makeSetup(dir);
    const uploadId = multipart.createMultipartUpload('b', 'obj');
    const u1 = multipart.uploadPart(uploadId, 1, Buffer.from('present'));
    assert.throws(() => {
      multipart.completeMultipartUpload(uploadId, [
        { partNumber: 1, etag: u1.etag },
        { partNumber: 2, etag: 'does-not-exist' },
      ]);
    }, RangeError);
  });
});

test('minimum part size enforced except for the last part', () => {
  withTempDir((dir) => {
    const { multipart } = makeSetup(dir);
    const uploadId = multipart.createMultipartUpload('b', 'obj');
    const u1 = multipart.uploadPart(uploadId, 1, Buffer.alloc(10, 'a'));
    const u2 = multipart.uploadPart(uploadId, 2, Buffer.alloc(10, 'b'));
    assert.throws(() => {
      multipart.completeMultipartUpload(uploadId, [
        { partNumber: 1, etag: u1.etag },
        { partNumber: 2, etag: u2.etag },
      ]);
    }, RangeError);
  });
});

test('minimum part size does not apply to a single-part or the last part', () => {
  withTempDir((dir) => {
    const { storage, multipart } = makeSetup(dir);
    const uploadId = multipart.createMultipartUpload('b', 'obj');
    const u1 = multipart.uploadPart(uploadId, 1, Buffer.alloc(MIN_PART_SIZE, 'a'));
    const u2 = multipart.uploadPart(uploadId, 2, Buffer.from('short tail'));
    multipart.completeMultipartUpload(uploadId, [
      { partNumber: 1, etag: u1.etag },
      { partNumber: 2, etag: u2.etag },
    ]);
    assert.strictEqual(storage.getObject('b', 'obj').body.length, MIN_PART_SIZE + 10);
  });
});

test('abortMultipartUpload leaves no staged chunks behind', () => {
  withTempDir((dir) => {
    const { storage, multipart } = makeSetup(dir);
    const uploadId = multipart.createMultipartUpload('b', 'obj');
    multipart.uploadPart(uploadId, 1, Buffer.from('part one'));
    multipart.uploadPart(uploadId, 2, Buffer.from('part two'));
    assert.strictEqual(storage.chunkStore.size(), 2);
    multipart.abortMultipartUpload(uploadId);
    assert.strictEqual(storage.chunkStore.size(), 0);
    assert.throws(() => multipart.abortMultipartUpload(uploadId), RangeError);
  });
});

test('etag has the -N multipart suffix convention', () => {
  withTempDir((dir) => {
    const { multipart } = makeSetup(dir);
    const uploadId = multipart.createMultipartUpload('b', 'obj');
    const u1 = multipart.uploadPart(uploadId, 1, Buffer.alloc(MIN_PART_SIZE, 'a'));
    const u2 = multipart.uploadPart(uploadId, 2, Buffer.from('tail'));
    const version = multipart.completeMultipartUpload(uploadId, [
      { partNumber: 1, etag: u1.etag },
      { partNumber: 2, etag: u2.etag },
    ]);
    assert.match(version.etag, /-2$/);
  });
});

test('quota rejection on complete leaves the upload staged for retry', () => {
  withTempDir((dir) => {
    const storage = new ObjectStorage(dir);
    const multipart = new MultipartManager(storage);
    storage.createBucket('b', { quotaBytes: 5 });
    const uploadId = multipart.createMultipartUpload('b', 'obj');
    const u1 = multipart.uploadPart(uploadId, 1, Buffer.from('this is way too big'));
    assert.throws(() => {
      multipart.completeMultipartUpload(uploadId, [{ partNumber: 1, etag: u1.etag }]);
    });
    multipart.abortMultipartUpload(uploadId);
    assert.strictEqual(storage.chunkStore.size(), 0);
  });
});

test('cleanup leaves no temp directories behind', () => {
  const before = fs.readdirSync(os.tmpdir()).filter((n) => n.startsWith('multipart-test-'));
  assert.strictEqual(before.length, 0);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
