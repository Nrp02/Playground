mod fenwick_tree;
mod segment_tree;

use fenwick_tree::FenwickTree;
use segment_tree::{MaxVal, MinVal, SegmentTree, SumVal};

struct Xorshift {
    state: u64,
}

impl Xorshift {
    fn new(seed: u64) -> Self {
        Xorshift { state: seed | 1 }
    }

    fn next_u64(&mut self) -> u64 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.state = x;
        x
    }

    fn range(&mut self, lo: i64, hi: i64) -> i64 {
        lo + (self.next_u64() % (hi - lo + 1) as u64) as i64
    }
}

fn brute_range_op(data: &[i64], l: usize, r: usize, op: &str) -> i64 {
    match op {
        "sum" => data[l..=r].iter().sum(),
        "min" => *data[l..=r].iter().min().unwrap(),
        "max" => *data[l..=r].iter().max().unwrap(),
        _ => unreachable!(),
    }
}

fn stress_test_segment_tree(rng: &mut Xorshift, iterations: usize) -> bool {
    let n = 60;
    let mut data: Vec<i64> = (0..n).map(|_| rng.range(-50, 50)).collect();

    let mut sum_tree = SegmentTree::<SumVal>::build(&data);
    let mut min_tree = SegmentTree::<MinVal>::build(&data);
    let mut max_tree = SegmentTree::<MaxVal>::build(&data);

    for _ in 0..iterations {
        let l = rng.range(0, n as i64 - 1) as usize;
        let r = rng.range(l as i64, n as i64 - 1) as usize;

        if rng.range(0, 1) == 0 {
            let delta = rng.range(-10, 10);
            for v in data.iter_mut().take(r + 1).skip(l) {
                *v += delta;
            }
            sum_tree.update_range(l, r, delta);
            min_tree.update_range(l, r, delta);
            max_tree.update_range(l, r, delta);
        } else {
            let expected_sum = brute_range_op(&data, l, r, "sum");
            let expected_min = brute_range_op(&data, l, r, "min");
            let expected_max = brute_range_op(&data, l, r, "max");

            let got_sum = sum_tree.query_range(l, r);
            let got_min = min_tree.query_range(l, r);
            let got_max = max_tree.query_range(l, r);

            if got_sum != expected_sum || got_min != expected_min || got_max != expected_max {
                println!(
                    "MISMATCH range [{l},{r}] sum {got_sum} vs {expected_sum}, min {got_min} vs {expected_min}, max {got_max} vs {expected_max}"
                );
                return false;
            }
        }
    }
    true
}

fn stress_test_fenwick(rng: &mut Xorshift, iterations: usize) -> bool {
    let n = 60;
    let mut data: Vec<i64> = (0..n).map(|_| rng.range(-50, 50)).collect();
    let mut ft = FenwickTree::build(&data);

    for _ in 0..iterations {
        if rng.range(0, 1) == 0 {
            let idx = rng.range(0, n as i64 - 1) as usize;
            let delta = rng.range(-20, 20);
            data[idx] += delta;
            ft.point_update(idx, delta);
        } else {
            let l = rng.range(0, n as i64 - 1) as usize;
            let r = rng.range(l as i64, n as i64 - 1) as usize;
            let expected: i64 = data[l..=r].iter().sum();
            let got = ft.range_sum(l, r);
            if got != expected {
                println!("MISMATCH range_sum [{l},{r}] got {got} vs expected {expected}");
                return false;
            }
        }
    }
    true
}

fn main() {
    let values = vec![5, 3, 8, 1, 9, 2, 7, 4, 6, 0];
    println!("input: {values:?}");

    let mut sum_tree = SegmentTree::<SumVal>::build(&values);
    let mut min_tree = SegmentTree::<MinVal>::build(&values);
    let mut max_tree = SegmentTree::<MaxVal>::build(&values);
    let mut ft = FenwickTree::build(&values);

    println!("sum[2..7] = {}", sum_tree.query_range(2, 7));
    println!("min[2..7] = {}", min_tree.query_range(2, 7));
    println!("max[2..7] = {}", max_tree.query_range(2, 7));
    println!("fenwick prefix_sum(4) = {}", ft.prefix_sum(4));
    println!("fenwick range_sum(3..6) = {}", ft.range_sum(3, 6));

    sum_tree.update_range(0, 4, 100);
    min_tree.update_range(0, 4, 100);
    max_tree.update_range(0, 4, 100);
    println!("after range-add(0..4, +100):");
    println!("sum[0..4] = {}", sum_tree.query_range(0, 4));
    println!("min[0..4] = {}", min_tree.query_range(0, 4));
    println!("max[0..4] = {}", max_tree.query_range(0, 4));

    ft.point_update(5, 50);
    println!("after fenwick point_update(5, +50): range_sum(0..9) = {}", ft.range_sum(0, 9));

    sum_tree.point_update(0, 1000);
    println!("after segtree point_update(0, +1000): sum[0..0] = {}", sum_tree.query_range(0, 0));

    println!();
    println!("running stress tests against brute-force O(n) reference...");
    let mut rng = Xorshift::new(0xC0FFEE);
    let seg_ok = stress_test_segment_tree(&mut rng, 5000);
    let fen_ok = stress_test_fenwick(&mut rng, 5000);

    if seg_ok {
        println!("segment tree stress test: PASS (5000 ops)");
    } else {
        println!("segment tree stress test: FAIL");
    }
    if fen_ok {
        println!("fenwick tree stress test: PASS (5000 ops)");
    } else {
        println!("fenwick tree stress test: FAIL");
    }

    if !seg_ok || !fen_ok {
        std::process::exit(1);
    }
}
