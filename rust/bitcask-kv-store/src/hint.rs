use crate::crc::Crc32;
use crate::error::{Error, Result};
use crate::keydir::EntryPointer;
use crate::record::{parse_header, read_u32, read_u64};
use std::fs::File;
use std::io::{BufReader, Read};
use std::path::Path;

pub const HINT_HEADER_LEN: usize = 28;

pub fn encode(key: &[u8], pointer: &EntryPointer) -> Vec<u8> {
    let mut out = Vec::with_capacity(HINT_HEADER_LEN + key.len());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&pointer.timestamp.to_le_bytes());
    out.extend_from_slice(&pointer.key_len.to_le_bytes());
    out.extend_from_slice(&pointer.value_len.to_le_bytes());
    out.extend_from_slice(&pointer.record_offset.to_le_bytes());
    out.extend_from_slice(key);
    let mut digest = Crc32::new();
    digest.update(&out[4..]);
    out[..4].copy_from_slice(&digest.finish().to_le_bytes());
    out
}

pub fn load(path: &Path, file_id: u64) -> Result<Vec<(Vec<u8>, EntryPointer)>> {
    let label = || path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    let file = File::open(path)?;
    let file_len = file.metadata()?.len();
    let mut reader = BufReader::new(file);
    let mut header = [0u8; HINT_HEADER_LEN];
    let mut offset = 0u64;
    let mut out = Vec::new();
    while offset < file_len {
        if file_len - offset < HINT_HEADER_LEN as u64 {
            return Err(Error::BadHintFile(label()));
        }
        reader.read_exact(&mut header)?;
        let stub = parse_header(&header[..]);
        let key_len = stub.key_len as u64;
        let record_offset = read_u64(&header[20..28]);
        if file_len - offset - (HINT_HEADER_LEN as u64) < key_len {
            return Err(Error::BadHintFile(label()));
        }
        let mut key = vec![0u8; key_len as usize];
        reader.read_exact(&mut key)?;
        let mut digest = Crc32::new();
        digest.update(&header[4..]);
        digest.update(&key);
        if digest.finish() != read_u32(&header[0..4]) {
            return Err(Error::BadHintFile(label()));
        }
        out.push((
            key,
            EntryPointer {
                file_id,
                record_offset,
                key_len: stub.key_len,
                value_len: stub.value_len,
                timestamp: stub.timestamp,
            },
        ));
        offset += HINT_HEADER_LEN as u64 + key_len;
    }
    Ok(out)
}
