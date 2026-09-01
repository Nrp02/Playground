'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { ObjectStorage, NoSuchKeyError, QuotaExceededError } = require('./storage.js');
const { IntegrityError } = require('./chunkStore.js');

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
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'storage-test-'));
  try {
    fn(dir);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

function makeStorage(dir, opts) {
  let now = 0;
  const clock = () => now;
  const storage = new ObjectStorage(dir, { clock, ...(opts || {}) });
  return { storage, setNow: (t) => { now = t; } };
}

function bruteForceListing(keys, prefix, delimiter) {
  const matching = keys.filter((k) => k.startsWith(prefix)).sort();
  const resultKeys = [];
  const commonPrefixes = new Set();
  for (const key of matching) {
    if (delimiter) {
      const rest = key.slice(prefix.length);
      const idx = rest.indexOf(delimiter);
      if (idx !== -1) {
        commonPrefixes.add(prefix + rest.slice(0, idx + delimiter.length));
        continue;
      }
    }
    resultKeys.push(key);
  }
  return { keys: resultKeys, commonPrefixes: Array.from(commonPrefixes).sort() };
}

test('putObject/getObject/headObject/deleteObject round-trip', () => {
  withTempDir((dir) => {
    const { storage } = makeStorage(dir);
    storage.createBucket('b');
    storage.putObject('b', 'k', Buffer.from('value'), { contentType: 'text/plain', metadata: { a: '1' } });
    const got = storage.getObject('b', 'k');
    assert.strictEqual(got.body.toString(), 'value');
    assert.strictEqual(got.contentType, 'text/plain');
    assert.strictEqual(got.metadata.a, '1');
    const head = storage.headObject('b', 'k');
    assert.strictEqual(head.body, undefined);
    assert.strictEqual(head.size, 5);
    storage.deleteObject('b', 'k');
    assert.throws(() => storage.getObject('b', 'k'), NoSuchKeyError);
  });
});

test('empty and large and binary objects round-trip', () => {
  withTempDir((dir) => {
    const { storage } = makeStorage(dir);
    storage.createBucket('b');
    storage.putObject('b', 'empty', Buffer.alloc(0));
    assert.strictEqual(storage.getObject('b', 'empty').body.length, 0);

    const large = Buffer.alloc(500000, 7);
    storage.putObject('b', 'large', large);
    assert.ok(storage.getObject('b', 'large').body.equals(large));

    const binary = Buffer.from([0, 255, 16, 1, 2, 254, 0, 0, 9]);
    storage.putObject('b', 'binary', binary);
    assert.ok(storage.getObject('b', 'binary').body.equals(binary));
  });
});

test('getObject supports byte ranges', () => {
  withTempDir((dir) => {
    const { storage } = makeStorage(dir);
    storage.createBucket('b');
    storage.putObject('b', 'k', Buffer.from('0123456789'));
    const result = storage.getObject('b', 'k', { range: [2, 5] });
    assert.strictEqual(result.body.toString(), '2345');
  });
});

test('listing prefix/delimiter/pagination matches a brute-force reference', () => {
  withTempDir((dir) => {
    const { storage } = makeStorage(dir);
    storage.createBucket('b');
    const keys = [];
    const letters = 'abcde';
    for (let i = 0; i < letters.length; i++) {
      for (let j = 0; j < letters.length; j++) {
        const key = `${letters[i]}/${letters[j]}/file.txt`;
        keys.push(key);
        storage.putObject('b', key, Buffer.from(key));
      }
    }
    storage.putObject('b', 'toplevel.txt', Buffer.from('top'));
    keys.push('toplevel.txt');

    for (const prefix of ['', 'a/', 'c/c/']) {
      const expected = bruteForceListing(keys, prefix, '/');
      let allKeys = [];
      let allPrefixes = new Set();
      let token = null;
      let guard = 0;
      do {
        const page = storage.listObjects('b', { prefix, delimiter: '/', maxKeys: 3, continuationToken: token });
        allKeys = allKeys.concat(page.keys);
        page.commonPrefixes.forEach((p) => allPrefixes.add(p));
        token = page.nextContinuationToken;
        guard++;
        assert.ok(guard < 1000, 'pagination did not terminate');
      } while (token);
      assert.deepStrictEqual(allKeys.sort(), expected.keys.sort());
      assert.deepStrictEqual(Array.from(allPrefixes).sort(), expected.commonPrefixes.sort());
    }
  });
});

test('multipart-less large listing without delimiter returns everything', () => {
  withTempDir((dir) => {
    const { storage } = makeStorage(dir);
    storage.createBucket('b');
    for (let i = 0; i < 10; i++) {
      storage.putObject('b', `key-${i}`, Buffer.from(String(i)));
    }
    const page = storage.listObjects('b', { maxKeys: 1000 });
    assert.strictEqual(page.keys.length, 10);
    assert.strictEqual(page.isTruncated, false);
  });
});

test('dedup refcounting: shared chunk survives one delete, disappears after both', () => {
  withTempDir((dir) => {
    const { storage } = makeStorage(dir);
    storage.createBucket('b');
    storage.putObject('b', 'x', Buffer.from('shared payload'));
    storage.putObject('b', 'y', Buffer.from('shared payload'));
    assert.strictEqual(storage.chunkStore.size(), 1);
    storage.deleteObject('b', 'x');
    assert.strictEqual(storage.chunkStore.size(), 1);
    assert.ok(storage.getObject('b', 'y').body.toString() === 'shared payload');
    storage.deleteObject('b', 'y');
    assert.strictEqual(storage.chunkStore.size(), 0);
  });
});

test('versioning: read latest and specific version, delete marker hides latest', () => {
  withTempDir((dir) => {
    const { storage, setNow } = makeStorage(dir);
    storage.createBucket('b', { versioning: true });
    setNow(1);
    const v1 = storage.putObject('b', 'k', Buffer.from('one'));
    setNow(2);
    const v2 = storage.putObject('b', 'k', Buffer.from('two'));
    assert.strictEqual(storage.getObject('b', 'k').body.toString(), 'two');
    assert.strictEqual(storage.getObject('b', 'k', { versionId: v1.versionId }).body.toString(), 'one');
    assert.strictEqual(storage.getObject('b', 'k', { versionId: v2.versionId }).body.toString(), 'two');
    setNow(3);
    storage.deleteObject('b', 'k');
    assert.throws(() => storage.getObject('b', 'k'), NoSuchKeyError);
    assert.strictEqual(storage.getObject('b', 'k', { versionId: v1.versionId }).body.toString(), 'one');
  });
});

test('versioning: listObjectVersions returns newest-first including delete markers', () => {
  withTempDir((dir) => {
    const { storage, setNow } = makeStorage(dir);
    storage.createBucket('b', { versioning: true });
    setNow(1);
    storage.putObject('b', 'k', Buffer.from('one'));
    setNow(2);
    storage.putObject('b', 'k', Buffer.from('two'));
    setNow(3);
    storage.deleteObject('b', 'k');
    const versions = storage.listObjectVersions('b', { prefix: 'k' });
    assert.strictEqual(versions.length, 3);
    assert.strictEqual(versions[0].deleteMarker, true);
    assert.strictEqual(versions[0].isLatest, true);
    assert.strictEqual(versions[1].deleteMarker, false);
    assert.strictEqual(versions[2].deleteMarker, false);
  });
});

test('quota rejection leaves no partial state', () => {
  withTempDir((dir) => {
    const { storage } = makeStorage(dir);
    storage.createBucket('b', { quotaBytes: 10 });
    storage.putObject('b', 'small', Buffer.from('12345'));
    assert.throws(() => storage.putObject('b', 'big', Buffer.from('123456789012')), QuotaExceededError);
    assert.throws(() => storage.getObject('b', 'big'), NoSuchKeyError);
    assert.strictEqual(storage.chunkStore.size(), 1);
    const listing = storage.listObjects('b');
    assert.deepStrictEqual(listing.keys, ['small']);
  });
});

test('quota rejection on overwrite accounts for freed bytes', () => {
  withTempDir((dir) => {
    const { storage } = makeStorage(dir);
    storage.createBucket('b', { quotaBytes: 10 });
    storage.putObject('b', 'k', Buffer.from('1234567890'));
    storage.putObject('b', 'k', Buffer.from('9876543210'));
    assert.strictEqual(storage.getObject('b', 'k').body.toString(), '9876543210');
  });
});

test('corruption is detected on getObject', () => {
  withTempDir((dir) => {
    const { storage } = makeStorage(dir);
    storage.createBucket('b');
    const put = storage.putObject('b', 'k', Buffer.from('original bytes'));
    fs.writeFileSync(storage.chunkStore.chunkPath(put.etag), Buffer.from('corrupted bytes'));
    assert.throws(() => storage.getObject('b', 'k'), IntegrityError);
  });
});

test('lifecycle expiry sweeps at the exact boundary tick', () => {
  withTempDir((dir) => {
    const { storage, setNow } = makeStorage(dir);
    setNow(0);
    storage.createBucket('b', { lifecycleRules: [{ prefix: 'tmp/', maxAgeMs: 100 }] });
    storage.putObject('b', 'tmp/a', Buffer.from('x'));
    storage.putObject('b', 'keep/a', Buffer.from('y'));
    setNow(99);
    storage.sweepExpired();
    assert.deepStrictEqual(storage.listObjects('b').keys.sort(), ['keep/a', 'tmp/a']);
    setNow(100);
    storage.sweepExpired();
    assert.deepStrictEqual(storage.listObjects('b').keys.sort(), ['keep/a']);
    assert.strictEqual(storage.chunkStore.size(), 1);
  });
});

test('lifecycle expiry removes only expired versions, keeps others', () => {
  withTempDir((dir) => {
    const { storage, setNow } = makeStorage(dir);
    setNow(0);
    storage.createBucket('b', { versioning: true, lifecycleRules: [{ prefix: '', maxAgeMs: 50 }] });
    storage.putObject('b', 'k', Buffer.from('old'));
    setNow(40);
    storage.putObject('b', 'k', Buffer.from('new'));
    setNow(50);
    storage.sweepExpired();
    const versions = storage.listObjectVersions('b', { prefix: 'k' });
    assert.strictEqual(versions.length, 1);
    assert.strictEqual(versions[0].size, Buffer.from('new').length);
  });
});

test('deleteBucket fails on non-empty bucket unless forced', () => {
  withTempDir((dir) => {
    const { storage } = makeStorage(dir);
    storage.createBucket('b');
    storage.putObject('b', 'k', Buffer.from('x'));
    assert.throws(() => storage.deleteBucket('b'), Error);
    storage.deleteBucket('b', { force: true });
    assert.strictEqual(storage.buckets.has('b'), false);
    assert.strictEqual(storage.chunkStore.size(), 0);
  });
});

test('cleanup leaves no temp directories behind', () => {
  const before = fs.readdirSync(os.tmpdir()).filter((n) => n.startsWith('storage-test-'));
  assert.strictEqual(before.length, 0);
});

console.log(`\n${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
