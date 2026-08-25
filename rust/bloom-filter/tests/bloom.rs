use bloom_filter::BloomFilter;

#[test]
fn no_false_negatives_for_all_inserted_items() {
    let items: Vec<String> = (0..3000).map(|i| format!("item-{}", i)).collect();
    let mut filter = BloomFilter::new(items.len(), 0.01);

    for item in &items {
        filter.insert(item);
    }

    for item in &items {
        assert!(filter.contains(item), "false negative for {:?}", item);
    }
}

#[test]
fn no_false_negatives_with_integer_keys() {
    let items: Vec<i64> = (0..10_000).collect();
    let mut filter = BloomFilter::new(items.len(), 0.02);

    for item in &items {
        filter.insert(item);
    }

    for item in &items {
        assert!(filter.contains(item));
    }
}

#[test]
fn empty_filter_reports_nothing_present() {
    let filter = BloomFilter::new(100, 0.01);
    assert!(!filter.contains(&"anything"));
    assert_eq!(filter.fill_ratio(), 0.0);
    assert_eq!(filter.estimated_false_positive_rate(), 0.0);
}

#[test]
fn false_positive_rate_stays_within_reasonable_bound_of_theoretical_estimate() {
    let n = 5000usize;
    let target_fp = 0.01;
    let items: Vec<String> = (0..n).map(|i| format!("member-{}", i)).collect();
    let mut filter = BloomFilter::new(n, target_fp);

    for item in &items {
        filter.insert(item);
    }

    let sample_size = 50_000;
    let mut false_positives = 0;
    for i in 0..sample_size {
        let candidate = format!("non-member-{}", i);
        if filter.contains(&candidate) {
            false_positives += 1;
        }
    }

    let observed_rate = false_positives as f64 / sample_size as f64;
    assert!(
        observed_rate < target_fp * 3.0,
        "observed false positive rate {} too far above target {}",
        observed_rate,
        target_fp
    );

    let estimated_rate = filter.estimated_false_positive_rate();
    assert!(
        (observed_rate - estimated_rate).abs() < 0.02,
        "observed rate {} too far from estimated rate {}",
        observed_rate,
        estimated_rate
    );
}

#[test]
fn bitset_sizing_uses_at_least_the_requested_capacity() {
    let filter = BloomFilter::new(1000, 0.01);
    assert!(filter.num_bits() >= 64);
    assert!(filter.num_hashes() >= 1);
}

#[test]
fn with_params_respects_explicit_bit_and_hash_counts() {
    let filter = BloomFilter::with_params(1024, 4);
    assert_eq!(filter.num_bits(), 1024);
    assert_eq!(filter.num_hashes(), 4);
}

#[test]
fn with_params_rounds_bit_count_up_to_whole_words() {
    let filter = BloomFilter::with_params(65, 3);
    assert_eq!(filter.num_bits(), 65);
    assert!(!filter.contains(&"x"));
}

#[test]
fn fill_ratio_increases_monotonically_as_items_are_inserted() {
    let mut filter = BloomFilter::with_params(4096, 5);
    let mut previous = filter.fill_ratio();
    assert_eq!(previous, 0.0);

    for i in 0..200 {
        filter.insert(&format!("key-{}", i));
        let current = filter.fill_ratio();
        assert!(current >= previous);
        previous = current;
    }

    assert!(filter.fill_ratio() > 0.0);
}

#[test]
fn len_tracks_number_of_inserted_items() {
    let mut filter = BloomFilter::new(100, 0.01);
    assert_eq!(filter.len(), 0);
    assert!(filter.is_empty());

    for i in 0..17 {
        filter.insert(&i);
    }

    assert_eq!(filter.len(), 17);
    assert!(!filter.is_empty());
}

#[test]
fn duplicate_inserts_do_not_break_membership() {
    let mut filter = BloomFilter::new(50, 0.01);
    for _ in 0..10 {
        filter.insert(&"repeated");
    }
    assert!(filter.contains(&"repeated"));
    assert_eq!(filter.len(), 10);
}
