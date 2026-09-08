use std::collections::HashSet;

use hyperloglog::hash::mix64;
use hyperloglog::{HllError, HyperLogLog, DEFAULT_PRECISION};

fn key(prefix: &str, i: u64) -> String {
    format!("{}-{}", prefix, i)
}

fn filled(prefix: &str, range: std::ops::Range<u64>) -> HyperLogLog {
    let mut sketch = HyperLogLog::default();
    for i in range {
        sketch.add_str(&key(prefix, i));
    }
    sketch
}

fn relative_error(sketch: &HyperLogLog, exact: u64) -> f64 {
    (sketch.estimate() - exact as f64).abs() / exact as f64
}

#[test]
fn precision_outside_the_supported_range_is_rejected() {
    assert_eq!(HyperLogLog::new(3).unwrap_err(), HllError::InvalidPrecision(3));
    assert_eq!(HyperLogLog::new(19).unwrap_err(), HllError::InvalidPrecision(19));
    assert!(HyperLogLog::new(4).is_ok());
    assert!(HyperLogLog::new(18).is_ok());
    assert_eq!(HyperLogLog::default().precision(), DEFAULT_PRECISION);
}

#[test]
fn an_empty_sketch_counts_nothing() {
    let sketch = HyperLogLog::default();
    assert!(sketch.is_empty());
    assert!(sketch.is_sparse());
    assert_eq!(sketch.len(), 0);
    assert_eq!(sketch.memory_bytes(), 0);
}

#[test]
fn small_cardinalities_are_exact_in_sparse_mode() {
    for exact in [1u64, 2, 17, 250, 1500] {
        let sketch = filled("small", 0..exact);
        assert!(sketch.is_sparse(), "n={} should still be sparse", exact);
        assert_eq!(sketch.len(), exact, "n={}", exact);
    }
}

#[test]
fn repeated_additions_do_not_change_the_estimate() {
    let mut sketch = HyperLogLog::default();
    for _ in 0..25 {
        for i in 0..5_000u64 {
            sketch.add_str(&key("repeat", i));
        }
    }
    assert!(relative_error(&sketch, 5_000) < 0.03, "estimate {}", sketch.estimate());
}

#[test]
fn insertion_order_does_not_affect_the_sketch() {
    let forward = filled("order", 0..40_000);
    let mut backward = HyperLogLog::default();
    for i in (0..40_000u64).rev() {
        backward.add_str(&key("order", i));
    }
    assert!(!forward.is_sparse());
    assert_eq!(forward.to_bytes(), backward.to_bytes());
}

#[test]
fn the_sparse_to_dense_switch_preserves_the_estimate() {
    let mut sketch = HyperLogLog::default();
    let mut before = 0.0f64;
    let mut count = 0u64;
    while sketch.is_sparse() {
        sketch.add_str(&key("switch", count));
        count += 1;
        if sketch.is_sparse() {
            before = sketch.estimate();
        }
    }
    assert!(!sketch.is_sparse());
    assert!(count > 2_000, "switched far too early at {}", count);
    assert!(
        (sketch.estimate() - before).abs() / before < 0.05,
        "sparse {} then dense {}",
        before,
        sketch.estimate()
    );
    assert_eq!(sketch.memory_bytes(), 16_384 * 6 / 8);
}

#[test]
fn dense_estimates_stay_within_three_standard_errors() {
    for exact in [50_000u64, 200_000, 750_000] {
        let sketch = filled("scale", 0..exact);
        let bound = 3.0 * sketch.standard_error();
        assert!(
            relative_error(&sketch, exact) < bound,
            "n={} estimate={} error={} bound={}",
            exact,
            sketch.estimate(),
            relative_error(&sketch, exact),
            bound
        );
    }
}

#[test]
fn low_precision_sketches_still_track_the_truth() {
    let mut sketch = HyperLogLog::new(6).expect("valid precision");
    for i in 0..30_000u64 {
        sketch.add_str(&key("tiny", i));
    }
    assert!(!sketch.is_sparse());
    assert!(
        relative_error(&sketch, 30_000) < 4.0 * sketch.standard_error(),
        "estimate {}",
        sketch.estimate()
    );
}

#[test]
fn hashed_values_and_typed_helpers_agree() {
    let mut by_bytes = HyperLogLog::default();
    let mut by_helper = HyperLogLog::default();
    for i in 0..1_000u64 {
        by_bytes.add(&i.to_le_bytes());
        by_helper.add_u64(i);
    }
    assert_eq!(by_bytes.to_bytes(), by_helper.to_bytes());
}

#[test]
fn merging_two_sparse_sketches_counts_the_union() {
    let left = filled("merge", 0..600);
    let right = filled("merge", 400..1_000);
    let union = left.union(&right).expect("same precision");
    assert!(union.is_sparse());
    assert_eq!(union.len(), 1_000);
}

#[test]
fn merging_a_dense_sketch_into_a_sparse_one_promotes_storage() {
    let mut sparse = filled("mix", 0..500);
    let dense = filled("mix", 0..120_000);
    assert!(sparse.is_sparse());
    assert!(!dense.is_sparse());
    sparse.merge(&dense).expect("same precision");
    assert!(!sparse.is_sparse());
    assert!(relative_error(&sparse, 120_000) < 3.0 * sparse.standard_error());
}

#[test]
fn merging_matches_a_sketch_built_from_every_element() {
    let mut combined = HyperLogLog::default();
    let mut parts = Vec::new();
    for shard in 0..4u64 {
        let mut part = HyperLogLog::default();
        for i in 0..50_000u64 {
            let item = key("shard", shard * 40_000 + i);
            part.add_str(&item);
            combined.add_str(&item);
        }
        parts.push(part);
    }
    let mut merged = parts[0].clone();
    for part in &parts[1..] {
        merged.merge(part).expect("same precision");
    }
    assert_eq!(merged.to_bytes(), combined.to_bytes());
}

#[test]
fn merging_different_precisions_is_an_error() {
    let mut left = HyperLogLog::new(12).expect("valid precision");
    let right = HyperLogLog::new(14).expect("valid precision");
    left.add_str("a");
    assert_eq!(
        left.merge(&right).unwrap_err(),
        HllError::PrecisionMismatch {
            left: 12,
            right: 14
        }
    );
}

#[test]
fn inclusion_exclusion_estimates_a_large_overlap() {
    let left = filled("overlap", 0..200_000);
    let right = filled("overlap", 150_000..350_000);
    let intersection = left.estimate_intersection(&right).expect("same precision");
    let error = (intersection - 50_000.0).abs() / 50_000.0;
    assert!(error < 0.2, "intersection estimate {}", intersection);
}

#[test]
fn merging_disjoint_sketches_reports_a_small_intersection() {
    let left = filled("disjoint-a", 0..100_000);
    let right = filled("disjoint-b", 0..100_000);
    let intersection = left.estimate_intersection(&right).expect("same precision");
    assert!(intersection < 10_000.0, "intersection estimate {}", intersection);
}

#[test]
fn sketches_survive_a_serialization_round_trip() {
    for exact in [900u64, 90_000] {
        let sketch = filled("codec", 0..exact);
        let bytes = sketch.to_bytes();
        let decoded = HyperLogLog::from_bytes(&bytes).expect("valid sketch");
        assert_eq!(decoded, sketch);
        assert_eq!(decoded.estimate().to_bits(), sketch.estimate().to_bits());
        assert_eq!(decoded.is_sparse(), sketch.is_sparse());
    }
}

#[test]
fn a_decoded_sketch_keeps_accepting_new_items() {
    let sketch = filled("resume", 0..30_000);
    let mut decoded = HyperLogLog::from_bytes(&sketch.to_bytes()).expect("valid sketch");
    for i in 30_000..60_000u64 {
        decoded.add_str(&key("resume", i));
    }
    assert!(relative_error(&decoded, 60_000) < 3.0 * decoded.standard_error());
}

#[test]
fn a_flipped_byte_is_caught_by_the_checksum() {
    let sketch = filled("corrupt", 0..80_000);
    let bytes = sketch.to_bytes();
    let mut state = 11u64;
    for _ in 0..200 {
        state = state.wrapping_add(0x9e37_79b9_7f4a_7c15);
        let position = (mix64(state) as usize) % bytes.len();
        let mut tampered = bytes.clone();
        tampered[position] ^= 1 << (mix64(state) % 8);
        match HyperLogLog::from_bytes(&tampered) {
            Err(HllError::ChecksumMismatch { .. }) => {}
            other => panic!("byte {} was not rejected: {:?}", position, other.map(|s| s.len())),
        }
    }
}

#[test]
fn malformed_headers_are_rejected() {
    let sketch = filled("header", 0..300);
    let bytes = sketch.to_bytes();

    assert_eq!(
        HyperLogLog::from_bytes(&bytes[..10]).unwrap_err(),
        HllError::Truncated {
            expected: 19,
            got: 10
        }
    );

    let mut wrong_magic = bytes.clone();
    wrong_magic[1] = b'X';
    assert_eq!(
        HyperLogLog::from_bytes(&wrong_magic).unwrap_err(),
        HllError::BadMagic
    );

    let mut wrong_version = bytes.clone();
    wrong_version[4] = 9;
    assert_eq!(
        HyperLogLog::from_bytes(&wrong_version).unwrap_err(),
        HllError::UnsupportedVersion(9)
    );
}

#[test]
fn an_unknown_storage_mode_is_rejected() {
    let sketch = filled("mode", 0..300);
    let mut bytes = sketch.to_bytes();
    let body_len = bytes.len() - 8;
    bytes[6] = 7;
    let checksum = hyperloglog::hash::fnv1a(&bytes[..body_len]);
    bytes[body_len..].copy_from_slice(&checksum.to_le_bytes());
    assert_eq!(
        HyperLogLog::from_bytes(&bytes).unwrap_err(),
        HllError::UnknownMode(7)
    );
}

#[test]
fn a_truncated_payload_is_rejected_even_with_a_valid_checksum() {
    let sketch = filled("cut", 0..300);
    let bytes = sketch.to_bytes();
    let body_len = bytes.len() - 8;
    let mut short = bytes[..body_len - 5].to_vec();
    let checksum = hyperloglog::hash::fnv1a(&short);
    short.extend_from_slice(&checksum.to_le_bytes());
    match HyperLogLog::from_bytes(&short) {
        Err(HllError::Truncated { .. }) => {}
        other => panic!("expected a truncation error, got {:?}", other.map(|s| s.len())),
    }
}

#[test]
fn clearing_returns_the_sketch_to_the_empty_sparse_state() {
    let mut sketch = filled("clear", 0..50_000);
    assert!(!sketch.is_sparse());
    sketch.clear();
    assert!(sketch.is_sparse());
    assert!(sketch.is_empty());
    assert_eq!(sketch.len(), 0);
    assert_eq!(sketch, HyperLogLog::default());
}

#[test]
fn the_sketch_counts_distinct_items_not_additions() {
    let mut sketch = HyperLogLog::default();
    let mut exact: HashSet<u64> = HashSet::new();
    let mut state = 2024u64;
    for _ in 0..400_000u64 {
        state = state.wrapping_add(0x9e37_79b9_7f4a_7c15);
        let value = mix64(state) % 120_000;
        sketch.add_u64(value);
        exact.insert(value);
    }
    let truth = exact.len() as u64;
    assert!(
        relative_error(&sketch, truth) < 3.0 * sketch.standard_error(),
        "estimate {} against {}",
        sketch.estimate(),
        truth
    );
}
