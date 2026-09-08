pub const SPARSE_PRECISION: u8 = 25;

pub fn encode(hash: u64) -> (u32, u8) {
    let index = (hash >> (64 - SPARSE_PRECISION as u32)) as u32;
    let tail = hash << SPARSE_PRECISION as u32;
    let rho = if tail == 0 {
        64 - SPARSE_PRECISION + 1
    } else {
        tail.leading_zeros() as u8 + 1
    };
    (index, rho)
}

pub fn downgrade(index: u32, rho: u8, precision: u8) -> (usize, u8) {
    let dropped = SPARSE_PRECISION - precision;
    let dense_index = (index >> dropped as u32) as usize;
    let middle = index & ((1u32 << dropped as u32) - 1);
    let dense_rho = if middle != 0 {
        (dropped as u32 - (32 - middle.leading_zeros()) + 1) as u8
    } else {
        dropped + rho
    };
    (dense_index, dense_rho)
}

pub fn linear_counting(registers: f64, zeros: f64) -> f64 {
    registers * (registers / zeros).ln()
}
