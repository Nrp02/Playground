pub struct FenwickTree {
    n: usize,
    tree: Vec<i64>,
}

impl FenwickTree {
    pub fn build(values: &[i64]) -> Self {
        let n = values.len();
        let mut tree = vec![0i64; n + 1];
        for i in 0..n {
            tree[i + 1] += values[i];
            let parent = i + 1 + lowbit(i + 1);
            if parent <= n {
                tree[parent] += tree[i + 1];
            }
        }
        FenwickTree { n, tree }
    }

    pub fn point_update(&mut self, idx: usize, delta: i64) {
        let mut i = idx + 1;
        while i <= self.n {
            self.tree[i] += delta;
            i += lowbit(i);
        }
    }

    pub fn prefix_sum(&self, idx: usize) -> i64 {
        let mut i = idx + 1;
        let mut sum = 0i64;
        while i > 0 {
            sum += self.tree[i];
            i -= lowbit(i);
        }
        sum
    }

    pub fn range_sum(&self, l: usize, r: usize) -> i64 {
        if l == 0 {
            self.prefix_sum(r)
        } else {
            self.prefix_sum(r) - self.prefix_sum(l - 1)
        }
    }
}

fn lowbit(x: usize) -> usize {
    x & x.wrapping_neg()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn prefix_sum_matches_brute_force() {
        let values = vec![3, 1, 4, 1, 5, 9, 2, 6];
        let ft = FenwickTree::build(&values);
        let mut running = 0i64;
        for (i, v) in values.iter().enumerate() {
            running += v;
            assert_eq!(ft.prefix_sum(i), running);
        }
    }

    #[test]
    fn range_sum_query() {
        let values = vec![3, 1, 4, 1, 5, 9, 2, 6];
        let ft = FenwickTree::build(&values);
        assert_eq!(ft.range_sum(2, 5), 4 + 1 + 5 + 9);
        assert_eq!(ft.range_sum(0, 7), values.iter().sum::<i64>());
    }

    #[test]
    fn point_update_reflected_in_queries() {
        let values = vec![1, 1, 1, 1, 1];
        let mut ft = FenwickTree::build(&values);
        ft.point_update(2, 10);
        assert_eq!(ft.range_sum(0, 4), 15);
        assert_eq!(ft.range_sum(2, 2), 11);
    }
}
