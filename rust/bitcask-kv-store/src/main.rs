use bitcask_kv_store::{data_path, Bitcask, Config, Error};
use std::collections::BTreeMap;
use std::fs::{self, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::time::{Instant, SystemTime, UNIX_EPOCH};

struct CleanupGuard(Vec<PathBuf>);

impl Drop for CleanupGuard {
    fn drop(&mut self) {
        for path in &self.0 {
            let _ = fs::remove_dir_all(path);
        }
    }
}

fn unique_temp_dir(label: &str) -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos();
    std::env::temp_dir().join(format!(
        "bitcask_demo_{label}_{}_{nanos}",
        std::process::id()
    ))
}

fn next_random(state: &mut u64) -> u64 {
    *state = state
        .wrapping_mul(6364136223846793005)
        .wrapping_add(1442695040888963407);
    *state >> 16
}

fn make_key(index: usize) -> Vec<u8> {
    format!("user:{index:06}:profile").into_bytes()
}

fn make_value(seed: u64, len: usize) -> Vec<u8> {
    let mut state = seed | 1;
    let mut out = Vec::with_capacity(len);
    for _ in 0..len {
        out.push((next_random(&mut state) % 251) as u8);
    }
    out
}

fn human(bytes: u64) -> String {
    if bytes >= 1024 * 1024 {
        format!("{:.2} MiB", bytes as f64 / (1024.0 * 1024.0))
    } else {
        format!("{:.2} KiB", bytes as f64 / 1024.0)
    }
}

fn flip_byte(path: &Path, offset: u64) -> std::io::Result<()> {
    let mut file = OpenOptions::new().read(true).write(true).open(path)?;
    file.seek(SeekFrom::Start(offset))?;
    let mut byte = [0u8; 1];
    file.read_exact(&mut byte)?;
    file.seek(SeekFrom::Start(offset))?;
    file.write_all(&[byte[0] ^ 0xFF])?;
    file.sync_all()
}

fn truncate_tail(path: &Path, bytes: u64) -> std::io::Result<u64> {
    let file = OpenOptions::new().write(true).open(path)?;
    let len = file.metadata()?.len();
    let cut = len.saturating_sub(bytes);
    file.set_len(cut)?;
    file.sync_all()?;
    Ok(cut)
}

fn build_workload(store: &mut Bitcask, model: &mut BTreeMap<Vec<u8>, Vec<u8>>) -> Result<(), Error> {
    let mut state = 0x5eed_1234_9abc_def0u64;
    for index in 0..4000usize {
        let key = make_key(index);
        let value = make_value(next_random(&mut state), 320 + (index % 8) * 40);
        store.put(&key, &value)?;
        model.insert(key, value);
    }
    for _ in 0..6000usize {
        let index = (next_random(&mut state) % 4000) as usize;
        let key = make_key(index);
        let value = make_value(next_random(&mut state), 320 + (index % 11) * 30);
        store.put(&key, &value)?;
        model.insert(key, value);
    }
    for step in 0..800usize {
        let index = step * 5;
        let key = make_key(index);
        store.delete(&key)?;
        model.remove(&key);
    }
    Ok(())
}

fn verify_against_model(
    store: &mut Bitcask,
    model: &BTreeMap<Vec<u8>, Vec<u8>>,
) -> Result<(), Error> {
    assert_eq!(store.len(), model.len(), "keydir size diverged from the model");
    for (key, expected) in model {
        let found = store.get(key)?;
        assert_eq!(found.as_ref(), Some(expected), "value diverged from the model");
    }
    Ok(())
}

fn section(title: &str) {
    println!();
    println!("== {title} ==");
}

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let dir = unique_temp_dir("store");
    let corrupt_dir = unique_temp_dir("corrupt");
    let _guard = CleanupGuard(vec![dir.clone(), corrupt_dir.clone()]);

    let config = Config {
        max_file_size: 256 * 1024,
    };
    let mut model: BTreeMap<Vec<u8>, Vec<u8>> = BTreeMap::new();

    section("append-only writes");
    let mut store = Bitcask::open_with(&dir, config)?;
    let started = Instant::now();
    build_workload(&mut store, &mut model)?;
    store.sync()?;
    println!(
        "wrote 4000 inserts + 6000 overwrites + 800 deletes in {:?}",
        started.elapsed()
    );
    println!("live keys in the keydir : {}", store.len());
    println!("data files on disk      : {}", store.data_file_ids()?.len());
    println!("active file id          : {:010}", store.active_file_id());
    println!("bytes on disk           : {}", human(store.disk_size()?));

    section("reads across rotated files");
    let ids = store.data_file_ids()?;
    let mut sampled = 0usize;
    for id in ids.iter().take(6) {
        let hit = model
            .keys()
            .find(|key| store.pointer(key).map(|p| p.file_id) == Some(*id));
        if let Some(key) = hit {
            let pointer = store.pointer(key).expect("pointer for a live key");
            let value = store.get(key)?.expect("value for a live key");
            println!(
                "file {:010} offset {:>7} len {:>4} -> {} bytes, matches model: {}",
                pointer.file_id,
                pointer.value_offset(),
                pointer.value_len,
                value.len(),
                value == model[key]
            );
            sampled += 1;
        }
    }
    println!("sampled {sampled} distinct data files, one seek + one read each");
    verify_against_model(&mut store, &model)?;

    section("merge compaction");
    let merge = store.merge()?;
    println!(
        "before : {} across {} data files",
        human(merge.bytes_before),
        merge.files_before
    );
    println!(
        "after  : {} across {} data files + {} hint files",
        human(merge.bytes_after),
        merge.files_after,
        merge.hint_files_written
    );
    println!("live keys kept          : {}", merge.live_keys);
    println!(
        "space amplification before merge: {:.2}x, reclaimed {}",
        merge.bytes_before as f64 / merge.bytes_after.max(1) as f64,
        human(merge.bytes_before.saturating_sub(merge.bytes_after))
    );
    verify_against_model(&mut store, &model)?;
    println!("every live key still readable after the swap: yes");
    drop(store);

    section("recovery: hint fast path vs full scan");
    let started = Instant::now();
    let mut store = Bitcask::open_with(&dir, config)?;
    let hint_elapsed = started.elapsed();
    let hint_stats = store.recovery_stats();
    println!(
        "hint path : {:?} | {} files from hints ({} records), {} files scanned, {} bytes read",
        hint_elapsed,
        hint_stats.files_from_hint,
        hint_stats.records_from_hint,
        hint_stats.files_scanned,
        hint_stats.bytes_scanned
    );
    verify_against_model(&mut store, &model)?;
    println!("reopened keydir identical to the pre-close keydir: yes");
    drop(store);

    for id in Bitcask::open_with(&dir, config)?.hint_file_ids()? {
        fs::remove_file(dir.join(format!("{id:010}.hint")))?;
    }
    let started = Instant::now();
    let mut store = Bitcask::open_with(&dir, config)?;
    let scan_elapsed = started.elapsed();
    let scan_stats = store.recovery_stats();
    println!(
        "scan path : {:?} | {} files scanned ({} records), {} bytes read",
        scan_elapsed, scan_stats.files_scanned, scan_stats.records_scanned, scan_stats.bytes_scanned
    );
    println!(
        "hint files avoided reading {} of value bytes at open",
        human(scan_stats.bytes_scanned.saturating_sub(hint_stats.bytes_scanned))
    );
    verify_against_model(&mut store, &model)?;

    section("crash mid-write: torn tail record");
    let markers: Vec<Vec<u8>> = (0..3).map(|i| format!("marker:{i}").into_bytes()).collect();
    for (index, key) in markers.iter().enumerate() {
        store.put(key, &make_value(7 + index as u64, 500))?;
    }
    store.sync()?;
    let active = data_path(&dir, store.active_file_id());
    drop(store);
    let kept = truncate_tail(&active, 200)?;
    println!("truncated the active file mid-record, {kept} bytes survive");
    let mut store = Bitcask::open_with(&dir, config)?;
    let torn_stats = store.recovery_stats();
    println!("torn records skipped    : {}", torn_stats.torn_records_skipped);
    for key in markers.iter() {
        println!(
            "  {} -> {}",
            String::from_utf8_lossy(key),
            match store.get(key)? {
                Some(value) => format!("{} bytes recovered", value.len()),
                None => "lost with the torn record".to_string(),
            }
        );
    }
    println!(
        "pre-crash data intact   : {} of {} original keys still present",
        model.keys().filter(|key| store.contains_key(key)).count(),
        model.len()
    );
    drop(store);

    section("corrupted record rejected");
    let small = Config { max_file_size: 4096 };
    let mut store = Bitcask::open_with(&corrupt_dir, small)?;
    for index in 0..24usize {
        store.put(&make_key(index), &make_value(index as u64 + 1, 400))?;
    }
    store.merge()?;
    let victim = (0..24usize)
        .map(make_key)
        .find(|key| match store.pointer(key) {
            Some(pointer) => {
                let len = fs::metadata(data_path(&corrupt_dir, pointer.file_id))
                    .map(|m| m.len())
                    .unwrap_or(0);
                pointer.record_offset + pointer.record_len() < len
            }
            None => false,
        })
        .expect("a record that is not the last one in its file");
    let pointer = store.pointer(&victim).expect("pointer for the victim key");
    drop(store);
    flip_byte(&data_path(&corrupt_dir, pointer.file_id), pointer.value_offset())?;
    println!(
        "flipped one value byte in file {:010} at offset {}",
        pointer.file_id,
        pointer.value_offset()
    );

    let mut store = Bitcask::open_with(&corrupt_dir, small)?;
    println!("open succeeded: hint files rebuild the keydir without reading value bytes");
    match store.get(&victim) {
        Err(error) => println!("get({}) -> {error}", String::from_utf8_lossy(&victim)),
        Ok(_) => println!("corruption went undetected, which should not happen"),
    }
    let hint_ids = store.hint_file_ids()?;
    drop(store);
    for id in hint_ids {
        fs::remove_file(corrupt_dir.join(format!("{id:010}.hint")))?;
    }
    match Bitcask::open_with(&corrupt_dir, small) {
        Err(error) => println!("open without hints -> {error}"),
        Ok(_) => println!("corruption went undetected on the scan path, which should not happen"),
    }

    section("design contrast");
    println!("Bitcask keeps one append-only log plus a full in-memory hash index, so every get is");
    println!("exactly one seek and one read with no merge-sorted lookup path and no read");
    println!("amplification. An LSM tree instead flushes sorted memtables into levelled runs, so a");
    println!("read may probe several sorted files and compaction rewrites data repeatedly - but it");
    println!("keeps only a sparse index in memory and supports ordered range scans.");
    println!("Bitcask pays for its simplicity with RAM: every key must fit in the keydir, and only");
    println!("point lookups are possible - a range scan degrades to a full keydir sort.");
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("demo failed: {error}");
        std::process::exit(1);
    }
}
