use hyperloglog::dense::{DenseRegisters, MAX_REGISTER};
use hyperloglog::error::HllError;
use hyperloglog::hash::mix64;
use hyperloglog::sparse::{downgrade, encode, SPARSE_PRECISION};

fn rng(state: &mut u64) -> u64 {
    *state = state.wrapping_add(0x9e37_79b9_7f4a_7c15);
    mix64(*state)
}

#[test]
fn packed_registers_round_trip_every_index() {
    let len = 1000;
    let mut registers = DenseRegisters::new(len);
    let mut expected = vec![0u8; len];
    let mut state = 7u64;
    for index in 0..len {
        let value = (rng(&mut state) % (MAX_REGISTER as u64 + 1)) as u8;
        expected[index] = value;
        registers.set_max(index, value).expect("value fits");
    }
    for index in 0..len {
        assert_eq!(registers.get(index), expected[index], "index {}", index);
    }
}

#[test]
fn adjacent_registers_do_not_bleed_into_each_other() {
    let mut registers = DenseRegisters::new(64);
    for index in 0..64 {
        registers.set_max(index, MAX_REGISTER).expect("value fits");
    }
    for index in (0..64).step_by(2) {
        assert_eq!(registers.get(index), MAX_REGISTER);
    }
    let mut fresh = DenseRegisters::new(9);
    fresh.set_max(4, 63).expect("value fits");
    for index in 0..9 {
        let expected = if index == 4 { 63 } else { 0 };
        assert_eq!(fresh.get(index), expected, "index {}", index);
    }
}

#[test]
fn set_max_never_lowers_a_register_and_tracks_zeros() {
    let mut registers = DenseRegisters::new(16);
    assert_eq!(registers.zeros(), 16);
    assert!(registers.set_max(3, 9).expect("value fits"));
    assert_eq!(registers.zeros(), 15);
    assert!(!registers.set_max(3, 4).expect("value fits"));
    assert_eq!(registers.get(3), 9);
    assert!(registers.set_max(3, 11).expect("value fits"));
    assert_eq!(registers.get(3), 11);
    assert_eq!(registers.zeros(), 15);
}

#[test]
fn oversized_register_values_are_rejected() {
    let mut registers = DenseRegisters::new(8);
    assert_eq!(
        registers.set_max(0, 64),
        Err(HllError::RegisterOutOfRange(64))
    );
}

#[test]
fn from_bytes_rejects_a_wrong_length_payload() {
    let registers = DenseRegisters::new(128);
    let mut bytes = registers.as_bytes().to_vec();
    bytes.pop();
    match DenseRegisters::from_bytes(128, bytes) {
        Err(HllError::Truncated { expected, got }) => {
            assert_eq!(expected, 96);
            assert_eq!(got, 95);
        }
        other => panic!("expected a truncation error, got {:?}", other),
    }
}

#[test]
fn inverse_power_sum_matches_a_naive_walk() {
    let len = 300;
    let mut registers = DenseRegisters::new(len);
    let mut state = 99u64;
    let mut naive = 0.0f64;
    for index in 0..len {
        let value = (rng(&mut state) % 20) as u8;
        registers.set_max(index, value).expect("value fits");
    }
    for index in 0..len {
        naive += 1.0 / ((1u64 << registers.get(index)) as f64);
    }
    assert!((registers.inverse_power_sum() - naive).abs() < 1e-9);
}

#[test]
fn merging_dense_registers_takes_the_elementwise_maximum() {
    let mut left = DenseRegisters::new(50);
    let mut right = DenseRegisters::new(50);
    for index in 0..50 {
        left.set_max(index, (index % 30) as u8).expect("value fits");
        right.set_max(index, ((index * 7) % 30) as u8).expect("value fits");
    }
    let mut merged = left.clone();
    merged.merge_from(&right).expect("same length");
    for index in 0..50 {
        assert_eq!(merged.get(index), left.get(index).max(right.get(index)));
    }
}

#[test]
fn sparse_encoding_matches_a_direct_dense_computation() {
    let mut state = 4242u64;
    for precision in [4u8, 8, 11, 14, 18] {
        for _ in 0..20_000 {
            let hash = rng(&mut state);
            let (sparse_index, sparse_rho) = encode(hash);
            let (index, rho) = downgrade(sparse_index, sparse_rho, precision);

            let expected_index = (hash >> (64 - precision as u32)) as usize;
            let tail = hash << precision as u32;
            let expected_rho = if tail == 0 {
                64 - precision + 1
            } else {
                tail.leading_zeros() as u8 + 1
            };
            assert_eq!(index, expected_index, "precision {}", precision);
            assert_eq!(rho, expected_rho, "precision {}", precision);
            assert!(rho <= MAX_REGISTER);
        }
    }
}

#[test]
fn sparse_encoding_handles_the_all_zero_tail() {
    let hash = 0x2aaau64 << 50;
    let (index, rho) = encode(hash);
    assert_eq!(index, (hash >> (64 - SPARSE_PRECISION as u32)) as u32);
    assert_eq!(rho, 40);
    let (dense_index, dense_rho) = downgrade(index, rho, 14);
    assert_eq!(dense_index, 0x2aaa);
    assert_eq!(dense_rho, 51);
}
