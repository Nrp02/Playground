use crate::error::{Error, Result};
use crate::hint;
use crate::keydir::{apply_live, apply_tombstone, EntryPointer, KeyDir};
use crate::record::{self, HEADER_LEN};
use std::collections::HashMap;
use std::fs::{self, File, OpenOptions};
use std::io::{BufReader, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

pub const DATA_EXTENSION: &str = "data";
pub const HINT_EXTENSION: &str = "hint";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Config {
    pub max_file_size: u64,
}

impl Default for Config {
    fn default() -> Config {
        Config {
            max_file_size: 1024 * 1024,
        }
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct RecoveryStats {
    pub files_from_hint: usize,
    pub files_scanned: usize,
    pub records_from_hint: usize,
    pub records_scanned: usize,
    pub bytes_scanned: u64,
    pub torn_records_skipped: usize,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct MergeStats {
    pub bytes_before: u64,
    pub bytes_after: u64,
    pub files_before: usize,
    pub files_after: usize,
    pub live_keys: usize,
    pub hint_files_written: usize,
}

struct ScanEntry {
    key: Vec<u8>,
    pointer: EntryPointer,
    tombstone: bool,
}

struct ScanResult {
    entries: Vec<ScanEntry>,
    valid_len: u64,
    torn: bool,
}

pub struct Bitcask {
    dir: PathBuf,
    config: Config,
    keydir: KeyDir,
    readers: HashMap<u64, File>,
    active_id: u64,
    active: File,
    active_size: u64,
    clock: u64,
    recovery: RecoveryStats,
}

impl Bitcask {
    pub fn open<P: AsRef<Path>>(dir: P) -> Result<Bitcask> {
        Bitcask::open_with(dir, Config::default())
    }

    pub fn open_with<P: AsRef<Path>>(dir: P, config: Config) -> Result<Bitcask> {
        let dir = dir.as_ref().to_path_buf();
        fs::create_dir_all(&dir)?;
        let ids = data_file_ids(&dir)?;
        let mut keydir = KeyDir::new();
        let mut recovery = RecoveryStats::default();
        let mut clock = 0u64;
        let last = ids.last().copied();
        for &id in &ids {
            let data = data_path(&dir, id);
            let hint = hint_path(&dir, id);
            let loaded = if Some(id) != last && hint.exists() {
                hint::load(&hint, id).ok()
            } else {
                None
            };
            match loaded {
                Some(entries) => {
                    recovery.files_from_hint += 1;
                    recovery.records_from_hint += entries.len();
                    for (key, pointer) in entries {
                        clock = clock.max(pointer.timestamp);
                        apply_live(&mut keydir, key, pointer);
                    }
                }
                None => {
                    let scan = scan_data_file(&data, id)?;
                    recovery.files_scanned += 1;
                    recovery.records_scanned += scan.entries.len();
                    recovery.bytes_scanned += scan.valid_len;
                    if scan.torn {
                        recovery.torn_records_skipped += 1;
                        OpenOptions::new().write(true).open(&data)?.set_len(scan.valid_len)?;
                    }
                    for entry in scan.entries {
                        clock = clock.max(entry.pointer.timestamp);
                        if entry.tombstone {
                            apply_tombstone(&mut keydir, &entry.key, entry.pointer.timestamp);
                        } else {
                            apply_live(&mut keydir, entry.key, entry.pointer);
                        }
                    }
                }
            }
        }
        let active_id = last.unwrap_or(0);
        let active = open_append(&data_path(&dir, active_id))?;
        let active_size = active.metadata()?.len();
        Ok(Bitcask {
            dir,
            config,
            keydir,
            readers: HashMap::new(),
            active_id,
            active,
            active_size,
            clock,
            recovery,
        })
    }

    pub fn config(&self) -> Config {
        self.config
    }

    pub fn recovery_stats(&self) -> RecoveryStats {
        self.recovery
    }

    pub fn len(&self) -> usize {
        self.keydir.len()
    }

    pub fn is_empty(&self) -> bool {
        self.keydir.is_empty()
    }

    pub fn contains_key(&self, key: &[u8]) -> bool {
        self.keydir.contains_key(key)
    }

    pub fn pointer(&self, key: &[u8]) -> Option<EntryPointer> {
        self.keydir.get(key).copied()
    }

    pub fn keys(&self) -> Vec<Vec<u8>> {
        self.keydir.keys().cloned().collect()
    }

    pub fn active_file_id(&self) -> u64 {
        self.active_id
    }

    pub fn data_file_ids(&self) -> Result<Vec<u64>> {
        data_file_ids(&self.dir)
    }

    pub fn hint_file_ids(&self) -> Result<Vec<u64>> {
        file_ids_with_extension(&self.dir, HINT_EXTENSION)
    }

    pub fn disk_size(&self) -> Result<u64> {
        let mut total = 0u64;
        for entry in fs::read_dir(&self.dir)? {
            let entry = entry?;
            if entry.metadata()?.is_file() {
                total += entry.metadata()?.len();
            }
        }
        Ok(total)
    }

    pub fn put(&mut self, key: &[u8], value: &[u8]) -> Result<()> {
        let timestamp = self.next_timestamp();
        let bytes = record::encode(timestamp, key, Some(value))?;
        let (file_id, offset) = self.append(&bytes)?;
        self.keydir.insert(
            key.to_vec(),
            EntryPointer {
                file_id,
                record_offset: offset,
                key_len: key.len() as u32,
                value_len: value.len() as u32,
                timestamp,
            },
        );
        Ok(())
    }

    pub fn get(&mut self, key: &[u8]) -> Result<Option<Vec<u8>>> {
        let pointer = match self.keydir.get(key) {
            Some(pointer) => *pointer,
            None => return Ok(None),
        };
        let (stored_key, value) = self.read_record(&pointer)?;
        if stored_key != key {
            return Err(Error::KeyMismatch {
                file_id: pointer.file_id,
                offset: pointer.record_offset,
            });
        }
        Ok(Some(value))
    }

    pub fn delete(&mut self, key: &[u8]) -> Result<bool> {
        if !self.keydir.contains_key(key) {
            return Ok(false);
        }
        let timestamp = self.next_timestamp();
        let bytes = record::encode(timestamp, key, None)?;
        self.append(&bytes)?;
        self.keydir.remove(key);
        Ok(true)
    }

    pub fn sync(&mut self) -> Result<()> {
        self.active.sync_all()?;
        Ok(())
    }

    pub fn rotate(&mut self) -> Result<u64> {
        self.active.sync_all()?;
        let next = self.active_id + 1;
        self.active = open_append(&data_path(&self.dir, next))?;
        self.active_id = next;
        self.active_size = 0;
        Ok(next)
    }

    pub fn merge(&mut self) -> Result<MergeStats> {
        let bytes_before = self.disk_size()?;
        let files_before = self.data_file_ids()?.len();
        if self.active_size > 0 {
            self.rotate()?;
        }
        let stale_ids = self.data_file_ids()?;
        let base = stale_ids.iter().copied().max().unwrap_or(self.active_id);

        let mut ordered: Vec<(Vec<u8>, EntryPointer)> =
            self.keydir.iter().map(|(k, v)| (k.clone(), *v)).collect();
        ordered.sort_by_key(|(_, pointer)| (pointer.file_id, pointer.record_offset));

        let mut fresh = KeyDir::new();
        let mut output_id = base;
        let mut output: Option<File> = None;
        let mut output_size = 0u64;
        let mut hints: Vec<u8> = Vec::new();
        let mut hint_files = 0usize;

        for (key, pointer) in ordered {
            let (_, value) = self.read_record(&pointer)?;
            let bytes = record::encode(pointer.timestamp, &key, Some(&value))?;
            let needs_new = match output.as_ref() {
                None => true,
                Some(_) => output_size + bytes.len() as u64 > self.config.max_file_size,
            };
            if needs_new {
                if let Some(file) = output.take() {
                    finish_output(file, &hint_path(&self.dir, output_id), &hints)?;
                    hint_files += 1;
                    hints.clear();
                }
                output_id += 1;
                output = Some(open_append(&data_path(&self.dir, output_id))?);
                output_size = 0;
            }
            let file = output.as_mut().expect("merge output file is open");
            file.write_all(&bytes)?;
            let moved = EntryPointer {
                file_id: output_id,
                record_offset: output_size,
                key_len: pointer.key_len,
                value_len: pointer.value_len,
                timestamp: pointer.timestamp,
            };
            hints.extend_from_slice(&hint::encode(&key, &moved));
            fresh.insert(key, moved);
            output_size += bytes.len() as u64;
        }
        if let Some(file) = output.take() {
            finish_output(file, &hint_path(&self.dir, output_id), &hints)?;
            hint_files += 1;
        }

        let new_active_id = output_id + 1;
        let new_active = open_append(&data_path(&self.dir, new_active_id))?;
        new_active.sync_all()?;

        let live_keys = fresh.len();
        self.keydir = fresh;
        self.readers.clear();
        self.active = new_active;
        self.active_id = new_active_id;
        self.active_size = 0;

        for id in stale_ids {
            let _ = fs::remove_file(data_path(&self.dir, id));
            let _ = fs::remove_file(hint_path(&self.dir, id));
        }

        Ok(MergeStats {
            bytes_before,
            bytes_after: self.disk_size()?,
            files_before,
            files_after: self.data_file_ids()?.len(),
            live_keys,
            hint_files_written: hint_files,
        })
    }

    fn next_timestamp(&mut self) -> u64 {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos() as u64;
        self.clock = if now > self.clock { now } else { self.clock + 1 };
        self.clock
    }

    fn append(&mut self, bytes: &[u8]) -> Result<(u64, u64)> {
        if self.active_size > 0 && self.active_size + bytes.len() as u64 > self.config.max_file_size {
            self.rotate()?;
        }
        let offset = self.active_size;
        self.active.write_all(bytes)?;
        self.active_size += bytes.len() as u64;
        Ok((self.active_id, offset))
    }

    fn read_record(&mut self, pointer: &EntryPointer) -> Result<(Vec<u8>, Vec<u8>)> {
        let total = pointer.record_len() as usize;
        let mut buffer = vec![0u8; total];
        {
            let file = self.reader(pointer.file_id)?;
            file.seek(SeekFrom::Start(pointer.record_offset))?;
            file.read_exact(&mut buffer)?;
        }
        if !record::verify(&buffer[..HEADER_LEN], &buffer[HEADER_LEN..]) {
            return Err(Error::CrcMismatch {
                file_id: pointer.file_id,
                offset: pointer.record_offset,
            });
        }
        let key_end = HEADER_LEN + pointer.key_len as usize;
        let key = buffer[HEADER_LEN..key_end].to_vec();
        let value = buffer[key_end..].to_vec();
        Ok((key, value))
    }

    fn reader(&mut self, file_id: u64) -> Result<&mut File> {
        if !self.readers.contains_key(&file_id) {
            let path = data_path(&self.dir, file_id);
            if !path.exists() {
                return Err(Error::MissingDataFile(file_id));
            }
            self.readers.insert(file_id, File::open(path)?);
        }
        Ok(self.readers.get_mut(&file_id).expect("reader was just inserted"))
    }
}

fn finish_output(file: File, hint: &Path, hints: &[u8]) -> Result<()> {
    file.sync_all()?;
    let mut handle = File::create(hint)?;
    handle.write_all(hints)?;
    handle.sync_all()?;
    Ok(())
}

fn open_append(path: &Path) -> Result<File> {
    let file = OpenOptions::new().create(true).append(true).open(path)?;
    Ok(file)
}

pub fn data_path(dir: &Path, id: u64) -> PathBuf {
    dir.join(format!("{id:010}.{DATA_EXTENSION}"))
}

pub fn hint_path(dir: &Path, id: u64) -> PathBuf {
    dir.join(format!("{id:010}.{HINT_EXTENSION}"))
}

pub fn data_file_ids(dir: &Path) -> Result<Vec<u64>> {
    file_ids_with_extension(dir, DATA_EXTENSION)
}

fn file_ids_with_extension(dir: &Path, extension: &str) -> Result<Vec<u64>> {
    let mut ids = Vec::new();
    if !dir.exists() {
        return Ok(ids);
    }
    for entry in fs::read_dir(dir)? {
        let entry = entry?;
        let path = entry.path();
        if path.extension().and_then(|e| e.to_str()) != Some(extension) {
            continue;
        }
        if let Some(stem) = path.file_stem().and_then(|s| s.to_str()) {
            if let Ok(id) = stem.parse::<u64>() {
                ids.push(id);
            }
        }
    }
    ids.sort_unstable();
    Ok(ids)
}

fn scan_data_file(path: &Path, file_id: u64) -> Result<ScanResult> {
    let file = File::open(path)?;
    let file_len = file.metadata()?.len();
    let mut reader = BufReader::new(file);
    let mut header = [0u8; HEADER_LEN];
    let mut offset = 0u64;
    let mut entries = Vec::new();
    let mut torn = false;
    while offset < file_len {
        if file_len - offset < HEADER_LEN as u64 {
            torn = true;
            break;
        }
        reader.read_exact(&mut header)?;
        let parsed = record::parse_header(&header);
        let payload = parsed.payload_len();
        if file_len - offset - (HEADER_LEN as u64) < payload {
            torn = true;
            break;
        }
        let mut body = vec![0u8; payload as usize];
        reader.read_exact(&mut body)?;
        if !record::verify(&header, &body) {
            if offset + parsed.total_len() == file_len {
                torn = true;
                break;
            }
            return Err(Error::CrcMismatch { file_id, offset });
        }
        entries.push(ScanEntry {
            key: body[..parsed.key_len as usize].to_vec(),
            pointer: EntryPointer {
                file_id,
                record_offset: offset,
                key_len: parsed.key_len,
                value_len: if parsed.is_tombstone() { 0 } else { parsed.value_len },
                timestamp: parsed.timestamp,
            },
            tombstone: parsed.is_tombstone(),
        });
        offset += parsed.total_len();
    }
    Ok(ScanResult {
        entries,
        valid_len: offset,
        torn,
    })
}
