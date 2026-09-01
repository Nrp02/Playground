'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { ObjectStorage } = require('./storage.js');
const { MultipartManager, MIN_PART_SIZE } = require('./multipart.js');

function main() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'object-storage-demo-'));
  let now = 1000;
  const clock = () => now;
  const storage = new ObjectStorage(root, { clock });
  const multipart = new MultipartManager(storage);

  try {
    console.log('=== bucket + dedup ===');
    storage.createBucket('photos', { versioning: false });
    storage.putObject('photos', 'a.txt', Buffer.from('hello world'), { contentType: 'text/plain' });
    storage.putObject('photos', 'b.txt', Buffer.from('hello world'), { contentType: 'text/plain' });
    console.log(`physical chunks on disk: ${storage.chunkStore.size()} (should be 1 despite 2 objects)`);

    console.log('\n=== prefix/delimiter listing with pagination ===');
    storage.createBucket('logs');
    const keys = ['2024/01/a.log', '2024/01/b.log', '2024/02/a.log', '2024/03/a.log', 'readme.txt'];
    for (const k of keys) {
      storage.putObject('logs', k, Buffer.from(k));
    }
    const page1 = storage.listObjects('logs', { prefix: '2024/', delimiter: '/', maxKeys: 2 });
    console.log('page1', page1);
    const page2 = storage.listObjects('logs', {
      prefix: '2024/',
      delimiter: '/',
      maxKeys: 2,
      continuationToken: page1.nextContinuationToken,
    });
    console.log('page2', page2);

    console.log('\n=== multipart upload, parts out of order ===');
    storage.createBucket('uploads');
    const partA = Buffer.alloc(MIN_PART_SIZE, 'A');
    const partB = Buffer.alloc(MIN_PART_SIZE, 'B');
    const partC = Buffer.alloc(10, 'C');
    const uploadId = multipart.createMultipartUpload('uploads', 'big.bin', { contentType: 'application/octet-stream' });
    const uploadedC = multipart.uploadPart(uploadId, 3, partC);
    const uploadedA = multipart.uploadPart(uploadId, 1, partA);
    const uploadedB = multipart.uploadPart(uploadId, 2, partB);
    const finalObject = multipart.completeMultipartUpload(uploadId, [
      { partNumber: 1, etag: uploadedA.etag },
      { partNumber: 2, etag: uploadedB.etag },
      { partNumber: 3, etag: uploadedC.etag },
    ]);
    console.log(`multipart object etag: ${finalObject.etag}, size: ${finalObject.size}`);
    const assembled = storage.getObject('uploads', 'big.bin');
    const expected = Buffer.concat([partA, partB, partC]);
    console.log(`assembled bytes match expected: ${assembled.body.equals(expected)}`);

    console.log('\n=== versioning + delete marker ===');
    storage.createBucket('docs', { versioning: true });
    now = 2000;
    const v1 = storage.putObject('docs', 'report.txt', Buffer.from('draft one'));
    now = 3000;
    const v2 = storage.putObject('docs', 'report.txt', Buffer.from('draft two'));
    console.log(`latest: ${storage.getObject('docs', 'report.txt').body.toString()}`);
    console.log(`v1 by id: ${storage.getObject('docs', 'report.txt', { versionId: v1.versionId }).body.toString()}`);
    now = 4000;
    storage.deleteObject('docs', 'report.txt');
    try {
      storage.getObject('docs', 'report.txt');
      console.log('ERROR: expected delete marker to hide object');
    } catch (err) {
      console.log(`getObject after delete correctly throws: ${err.name}`);
    }
    const versions = storage.listObjectVersions('docs', { prefix: 'report.txt' });
    console.log(`listObjectVersions shows ${versions.length} entries (2 content + 1 delete marker)`);

    console.log('\n=== quota enforcement ===');
    storage.createBucket('quota-bucket', { quotaBytes: 10 });
    storage.putObject('quota-bucket', 'small.txt', Buffer.from('12345'));
    try {
      storage.putObject('quota-bucket', 'toobig.txt', Buffer.from('1234567890abcdef'));
      console.log('ERROR: expected quota rejection');
    } catch (err) {
      console.log(`quota rejection correctly thrown: ${err.name}`);
    }
    const afterQuotaFail = storage.listObjects('quota-bucket');
    console.log(`bucket keys after failed put: ${JSON.stringify(afterQuotaFail.keys)}`);

    console.log('\n=== corruption detection ===');
    storage.createBucket('fragile');
    const put = storage.putObject('fragile', 'file.bin', Buffer.from('untouched content'));
    fs.writeFileSync(storage.chunkStore.chunkPath(put.etag), Buffer.from('tampered content'));
    try {
      storage.getObject('fragile', 'file.bin');
      console.log('ERROR: expected integrity failure');
    } catch (err) {
      console.log(`corruption correctly detected: ${err.name}`);
    }

    console.log('\n=== lifecycle expiry ===');
    now = 10000;
    storage.createBucket('temp-data', { lifecycleRules: [{ prefix: 'cache/', maxAgeMs: 500 }] });
    storage.putObject('temp-data', 'cache/entry1', Buffer.from('x'));
    storage.putObject('temp-data', 'keep/entry2', Buffer.from('y'));
    now = 10500;
    const swept = storage.sweepExpired();
    console.log(`swept ${swept} expired version(s)`);
    console.log(`remaining keys: ${JSON.stringify(storage.listObjects('temp-data').keys)}`);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
    console.log('\ncleaned up temp directory');
  }
}

main();
