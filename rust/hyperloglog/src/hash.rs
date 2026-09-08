const SEED: u64 = 0x9e37_79b9_7f4a_7c15;
const M1: u64 = 0xff51_afd7_ed55_8ccd;
const M2: u64 = 0xc4ce_b9fe_1a85_ec53;
const M3: u64 = 0x1656_67b1_9e37_79f9;

pub fn mix64(mut x: u64) -> u64 {
    x ^= x >> 33;
    x = x.wrapping_mul(M1);
    x ^= x >> 29;
    x = x.wrapping_mul(M2);
    x ^= x >> 32;
    x
}

pub fn hash64(data: &[u8]) -> u64 {
    hash64_seeded(data, SEED)
}

pub fn hash64_seeded(data: &[u8], seed: u64) -> u64 {
    let mut acc = seed ^ (data.len() as u64).wrapping_mul(M3);
    let mut chunks = data.chunks_exact(8);
    for chunk in chunks.by_ref() {
        let mut block = [0u8; 8];
        block.copy_from_slice(chunk);
        let k = u64::from_le_bytes(block);
        acc ^= mix64(k);
        acc = acc.rotate_left(27).wrapping_mul(M3).wrapping_add(M1);
    }

    let tail = chunks.remainder();
    if !tail.is_empty() {
        let mut block = [0u8; 8];
        block[..tail.len()].copy_from_slice(tail);
        block[7] = tail.len() as u8;
        let k = u64::from_le_bytes(block);
        acc ^= mix64(k);
        acc = acc.rotate_left(31).wrapping_mul(M2);
    }

    mix64(acc)
}

pub fn fnv1a(data: &[u8]) -> u64 {
    let mut h: u64 = 0xcbf2_9ce4_8422_2325;
    for byte in data {
        h ^= *byte as u64;
        h = h.wrapping_mul(0x1000_0000_01b3);
    }
    h
}
