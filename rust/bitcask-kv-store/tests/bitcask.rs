use bitcask_kv_store::{
    data_file_ids, data_path, parse_header, Bitcask, Config, Error, HEADER_LEN, TOMBSTONE_LEN,
};
use std::collections::{BTreeSet, HashMap};
use std::fs::{self, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

static COUNTER: AtomicU64 = AtomicU64::new(0);

fn temp_dir(label: &str) -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos();
    let n = COUNTER.fetch_add(1, Ordering::SeqCst);
    std::env::temp_dir().join(format!(
        "bitcask_test_{label}_{}_{nanos}_{n}",
        std::process::id()
    ))
}

struct Cleanup(Vec<PathBuf>);

impl Drop for Cleanup {
    fn drop(&mut self) {
        for path in &self.0 {
            let _ = fs::remove_dir_all(path);
        }
    }
}

fn lcg(state: &mut u64) -> u64 {
    *state = state
        .wrapping_mul(6364136223846793005)
        .wrapping_add(1442695040888963407);
    *state >> 16
}

fn bytes_of(seed: u64, len: usize) -> Vec<u8> {
    let mut state = seed | 1;
    let mut out = Vec::with_capacity(len);
    for _ in 0..len {
        out.push((lcg(&mut state) % 256) as u8);
    }
    out
}

fn small_files() -> Config {
    Config {
        max_file_size: 4096,
    }
}

fn count_raw_records(dir: &Path) -> (usize, usize) {
    let mut total = 0usize;
    let mut tombstones = 0usize;
    for id in data_file_ids(dir).unwrap() {
        let bytes = fs::read(data_path(dir, id)).unwrap();
        let mut offset = 0usize;
        while offset + HEADER_LEN <= bytes.len() {
            let header = parse_header(&bytes[offset..offset + HEADER_LEN]);
            let is_tombstone = header.value_len == TOMBSTONE_LEN;
            let value_len = if is_tombstone { 0 } else { header.value_len as usize };
            let record_len = HEADER_LEN + header.key_len as usize + value_len;
            if offset + record_len > bytes.len() {
                break;
            }
            total += 1;
            if is_tombstone {
                tombstones += 1;
            }
            offset += record_len;
        }
    }
    (total, tombstones)
}

fn snapshot(store: &mut Bitcask) -> HashMap<Vec<u8>, Vec<u8>> {
    let mut out = HashMap::new();
    for key in store.keys() {
        let value = store.get(&key).unwrap().expect("live key must have a value");
        out.insert(key, value);
    }
    out
}

#[test]
fn round_trip_with_empty_values_and_binary_keys() {
    let dir = temp_dir("round_trip");
    let _cleanup = Cleanup(vec![dir.clone()]);
    let mut store = Bitcask::open(&dir).unwrap();

    assert!(store.is_empty());
    store.put(b"plain", b"value").unwrap();
    store.put(b"empty", b"").unwrap();
    let binary_key: Vec<u8> = vec![0x00, 0xFF, 0xC3, 0x28, 0x80, 0x7F];
    let binary_value: Vec<u8> = vec![0xDE, 0xAD, 0x00, 0xBE, 0xEF];
    store.put(&binary_key, &binary_value).unwrap();

    assert_eq!(store.get(b"plain").unwrap(), Some(b"value".to_vec()));
    assert_eq!(store.get(b"empty").unwrap(), Some(Vec::new()));
    assert_eq!(store.get(&binary_key).unwrap(), Some(binary_value));
    assert_eq!(store.len(), 3);

    assert!(store.delete(b"plain").unwrap());
    assert!(!store.delete(b"plain").unwrap());
    assert_eq!(store.get(b"plain").unwrap(), None);
    assert_eq!(store.get(b"never-written").unwrap(), None);
    assert_eq!(store.len(), 2);
}

#[test]
fn overwrite_returns_the_newest_value() {
    let dir = temp_dir("overwrite");
    let _cleanup = Cleanup(vec![dir.clone()]);
    let mut store = Bitcask::open_with(&dir, small_files()).unwrap();

    for round in 0..50u64 {
        store.put(b"hot", &bytes_of(round + 1, 300)).unwrap();
    }
    assert_eq!(store.len(), 1);
    assert_eq!(store.get(b"hot").unwrap(), Some(bytes_of(50, 300)));

    let pointer = store.pointer(b"hot").unwrap();
    assert_eq!(pointer.value_len, 300);
    assert_eq!(
        pointer.value_offset(),
        pointer.record_offset + HEADER_LEN as u64 + 3
    );

    drop(store);
    let mut reopened = Bitcask::open_with(&dir, small_files()).unwrap();
    assert_eq!(reopened.get(b"hot").unwrap(), Some(bytes_of(50, 300)));
    assert_eq!(reopened.len(), 1);
}

#[test]
fn rotation_creates_new_files_and_reads_span_them() {
    let dir = temp_dir("rotation");
    let _cleanup = Cleanup(vec![dir.clone()]);
    let mut store = Bitcask::open_with(&dir, small_files()).unwrap();

    let mut expected = HashMap::new();
    for index in 0..400usize {
        let key = format!("k{index:04}").into_bytes();
        let value = bytes_of(index as u64 + 1, 200);
        store.put(&key, &value).unwrap();
        expected.insert(key, value);
    }

    let ids = store.data_file_ids().unwrap();
    assert!(ids.len() > 10, "expected many rotations, got {}", ids.len());
    assert_eq!(store.active_file_id(), *ids.last().unwrap());
    for id in &ids {
        let len = fs::metadata(data_path(&dir, *id)).unwrap().len();
        assert!(len <= small_files().max_file_size + 300, "file {id} grew past the threshold");
    }

    let touched: BTreeSet<u64> = expected
        .keys()
        .map(|key| store.pointer(key).unwrap().file_id)
        .collect();
    assert_eq!(touched.len(), ids.len(), "live keys should span every data file");

    for (key, value) in &expected {
        assert_eq!(store.get(key).unwrap().as_ref(), Some(value));
    }
}

#[test]
fn merge_shrinks_disk_and_keeps_live_keys() {
    let dir = temp_dir("merge");
    let _cleanup = Cleanup(vec![dir.clone()]);
    let mut store = Bitcask::open_with(&dir, small_files()).unwrap();

    let mut expected = HashMap::new();
    for round in 0..12u64 {
        for index in 0..100usize {
            let key = format!("key{index:03}").into_bytes();
            let value = bytes_of(round * 1000 + index as u64 + 1, 150);
            store.put(&key, &value).unwrap();
            expected.insert(key, value);
        }
    }
    for index in (0..100usize).step_by(4) {
        let key = format!("key{index:03}").into_bytes();
        assert!(store.delete(&key).unwrap());
        expected.remove(&key);
    }

    let (records_before, tombstones_before) = count_raw_records(&dir);
    assert!(tombstones_before > 0);
    assert!(records_before > expected.len() * 5);

    let stats = store.merge().unwrap();
    assert_eq!(stats.live_keys, expected.len());
    assert!(stats.bytes_after < stats.bytes_before / 2);
    assert!(stats.files_after < stats.files_before);
    assert!(stats.hint_files_written > 0);

    let (records_after, tombstones_after) = count_raw_records(&dir);
    assert_eq!(records_after, expected.len());
    assert_eq!(tombstones_after, 0);

    assert_eq!(store.len(), expected.len());
    for (key, value) in &expected {
        assert_eq!(store.get(key).unwrap().as_ref(), Some(value));
    }
    for index in (0..100usize).step_by(4) {
        let key = format!("key{index:03}").into_bytes();
        assert_eq!(store.get(&key).unwrap(), None);
    }
}

#[test]
fn merge_writes_hint_files_that_reopen_actually_uses() {
    let dir = temp_dir("hints");
    let _cleanup = Cleanup(vec![dir.clone()]);
    let mut store = Bitcask::open_with(&dir, small_files()).unwrap();

    for index in 0..300usize {
        store
            .put(&format!("h{index:04}").into_bytes(), &bytes_of(index as u64 + 1, 120))
            .unwrap();
    }
    let stats = store.merge().unwrap();
    let hint_ids = store.hint_file_ids().unwrap();
    assert_eq!(hint_ids.len(), stats.hint_files_written);
    assert!(!hint_ids.is_empty());
    let before = snapshot(&mut store);
    drop(store);

    let mut hinted = Bitcask::open_with(&dir, small_files()).unwrap();
    let hinted_stats = hinted.recovery_stats();
    assert_eq!(hinted_stats.files_from_hint, hint_ids.len());
    assert_eq!(hinted_stats.records_from_hint, 300);
    assert_eq!(hinted_stats.bytes_scanned, 0);
    assert_eq!(snapshot(&mut hinted), before);
    drop(hinted);

    for id in hint_ids {
        fs::remove_file(dir.join(format!("{id:010}.hint"))).unwrap();
    }
    let mut scanned = Bitcask::open_with(&dir, small_files()).unwrap();
    let scanned_stats = scanned.recovery_stats();
    assert_eq!(scanned_stats.files_from_hint, 0);
    assert_eq!(scanned_stats.records_scanned, 300);
    assert!(scanned_stats.bytes_scanned > 0);
    assert_eq!(snapshot(&mut scanned), before);
}

#[test]
fn reopen_reconstructs_an_identical_keydir() {
    let dir = temp_dir("reopen");
    let _cleanup = Cleanup(vec![dir.clone()]);
    let mut store = Bitcask::open_with(&dir, small_files()).unwrap();

    let mut state = 99u64;
    for index in 0..500usize {
        store
            .put(&format!("r{index:04}").into_bytes(), &bytes_of(lcg(&mut state), 90))
            .unwrap();
    }
    for index in (0..500usize).step_by(7) {
        store.delete(&format!("r{index:04}").into_bytes()).unwrap();
    }
    for index in (0..500usize).step_by(11) {
        store
            .put(&format!("r{index:04}").into_bytes(), &bytes_of(lcg(&mut state), 130))
            .unwrap();
    }
    store.sync().unwrap();
    let before = snapshot(&mut store);
    let before_keys: BTreeSet<Vec<u8>> = before.keys().cloned().collect();
    drop(store);

    let mut reopened = Bitcask::open_with(&dir, small_files()).unwrap();
    let after = snapshot(&mut reopened);
    let after_keys: BTreeSet<Vec<u8>> = after.keys().cloned().collect();
    assert_eq!(before_keys, after_keys);
    assert_eq!(before, after);
    assert_eq!(reopened.recovery_stats().torn_records_skipped, 0);
}

#[test]
fn recovery_skips_a_truncated_final_record() {
    let dir = temp_dir("truncated");
    let _cleanup = Cleanup(vec![dir.clone()]);
    let mut store = Bitcask::open(&dir).unwrap();

    for index in 0..20usize {
        store
            .put(&format!("t{index:02}").into_bytes(), &bytes_of(index as u64 + 1, 400))
            .unwrap();
    }
    store.sync().unwrap();
    let active = data_path(&dir, store.active_file_id());
    let full_len = fs::metadata(&active).unwrap().len();
    drop(store);

    let file = OpenOptions::new().write(true).open(&active).unwrap();
    file.set_len(full_len - 150).unwrap();
    file.sync_all().unwrap();
    drop(file);

    let mut recovered = Bitcask::open(&dir).unwrap();
    assert_eq!(recovered.recovery_stats().torn_records_skipped, 1);
    assert_eq!(recovered.len(), 19);
    assert_eq!(recovered.get(b"t19").unwrap(), None);
    for index in 0..19usize {
        let key = format!("t{index:02}").into_bytes();
        assert_eq!(
            recovered.get(&key).unwrap(),
            Some(bytes_of(index as u64 + 1, 400))
        );
    }

    assert!(fs::metadata(&active).unwrap().len() < full_len - 150 + 1);
    recovered.put(b"after-crash", b"still writable").unwrap();
    drop(recovered);
    let mut again = Bitcask::open(&dir).unwrap();
    assert_eq!(again.recovery_stats().torn_records_skipped, 0);
    assert_eq!(again.get(b"after-crash").unwrap(), Some(b"still writable".to_vec()));
    assert_eq!(again.len(), 20);
}

#[test]
fn truncated_header_is_also_treated_as_torn() {
    let dir = temp_dir("torn_header");
    let _cleanup = Cleanup(vec![dir.clone()]);
    let mut store = Bitcask::open(&dir).unwrap();
    store.put(b"a", b"first").unwrap();
    store.put(b"b", b"second").unwrap();
    store.sync().unwrap();
    let active = data_path(&dir, store.active_file_id());
    let full_len = fs::metadata(&active).unwrap().len();
    drop(store);

    let file = OpenOptions::new().write(true).open(&active).unwrap();
    file.set_len(full_len - (HEADER_LEN as u64 + 6) + 5).unwrap();
    file.sync_all().unwrap();
    drop(file);

    let mut recovered = Bitcask::open(&dir).unwrap();
    assert_eq!(recovered.recovery_stats().torn_records_skipped, 1);
    assert_eq!(recovered.get(b"a").unwrap(), Some(b"first".to_vec()));
    assert_eq!(recovered.get(b"b").unwrap(), None);
}

#[test]
fn corrupted_record_is_reported_as_an_error() {
    let dir = temp_dir("corrupt");
    let _cleanup = Cleanup(vec![dir.clone()]);
    let mut store = Bitcask::open_with(&dir, small_files()).unwrap();
    for index in 0..40usize {
        store
            .put(&format!("c{index:03}").into_bytes(), &bytes_of(index as u64 + 1, 300))
            .unwrap();
    }
    store.merge().unwrap();

    let victim = (0..40usize)
        .map(|index| format!("c{index:03}").into_bytes())
        .find(|key| {
            let pointer = store.pointer(key).unwrap();
            let len = fs::metadata(data_path(&dir, pointer.file_id)).unwrap().len();
            pointer.record_offset + pointer.record_len() < len
        })
        .expect("a record that is not the last one in its file");
    let pointer = store.pointer(&victim).unwrap();
    let hint_ids = store.hint_file_ids().unwrap();
    drop(store);

    let path = data_path(&dir, pointer.file_id);
    let mut file = OpenOptions::new().read(true).write(true).open(&path).unwrap();
    file.seek(SeekFrom::Start(pointer.value_offset())).unwrap();
    let mut byte = [0u8; 1];
    file.read_exact(&mut byte).unwrap();
    file.seek(SeekFrom::Start(pointer.value_offset())).unwrap();
    file.write_all(&[byte[0] ^ 0xFF]).unwrap();
    file.sync_all().unwrap();
    drop(file);

    let mut store = Bitcask::open_with(&dir, small_files()).unwrap();
    match store.get(&victim) {
        Err(Error::CrcMismatch { file_id, offset }) => {
            assert_eq!(file_id, pointer.file_id);
            assert_eq!(offset, pointer.record_offset);
        }
        other => panic!("expected a crc mismatch, got {other:?}"),
    }
    let healthy = (0..40usize)
        .map(|index| format!("c{index:03}").into_bytes())
        .filter(|key| key != &victim)
        .all(|key| store.get(&key).is_ok());
    assert!(healthy, "only the corrupted record should fail");
    drop(store);

    for id in hint_ids {
        fs::remove_file(dir.join(format!("{id:010}.hint"))).unwrap();
    }
    match Bitcask::open_with(&dir, small_files()) {
        Err(Error::CrcMismatch { file_id, .. }) => assert_eq!(file_id, pointer.file_id),
        Err(other) => panic!("expected a crc mismatch on the scan path, got {other:?}"),
        Ok(_) => panic!("expected recovery to reject the corrupt record"),
    }
}

#[test]
fn corrupted_hint_file_falls_back_to_scanning() {
    let dir = temp_dir("bad_hint");
    let _cleanup = Cleanup(vec![dir.clone()]);
    let mut store = Bitcask::open_with(&dir, small_files()).unwrap();
    for index in 0..200usize {
        store
            .put(&format!("q{index:03}").into_bytes(), &bytes_of(index as u64 + 1, 100))
            .unwrap();
    }
    store.merge().unwrap();
    let hint_ids = store.hint_file_ids().unwrap();
    let before = snapshot(&mut store);
    drop(store);

    let target = dir.join(format!("{:010}.hint", hint_ids[0]));
    let mut bytes = fs::read(&target).unwrap();
    bytes[9] ^= 0xFF;
    fs::write(&target, &bytes).unwrap();

    let mut store = Bitcask::open_with(&dir, small_files()).unwrap();
    let stats = store.recovery_stats();
    assert_eq!(stats.files_from_hint, hint_ids.len() - 1);
    assert!(stats.files_scanned >= 2);
    assert_eq!(snapshot(&mut store), before);
}

#[test]
fn randomized_workload_matches_a_hashmap_reference() {
    let dir = temp_dir("random");
    let _cleanup = Cleanup(vec![dir.clone()]);
    let config = Config {
        max_file_size: 8 * 1024,
    };
    let mut store = Bitcask::open_with(&dir, config).unwrap();
    let mut model: HashMap<Vec<u8>, Vec<u8>> = HashMap::new();
    let mut state = 0xabcd_ef01_2345_6789u64;

    for step in 0..6000usize {
        let key = format!("rk{:03}", lcg(&mut state) % 400).into_bytes();
        match lcg(&mut state) % 10 {
            0 | 1 => {
                let removed = store.delete(&key).unwrap();
                assert_eq!(removed, model.remove(&key).is_some());
            }
            2 | 3 | 4 => {
                assert_eq!(store.get(&key).unwrap().as_ref(), model.get(&key));
            }
            _ => {
                let value = bytes_of(lcg(&mut state), (lcg(&mut state) % 200) as usize);
                store.put(&key, &value).unwrap();
                model.insert(key, value);
            }
        }
        if step == 3000 {
            let stats = store.merge().unwrap();
            assert_eq!(stats.live_keys, model.len());
        }
    }

    assert_eq!(store.len(), model.len());
    for (key, value) in &model {
        assert_eq!(store.get(key).unwrap().as_ref(), Some(value));
    }

    store.merge().unwrap();
    drop(store);
    let mut reopened = Bitcask::open_with(&dir, config).unwrap();
    assert_eq!(reopened.len(), model.len());
    for (key, value) in &model {
        assert_eq!(reopened.get(key).unwrap().as_ref(), Some(value));
    }
    for probe in 400..450usize {
        assert_eq!(reopened.get(format!("rk{probe:03}").as_bytes()).unwrap(), None);
    }
}

#[test]
fn merge_on_an_empty_store_is_a_no_op() {
    let dir = temp_dir("empty_merge");
    let _cleanup = Cleanup(vec![dir.clone()]);
    let mut store = Bitcask::open(&dir).unwrap();
    let stats = store.merge().unwrap();
    assert_eq!(stats.live_keys, 0);
    assert_eq!(stats.hint_files_written, 0);
    assert!(store.is_empty());
    store.put(b"after", b"merge").unwrap();
    assert_eq!(store.get(b"after").unwrap(), Some(b"merge".to_vec()));
    drop(store);
    let mut reopened = Bitcask::open(&dir).unwrap();
    assert_eq!(reopened.get(b"after").unwrap(), Some(b"merge".to_vec()));
    assert_eq!(reopened.len(), 1);
}
