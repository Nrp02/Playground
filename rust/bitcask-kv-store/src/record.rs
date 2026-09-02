use crate::crc::Crc32;
use crate::error::{Error, Result};

pub const HEADER_LEN: usize = 20;

pub const TOMBSTONE_LEN: u32 = u32::MAX;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RecordHeader {
    pub crc: u32,
    pub timestamp: u64,
    pub key_len: u32,
    pub value_len: u32,
}

impl RecordHeader {
    pub fn is_tombstone(&self) -> bool {
        self.value_len == TOMBSTONE_LEN
    }

    pub fn stored_value_len(&self) -> u64 {
        if self.is_tombstone() {
            0
        } else {
            self.value_len as u64
        }
    }

    pub fn payload_len(&self) -> u64 {
        self.key_len as u64 + self.stored_value_len()
    }

    pub fn total_len(&self) -> u64 {
        HEADER_LEN as u64 + self.payload_len()
    }
}

pub fn encode(timestamp: u64, key: &[u8], value: Option<&[u8]>) -> Result<Vec<u8>> {
    if key.len() > u32::MAX as usize {
        return Err(Error::KeyTooLarge(key.len()));
    }
    let value_len = match value {
        Some(bytes) => {
            if bytes.len() >= TOMBSTONE_LEN as usize {
                return Err(Error::ValueTooLarge(bytes.len()));
            }
            bytes.len() as u32
        }
        None => TOMBSTONE_LEN,
    };
    let payload = key.len() + value.map_or(0, |bytes| bytes.len());
    let mut out = Vec::with_capacity(HEADER_LEN + payload);
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&timestamp.to_le_bytes());
    out.extend_from_slice(&(key.len() as u32).to_le_bytes());
    out.extend_from_slice(&value_len.to_le_bytes());
    out.extend_from_slice(key);
    if let Some(bytes) = value {
        out.extend_from_slice(bytes);
    }
    let mut digest = Crc32::new();
    digest.update(&out[4..]);
    out[..4].copy_from_slice(&digest.finish().to_le_bytes());
    Ok(out)
}

pub fn parse_header(bytes: &[u8]) -> RecordHeader {
    RecordHeader {
        crc: read_u32(&bytes[0..4]),
        timestamp: read_u64(&bytes[4..12]),
        key_len: read_u32(&bytes[12..16]),
        value_len: read_u32(&bytes[16..20]),
    }
}

pub fn verify(header_bytes: &[u8], payload: &[u8]) -> bool {
    let mut digest = Crc32::new();
    digest.update(&header_bytes[4..HEADER_LEN]);
    digest.update(payload);
    digest.finish() == read_u32(&header_bytes[0..4])
}

pub fn read_u32(bytes: &[u8]) -> u32 {
    u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]])
}

pub fn read_u64(bytes: &[u8]) -> u64 {
    u64::from_le_bytes([
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
    ])
}
