'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { WriteAheadLog } = require('./wal.js');

function main() {
  const walPath = path.join(os.tmpdir(), `wal-demo-${Date.now()}.log`);

  console.log('=== appending records ===');
  let wal = new WriteAheadLog(walPath);
  wal.append({ op: 'set', key: 'a', value: 1 });
  wal.append({ op: 'set', key: 'b', value: 2 });
  wal.append({ op: 'set', key: 'c', value: 3 });
  console.log(`wrote 3 records, file size: ${wal.size()} bytes`);

  console.log('\n=== restart: reopen and replay ===');
  wal = new WriteAheadLog(walPath);
  let result = wal.replay();
  console.log(`replayed ${result.records.length} records, corrupted: ${result.corrupted}`);
  for (const record of result.records) {
    console.log(`  ${JSON.stringify(record)}`);
  }

  console.log('\n=== simulating a crash mid-write (truncated final record) ===');
  wal.append({ op: 'set', key: 'd', value: 4 });
  const validSize = wal.size();
  fs.appendFileSync(walPath, Buffer.from([0x99, 0x01, 0x00, 0x00, 0xff, 0xff]));
  console.log(`appended ${validSize} valid bytes then a truncated/garbage tail`);

  wal = new WriteAheadLog(walPath);
  result = wal.replay();
  console.log(`replay stopped cleanly: corrupted=${result.corrupted}, validBytes=${result.validBytes}`);
  console.log(`recovered all ${result.records.length} valid records, garbage tail ignored:`);
  for (const record of result.records) {
    console.log(`  ${JSON.stringify(record)}`);
  }

  console.log('\n=== compaction after a snapshot checkpoint ===');
  console.log(`before compaction: file size ${wal.size()} bytes, ${result.records.length} records`);
  const keptCount = wal.compact(2);
  console.log(`compacted, keeping records from index 2 onward: ${keptCount} records kept`);
  console.log(`after compaction: file size ${wal.size()} bytes`);

  const finalReplay = wal.replay();
  console.log('records remaining after compaction:');
  for (const record of finalReplay.records) {
    console.log(`  ${JSON.stringify(record)}`);
  }

  fs.unlinkSync(walPath);
  console.log('\ncleaned up temp WAL file');
}

main();
