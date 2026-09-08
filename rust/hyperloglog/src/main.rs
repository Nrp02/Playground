use std::collections::HashSet;

use hyperloglog::hash::fnv1a;
use hyperloglog::{HllError, HyperLogLog};

fn key(prefix: &str, i: u64) -> String {
    format!("{}-{}", prefix, i)
}

fn percent_error(estimate: f64, exact: u64) -> f64 {
    (estimate - exact as f64).abs() / exact as f64 * 100.0
}

fn sparse_mode_is_near_exact() {
    println!("== sparse mode (small cardinality)");
    for exact in [10u64, 100, 1_000] {
        let mut sketch = HyperLogLog::default();
        for i in 0..exact {
            sketch.add_str(&key("visitor", i));
        }
        println!(
            "  n={:<5} estimate={:<10.1} error={:>5.2}%  sparse={}  bytes={}",
            exact,
            sketch.estimate(),
            percent_error(sketch.estimate(), exact),
            sketch.is_sparse(),
            sketch.memory_bytes()
        );
    }
    println!();
}

fn transition_to_dense() {
    println!("== switch from sparse to dense");
    let mut sketch = HyperLogLog::default();
    let mut switched_at = 0u64;
    let mut sparse_bytes = 0usize;
    for i in 0..20_000u64 {
        sketch.add_str(&key("event", i));
        if !sketch.is_sparse() && switched_at == 0 {
            switched_at = i + 1;
            break;
        }
        sparse_bytes = sketch.memory_bytes();
    }
    println!(
        "  precision 14 holds {} registers; sparse pairs cost 5 bytes each",
        sketch.registers()
    );
    println!(
        "  switched after {} distinct items ({} bytes sparse -> {} bytes dense)",
        switched_at,
        sparse_bytes,
        sketch.memory_bytes()
    );
    println!();
}

fn accuracy_across_scales() {
    println!("== dense accuracy at precision 14 (standard error 0.81%)");
    for exact in [10_000u64, 100_000, 1_000_000] {
        let trials = 6;
        let mut worst = 0.0f64;
        let mut total = 0.0f64;
        let mut last = 0.0f64;
        for trial in 0..trials {
            let mut sketch = HyperLogLog::default();
            for i in 0..exact {
                sketch.add_str(&key(&format!("session{}", trial), i));
            }
            let error = percent_error(sketch.estimate(), exact);
            worst = worst.max(error);
            total += error;
            last = sketch.estimate();
        }
        println!(
            "  n={:<9} last estimate={:<12.0} mean error={:>5.2}%  worst of {} trials={:>5.2}%",
            exact,
            last,
            total / trials as f64,
            trials,
            worst
        );
    }
    println!();
}

fn memory_against_exact_counting() {
    println!("== memory: 1,000,000 distinct keys");
    let mut exact: HashSet<String> = HashSet::new();
    let mut sketch = HyperLogLog::default();
    for i in 0..1_000_000u64 {
        let item = key("user", i);
        sketch.add_str(&item);
        exact.insert(item);
    }
    let hash_set_bytes: usize = exact
        .iter()
        .map(|item| item.len() + std::mem::size_of::<String>())
        .sum::<usize>()
        + exact.capacity() * 8;
    println!("  exact HashSet    {} keys, roughly {} bytes", exact.len(), hash_set_bytes);
    println!(
        "  hyperloglog      estimate {:.0}, {} bytes ({:.0}x smaller, {:.2}% off)",
        sketch.estimate(),
        sketch.memory_bytes(),
        hash_set_bytes as f64 / sketch.memory_bytes() as f64,
        percent_error(sketch.estimate(), exact.len() as u64)
    );
    println!();
}

fn duplicates_are_free() {
    println!("== 2,000,000 additions drawn from 50,000 distinct keys");
    let mut sketch = HyperLogLog::default();
    let mut value = 12345u64;
    for _ in 0..2_000_000u64 {
        value = value.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        sketch.add_str(&key("sku", value % 50_000));
    }
    println!(
        "  estimate={:.0}  error={:.2}%",
        sketch.estimate(),
        percent_error(sketch.estimate(), 50_000)
    );
    println!();
}

fn unions_and_intersections() {
    println!("== mergeable sketches");
    let mut monday = HyperLogLog::default();
    let mut tuesday = HyperLogLog::default();
    for i in 0..300_000u64 {
        monday.add_str(&key("visitor", i));
    }
    for i in 200_000..500_000u64 {
        tuesday.add_str(&key("visitor", i));
    }
    let both = monday.union(&tuesday).expect("same precision");
    println!("  monday   estimate={:.0} (exact 300000)", monday.estimate());
    println!("  tuesday  estimate={:.0} (exact 300000)", tuesday.estimate());
    println!(
        "  union    estimate={:.0} (exact 500000, error {:.2}%)",
        both.estimate(),
        percent_error(both.estimate(), 500_000)
    );
    let overlap = monday.estimate_intersection(&tuesday).expect("same precision");
    println!(
        "  overlap  estimate={:.0} (exact 100000, error {:.2}%)",
        overlap,
        percent_error(overlap, 100_000)
    );

    let mut small = HyperLogLog::new(10).expect("valid precision");
    small.add_str("a");
    match monday.union(&small) {
        Ok(_) => println!("  merging different precisions unexpectedly succeeded"),
        Err(err) => println!("  merging different precisions rejected: {}", err),
    }
    println!();
}

fn serialization_roundtrip() {
    println!("== serialization");
    let mut sketch = HyperLogLog::default();
    for i in 0..250_000u64 {
        sketch.add_str(&key("order", i));
    }
    let encoded = sketch.to_bytes();
    let decoded = HyperLogLog::from_bytes(&encoded).expect("valid sketch");
    println!(
        "  encoded {} bytes, decoded estimate {:.0}, identical={}",
        encoded.len(),
        decoded.estimate(),
        decoded == sketch
    );

    let mut tampered = encoded.clone();
    tampered[64] ^= 0x01;
    match HyperLogLog::from_bytes(&tampered) {
        Ok(_) => println!("  tampered payload unexpectedly accepted"),
        Err(HllError::ChecksumMismatch { .. }) => println!("  tampered payload rejected by checksum"),
        Err(err) => println!("  tampered payload rejected: {}", err),
    }

    let body_len = encoded.len() - 8;
    let mut shortened = encoded[..body_len - 64].to_vec();
    let checksum = fnv1a(&shortened);
    shortened.extend_from_slice(&checksum.to_le_bytes());
    match HyperLogLog::from_bytes(&shortened) {
        Ok(_) => println!("  truncated payload unexpectedly accepted"),
        Err(err) => println!("  truncated payload rejected: {}", err),
    }
    println!();
}

fn precision_tradeoff() {
    println!("== precision tradeoff over 200,000 distinct keys");
    for precision in [8u8, 10, 12, 14, 16] {
        let mut sketch = HyperLogLog::new(precision).expect("valid precision");
        for i in 0..200_000u64 {
            sketch.add_str(&key("device", i));
        }
        println!(
            "  p={:<3} registers={:<7} bytes={:<7} estimate={:<10.0} error={:>5.2}%  predicted={:.2}%",
            precision,
            sketch.registers(),
            sketch.memory_bytes(),
            sketch.estimate(),
            percent_error(sketch.estimate(), 200_000),
            sketch.standard_error() * 100.0
        );
    }
}

fn main() {
    sparse_mode_is_near_exact();
    transition_to_dense();
    accuracy_across_scales();
    memory_against_exact_counting();
    duplicates_are_free();
    unions_and_intersections();
    serialization_roundtrip();
    precision_tradeoff();
}
