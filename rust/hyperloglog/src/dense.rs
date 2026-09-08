use crate::error::{HllError, Result};

pub const REGISTER_BITS: usize = 6;
pub const MAX_REGISTER: u8 = (1 << REGISTER_BITS) - 1;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DenseRegisters {
    words: Vec<u8>,
    len: usize,
    zeros: usize,
}

impl DenseRegisters {
    pub fn new(len: usize) -> Self {
        let bytes = (len * REGISTER_BITS + 7) / 8;
        DenseRegisters {
            words: vec![0u8; bytes],
            len,
            zeros: len,
        }
    }

    pub fn from_bytes(len: usize, words: Vec<u8>) -> Result<Self> {
        let expected = (len * REGISTER_BITS + 7) / 8;
        if words.len() != expected {
            return Err(HllError::Truncated {
                expected,
                got: words.len(),
            });
        }
        let mut registers = DenseRegisters {
            words,
            len,
            zeros: 0,
        };
        registers.zeros = (0..len).filter(|i| registers.get(*i) == 0).count();
        Ok(registers)
    }

    pub fn len(&self) -> usize {
        self.len
    }

    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.words
    }

    pub fn byte_len(&self) -> usize {
        self.words.len()
    }

    pub fn zeros(&self) -> usize {
        self.zeros
    }

    pub fn get(&self, index: usize) -> u8 {
        let bit = index * REGISTER_BITS;
        let byte = bit / 8;
        let shift = bit % 8;
        let low = (self.words[byte] >> shift) & MAX_REGISTER;
        if shift <= 2 {
            low
        } else {
            let taken = 8 - shift;
            let high = self.words[byte + 1] << taken;
            (low | high) & MAX_REGISTER
        }
    }

    pub fn set_max(&mut self, index: usize, value: u8) -> Result<bool> {
        if value > MAX_REGISTER {
            return Err(HllError::RegisterOutOfRange(value));
        }
        let current = self.get(index);
        if value <= current {
            return Ok(false);
        }
        if current == 0 {
            self.zeros -= 1;
        }
        self.write(index, value);
        Ok(true)
    }

    fn write(&mut self, index: usize, value: u8) {
        let bit = index * REGISTER_BITS;
        let byte = bit / 8;
        let shift = bit % 8;
        let mask = (MAX_REGISTER as u16) << shift;
        let payload = (value as u16) << shift;
        self.words[byte] = ((self.words[byte] as u16 & !mask) | (payload & 0x00ff)) as u8;
        if shift > 2 {
            let high_mask = (mask >> 8) as u8;
            let high_payload = (payload >> 8) as u8;
            self.words[byte + 1] = (self.words[byte + 1] & !high_mask) | high_payload;
        }
    }

    pub fn merge_from(&mut self, other: &DenseRegisters) -> Result<()> {
        if other.len != self.len {
            return Err(HllError::Truncated {
                expected: self.len,
                got: other.len,
            });
        }
        for index in 0..self.len {
            self.set_max(index, other.get(index))?;
        }
        Ok(())
    }

    pub fn inverse_power_sum(&self) -> f64 {
        let mut sum = 0.0f64;
        for index in 0..self.len {
            sum += 1.0 / ((1u64 << self.get(index)) as f64);
        }
        sum
    }
}
