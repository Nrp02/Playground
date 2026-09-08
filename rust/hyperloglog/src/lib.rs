pub mod dense;
pub mod error;
pub mod hash;
pub mod sparse;

use std::collections::BTreeMap;

use dense::DenseRegisters;
pub use error::{HllError, Result};
use hash::{fnv1a, hash64};
use sparse::{downgrade, encode, linear_counting, SPARSE_PRECISION};

pub const MIN_PRECISION: u8 = 4;
pub const MAX_PRECISION: u8 = 18;
pub const DEFAULT_PRECISION: u8 = 14;

const MAGIC: [u8; 4] = *b"HLL1";
const FORMAT_VERSION: u8 = 1;
const MODE_SPARSE: u8 = 0;
const MODE_DENSE: u8 = 1;
const SPARSE_ENTRY_BYTES: usize = 5;

#[derive(Debug, Clone, PartialEq, Eq)]
enum Storage {
    Sparse(BTreeMap<u32, u8>),
    Dense(DenseRegisters),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HyperLogLog {
    precision: u8,
    storage: Storage,
}

impl Default for HyperLogLog {
    fn default() -> Self {
        HyperLogLog::new(DEFAULT_PRECISION).expect("default precision is valid")
    }
}

impl HyperLogLog {
    pub fn new(precision: u8) -> Result<Self> {
        if !(MIN_PRECISION..=MAX_PRECISION).contains(&precision) {
            return Err(HllError::InvalidPrecision(precision));
        }
        Ok(HyperLogLog {
            precision,
            storage: Storage::Sparse(BTreeMap::new()),
        })
    }

    pub fn precision(&self) -> u8 {
        self.precision
    }

    pub fn registers(&self) -> usize {
        1usize << self.precision
    }

    pub fn is_sparse(&self) -> bool {
        matches!(self.storage, Storage::Sparse(_))
    }

    pub fn standard_error(&self) -> f64 {
        1.04 / (self.registers() as f64).sqrt()
    }

    pub fn memory_bytes(&self) -> usize {
        match &self.storage {
            Storage::Sparse(entries) => entries.len() * SPARSE_ENTRY_BYTES,
            Storage::Dense(registers) => registers.byte_len(),
        }
    }

    pub fn clear(&mut self) {
        self.storage = Storage::Sparse(BTreeMap::new());
    }

    pub fn add(&mut self, item: &[u8]) {
        self.add_hash(hash64(item));
    }

    pub fn add_str(&mut self, item: &str) {
        self.add(item.as_bytes());
    }

    pub fn add_u64(&mut self, item: u64) {
        self.add(&item.to_le_bytes());
    }

    pub fn add_hash(&mut self, hash: u64) {
        let (index, rho) = encode(hash);
        self.insert_sparse_pair(index, rho);
    }

    fn insert_sparse_pair(&mut self, index: u32, rho: u8) {
        let overflowed = match &mut self.storage {
            Storage::Sparse(entries) => {
                let slot = entries.entry(index).or_insert(0);
                if rho > *slot {
                    *slot = rho;
                }
                entries.len() * SPARSE_ENTRY_BYTES > dense_byte_len(self.precision)
            }
            Storage::Dense(registers) => {
                let (dense_index, dense_rho) = downgrade(index, rho, self.precision);
                let _ = registers.set_max(dense_index, dense_rho);
                false
            }
        };
        if overflowed {
            self.convert_to_dense();
        }
    }

    fn convert_to_dense(&mut self) {
        let entries = match &mut self.storage {
            Storage::Sparse(entries) => std::mem::take(entries),
            Storage::Dense(_) => return,
        };
        let mut registers = DenseRegisters::new(self.registers());
        for (index, rho) in entries {
            let (dense_index, dense_rho) = downgrade(index, rho, self.precision);
            let _ = registers.set_max(dense_index, dense_rho);
        }
        self.storage = Storage::Dense(registers);
    }

    pub fn estimate(&self) -> f64 {
        match &self.storage {
            Storage::Sparse(entries) => {
                let registers = (1u64 << SPARSE_PRECISION) as f64;
                let zeros = registers - entries.len() as f64;
                if zeros <= 0.0 {
                    registers
                } else {
                    linear_counting(registers, zeros)
                }
            }
            Storage::Dense(registers) => {
                let m = registers.len() as f64;
                let raw = alpha(registers.len()) * m * m / registers.inverse_power_sum();
                let zeros = registers.zeros();
                if zeros > 0 && raw <= 2.5 * m {
                    linear_counting(m, zeros as f64)
                } else {
                    raw
                }
            }
        }
    }

    pub fn len(&self) -> u64 {
        self.estimate().round() as u64
    }

    pub fn is_empty(&self) -> bool {
        match &self.storage {
            Storage::Sparse(entries) => entries.is_empty(),
            Storage::Dense(registers) => registers.zeros() == registers.len(),
        }
    }

    pub fn merge(&mut self, other: &HyperLogLog) -> Result<()> {
        if self.precision != other.precision {
            return Err(HllError::PrecisionMismatch {
                left: self.precision,
                right: other.precision,
            });
        }
        match &other.storage {
            Storage::Sparse(entries) => {
                let pairs: Vec<(u32, u8)> = entries.iter().map(|(k, v)| (*k, *v)).collect();
                for (index, rho) in pairs {
                    self.insert_sparse_pair(index, rho);
                }
            }
            Storage::Dense(other_registers) => {
                self.convert_to_dense();
                if let Storage::Dense(registers) = &mut self.storage {
                    registers.merge_from(other_registers)?;
                }
            }
        }
        Ok(())
    }

    pub fn union(&self, other: &HyperLogLog) -> Result<HyperLogLog> {
        let mut merged = self.clone();
        merged.merge(other)?;
        Ok(merged)
    }

    pub fn estimate_intersection(&self, other: &HyperLogLog) -> Result<f64> {
        let union = self.union(other)?;
        Ok((self.estimate() + other.estimate() - union.estimate()).max(0.0))
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(self.memory_bytes() + 24);
        out.extend_from_slice(&MAGIC);
        out.push(FORMAT_VERSION);
        out.push(self.precision);
        match &self.storage {
            Storage::Sparse(entries) => {
                out.push(MODE_SPARSE);
                out.extend_from_slice(&(entries.len() as u32).to_le_bytes());
                for (index, rho) in entries {
                    out.extend_from_slice(&index.to_le_bytes());
                    out.push(*rho);
                }
            }
            Storage::Dense(registers) => {
                out.push(MODE_DENSE);
                out.extend_from_slice(&(registers.byte_len() as u32).to_le_bytes());
                out.extend_from_slice(registers.as_bytes());
            }
        }
        let checksum = fnv1a(&out);
        out.extend_from_slice(&checksum.to_le_bytes());
        out
    }

    pub fn from_bytes(bytes: &[u8]) -> Result<HyperLogLog> {
        if bytes.len() < 19 {
            return Err(HllError::Truncated {
                expected: 19,
                got: bytes.len(),
            });
        }
        if bytes[0..4] != MAGIC {
            return Err(HllError::BadMagic);
        }
        if bytes[4] != FORMAT_VERSION {
            return Err(HllError::UnsupportedVersion(bytes[4]));
        }
        let body = &bytes[..bytes.len() - 8];
        let mut stored = [0u8; 8];
        stored.copy_from_slice(&bytes[bytes.len() - 8..]);
        let expected = u64::from_le_bytes(stored);
        let actual = fnv1a(body);
        if expected != actual {
            return Err(HllError::ChecksumMismatch {
                expected,
                got: actual,
            });
        }

        let precision = body[5];
        if !(MIN_PRECISION..=MAX_PRECISION).contains(&precision) {
            return Err(HllError::InvalidPrecision(precision));
        }
        let mode = body[6];
        let mut count_bytes = [0u8; 4];
        count_bytes.copy_from_slice(&body[7..11]);
        let count = u32::from_le_bytes(count_bytes) as usize;
        let payload = &body[11..];

        let storage = match mode {
            MODE_SPARSE => {
                let expected_len = count * SPARSE_ENTRY_BYTES;
                if payload.len() != expected_len {
                    return Err(HllError::Truncated {
                        expected: expected_len,
                        got: payload.len(),
                    });
                }
                let mut entries = BTreeMap::new();
                for chunk in payload.chunks_exact(SPARSE_ENTRY_BYTES) {
                    let mut index_bytes = [0u8; 4];
                    index_bytes.copy_from_slice(&chunk[..4]);
                    entries.insert(u32::from_le_bytes(index_bytes), chunk[4]);
                }
                Storage::Sparse(entries)
            }
            MODE_DENSE => {
                if payload.len() != count {
                    return Err(HllError::Truncated {
                        expected: count,
                        got: payload.len(),
                    });
                }
                Storage::Dense(DenseRegisters::from_bytes(
                    1usize << precision,
                    payload.to_vec(),
                )?)
            }
            other => return Err(HllError::UnknownMode(other)),
        };

        Ok(HyperLogLog { precision, storage })
    }
}

fn dense_byte_len(precision: u8) -> usize {
    ((1usize << precision) * dense::REGISTER_BITS + 7) / 8
}

fn alpha(registers: usize) -> f64 {
    match registers {
        16 => 0.673,
        32 => 0.697,
        64 => 0.709,
        _ => 0.7213 / (1.0 + 1.079 / registers as f64),
    }
}
