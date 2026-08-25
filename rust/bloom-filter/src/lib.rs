use std::collections::hash_map::DefaultHasher;
use std::hash::Hash;
use std::hash::Hasher;

pub struct BloomFilter {
    bits: Vec<u64>,
    num_bits: usize,
    num_hashes: usize,
    num_inserted: usize,
}

impl BloomFilter {
    pub fn new(expected_items: usize, target_false_positive_rate: f64) -> Self {
        let expected_items = expected_items.max(1);
        let p = target_false_positive_rate.clamp(1e-9, 0.9);
        let ln2 = std::f64::consts::LN_2;
        let raw_bits = -(expected_items as f64) * p.ln() / (ln2 * ln2);
        let num_bits = (raw_bits.ceil() as usize).max(64);
        let raw_hashes = (num_bits as f64 / expected_items as f64) * ln2;
        let num_hashes = (raw_hashes.round() as usize).clamp(1, 32);
        BloomFilter::with_params(num_bits, num_hashes)
    }

    pub fn with_params(num_bits: usize, num_hashes: usize) -> Self {
        let num_bits = num_bits.max(1);
        let num_hashes = num_hashes.max(1);
        let words = (num_bits + 63) / 64;
        BloomFilter {
            bits: vec![0u64; words],
            num_bits,
            num_hashes,
            num_inserted: 0,
        }
    }

    fn base_hashes<T: Hash>(item: &T) -> (u64, u64) {
        let mut h1 = DefaultHasher::new();
        item.hash(&mut h1);
        let first = h1.finish();

        let mut h2 = DefaultHasher::new();
        item.hash(&mut h2);
        0x9E3779B97F4A7C15u64.hash(&mut h2);
        let second = h2.finish() | 1;

        (first, second)
    }

    fn slot_indices<T: Hash>(&self, item: &T) -> Vec<usize> {
        let (h1, h2) = BloomFilter::base_hashes(item);
        let m = self.num_bits as u64;
        let mut indices = Vec::with_capacity(self.num_hashes);
        for i in 0..self.num_hashes {
            let combined = h1.wrapping_add((i as u64).wrapping_mul(h2));
            indices.push((combined % m) as usize);
        }
        indices
    }

    fn set_bit(&mut self, index: usize) {
        let word = index / 64;
        let offset = index % 64;
        self.bits[word] |= 1u64 << offset;
    }

    fn get_bit(&self, index: usize) -> bool {
        let word = index / 64;
        let offset = index % 64;
        (self.bits[word] >> offset) & 1 == 1
    }

    pub fn insert<T: Hash>(&mut self, item: &T) {
        let indices = self.slot_indices(item);
        for index in indices {
            self.set_bit(index);
        }
        self.num_inserted += 1;
    }

    pub fn contains<T: Hash>(&self, item: &T) -> bool {
        let indices = self.slot_indices(item);
        indices.iter().all(|&index| self.get_bit(index))
    }

    pub fn len(&self) -> usize {
        self.num_inserted
    }

    pub fn is_empty(&self) -> bool {
        self.num_inserted == 0
    }

    pub fn num_bits(&self) -> usize {
        self.num_bits
    }

    pub fn num_hashes(&self) -> usize {
        self.num_hashes
    }

    pub fn fill_ratio(&self) -> f64 {
        let set_bits: u32 = self.bits.iter().map(|word| word.count_ones()).sum();
        set_bits as f64 / self.num_bits as f64
    }

    pub fn estimated_false_positive_rate(&self) -> f64 {
        self.fill_ratio().powi(self.num_hashes as i32)
    }
}
