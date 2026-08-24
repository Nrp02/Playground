//! In-memory key-value map backed by a durable write-ahead log.
//!
//! `Store` is the public API of the crate: every mutation goes through the
//! WAL first (so it survives a crash) and is only applied to the in-memory
//! map once it has been durably appended to disk. Reads are served entirely
//! from memory for speed.

use std::collections::BTreeMap;
use std::io;
use std::path::{Path, PathBuf};

use crate::wal::{Record, Wal};

/// A persistent key-value store.
///
/// Construct with [`Store::open`], which replays any existing WAL at the
/// given path to reconstruct in-memory state before returning.
pub struct Store {
    map: BTreeMap<String, String>,
    wal: Wal,
    /// Number of records currently in the WAL (including overwritten and
    /// deleted keys). Used to decide when auto-compaction is worthwhile.
    wal_record_count: usize,
}

/// When the WAL grows to more than this many records past what the live
/// key count needs, `set`/`delete` will automatically trigger a compaction.
/// Kept generous so compaction is infrequent under normal use, but this is
/// exposed as a constant (rather than hardcoded inline) so callers/tests can
/// reason about the threshold.
pub const AUTO_COMPACT_SLACK: usize = 64;

impl Store {
    /// Open a store whose WAL lives at `path`, creating the file if it does
    /// not already exist. Replays the WAL to rebuild in-memory state.
    pub fn open<P: AsRef<Path>>(path: P) -> io::Result<Self> {
        let path: PathBuf = path.as_ref().to_path_buf();
        let records = Wal::replay(&path)?;
        let mut map = BTreeMap::new();
        for record in &records {
            match record {
                Record::Set(k, v) => {
                    map.insert(k.clone(), v.clone());
                }
                Record::Delete(k) => {
                    map.remove(k);
                }
            }
        }
        let wal = Wal::open(&path)?;
        Ok(Store {
            map,
            wal,
            wal_record_count: records.len(),
        })
    }

    /// Insert or overwrite `key` with `value`. Durably appended to the WAL
    /// before the in-memory map is updated.
    pub fn set(&mut self, key: &str, value: &str) -> io::Result<()> {
        self.wal.append_set(key, value)?;
        self.map.insert(key.to_string(), value.to_string());
        self.wal_record_count += 1;
        self.maybe_auto_compact()?;
        Ok(())
    }

    /// Look up `key`, returning its current value if present.
    pub fn get(&self, key: &str) -> Option<&str> {
        self.map.get(key).map(|s| s.as_str())
    }

    /// Remove `key` if present, returning whether it existed. Durably
    /// appended to the WAL before the in-memory map is updated.
    pub fn delete(&mut self, key: &str) -> io::Result<bool> {
        let existed = self.map.remove(key).is_some();
        if existed {
            self.wal.append_delete(key)?;
            self.wal_record_count += 1;
            self.maybe_auto_compact()?;
        }
        Ok(existed)
    }

    /// Number of live keys currently in the store.
    pub fn len(&self) -> usize {
        self.map.len()
    }

    /// Whether the store currently holds no keys.
    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }

    /// Iterate over all live key-value pairs in key order.
    pub fn iter(&self) -> impl Iterator<Item = (&str, &str)> {
        self.map.iter().map(|(k, v)| (k.as_str(), v.as_str()))
    }

    /// Force a WAL compaction now, rewriting the log to contain exactly one
    /// record per live key.
    pub fn compact(&mut self) -> io::Result<()> {
        self.wal.compact(&self.map)?;
        self.wal_record_count = self.map.len();
        Ok(())
    }

    /// Path to the underlying WAL file.
    pub fn wal_path(&self) -> &Path {
        self.wal.path()
    }

    /// Number of raw records currently in the WAL (live + stale).
    pub fn wal_record_count(&self) -> usize {
        self.wal_record_count
    }

    fn maybe_auto_compact(&mut self) -> io::Result<()> {
        if self.wal_record_count > self.map.len() + AUTO_COMPACT_SLACK {
            self.compact()?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp_path(name: &str) -> PathBuf {
        let mut p = std::env::temp_dir();
        p.push(format!(
            "kvstore_store_test_{}_{}_{}",
            name,
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        p
    }

    #[test]
    fn set_and_get() {
        let path = temp_path("set_get");
        let mut store = Store::open(&path).unwrap();
        store.set("foo", "bar").unwrap();
        assert_eq!(store.get("foo"), Some("bar"));
        assert_eq!(store.get("missing"), None);
        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn overwrite_updates_value() {
        let path = temp_path("overwrite");
        let mut store = Store::open(&path).unwrap();
        store.set("k", "v1").unwrap();
        store.set("k", "v2").unwrap();
        assert_eq!(store.get("k"), Some("v2"));
        assert_eq!(store.len(), 1);
        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn delete_removes_key() {
        let path = temp_path("delete");
        let mut store = Store::open(&path).unwrap();
        store.set("k", "v").unwrap();
        assert!(store.delete("k").unwrap());
        assert_eq!(store.get("k"), None);
        assert!(!store.delete("k").unwrap());
        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn reopen_replays_wal() {
        let path = temp_path("reopen");
        {
            let mut store = Store::open(&path).unwrap();
            store.set("a", "1").unwrap();
            store.set("b", "2").unwrap();
            store.delete("a").unwrap();
            store.set("c", "3").unwrap();
        }
        let store = Store::open(&path).unwrap();
        assert_eq!(store.get("a"), None);
        assert_eq!(store.get("b"), Some("2"));
        assert_eq!(store.get("c"), Some("3"));
        assert_eq!(store.len(), 2);
        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn compact_shrinks_wal_record_count() {
        let path = temp_path("compact");
        let mut store = Store::open(&path).unwrap();
        store.set("a", "1").unwrap();
        store.set("a", "2").unwrap();
        store.set("a", "3").unwrap();
        store.set("b", "x").unwrap();
        store.delete("b").unwrap();
        assert_eq!(store.wal_record_count(), 5);

        store.compact().unwrap();
        assert_eq!(store.wal_record_count(), 1);
        assert_eq!(store.get("a"), Some("3"));

        // Data should still be correct after reopening post-compaction.
        let reopened = Store::open(&path).unwrap();
        assert_eq!(reopened.get("a"), Some("3"));
        assert_eq!(reopened.len(), 1);
        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn auto_compact_triggers_past_slack_threshold() {
        let path = temp_path("auto_compact");
        let mut store = Store::open(&path).unwrap();
        for i in 0..(AUTO_COMPACT_SLACK + 5) {
            store.set("only-key", &i.to_string()).unwrap();
        }
        // Repeated overwrites of a single key should have triggered at
        // least one auto-compaction, collapsing the WAL back down near 1.
        assert!(store.wal_record_count() < AUTO_COMPACT_SLACK);
        assert_eq!(store.get("only-key"), Some((AUTO_COMPACT_SLACK + 4).to_string().as_str()));
        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn iter_yields_keys_in_order() {
        let path = temp_path("iter");
        let mut store = Store::open(&path).unwrap();
        store.set("banana", "2").unwrap();
        store.set("apple", "1").unwrap();
        store.set("cherry", "3").unwrap();
        let keys: Vec<&str> = store.iter().map(|(k, _)| k).collect();
        assert_eq!(keys, vec!["apple", "banana", "cherry"]);
        std::fs::remove_file(&path).ok();
    }
}
