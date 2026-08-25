use bloom_filter::BloomFilter;

fn main() {
    let inserted: Vec<String> = (0..2000).map(|i| format!("user-{}", i)).collect();
    let mut filter = BloomFilter::new(inserted.len(), 0.01);

    for item in &inserted {
        filter.insert(item);
    }

    let mut false_negatives = 0;
    for item in &inserted {
        if !filter.contains(item) {
            false_negatives += 1;
        }
    }
    println!(
        "checked {} inserted items, false negatives: {}",
        inserted.len(),
        false_negatives
    );

    let absent: Vec<String> = (0..5000).map(|i| format!("absent-{}", i)).collect();
    let mut false_positives = 0;
    let mut first_false_positive: Option<&str> = None;
    for item in &absent {
        if filter.contains(item) {
            false_positives += 1;
            if first_false_positive.is_none() {
                first_false_positive = Some(item.as_str());
            }
        }
    }

    println!(
        "checked {} absent items, false positives: {} ({:.3}% observed rate)",
        absent.len(),
        false_positives,
        (false_positives as f64 / absent.len() as f64) * 100.0
    );

    match first_false_positive {
        Some(example) => println!("example false positive: {:?}", example),
        None => println!("no false positives observed in this sample"),
    }

    println!("bitset size: {} bits", filter.num_bits());
    println!("hash functions: {}", filter.num_hashes());
    println!("fill ratio: {:.4}", filter.fill_ratio());
    println!(
        "estimated false positive rate: {:.6}",
        filter.estimated_false_positive_rate()
    );
}
