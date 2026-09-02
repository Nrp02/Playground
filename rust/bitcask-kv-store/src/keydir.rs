use crate::record::HEADER_LEN;
use std::collections::HashMap;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct EntryPointer {
    pub file_id: u64,
    pub record_offset: u64,
    pub key_len: u32,
    pub value_len: u32,
    pub timestamp: u64,
}

impl EntryPointer {
    pub fn value_offset(&self) -> u64 {
        self.record_offset + HEADER_LEN as u64 + self.key_len as u64
    }

    pub fn record_len(&self) -> u64 {
        HEADER_LEN as u64 + self.key_len as u64 + self.value_len as u64
    }
}

pub type KeyDir = HashMap<Vec<u8>, EntryPointer>;

pub fn apply_live(keydir: &mut KeyDir, key: Vec<u8>, pointer: EntryPointer) {
    match keydir.get(&key) {
        Some(existing) if existing.timestamp >= pointer.timestamp => {}
        _ => {
            keydir.insert(key, pointer);
        }
    }
}

pub fn apply_tombstone(keydir: &mut KeyDir, key: &[u8], timestamp: u64) {
    if let Some(existing) = keydir.get(key) {
        if existing.timestamp < timestamp {
            keydir.remove(key);
        }
    }
}
