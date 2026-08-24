pub trait Monoid: Copy {
    fn identity() -> Self;
    fn combine(a: Self, b: Self) -> Self;
    fn from_leaf(v: i64) -> Self;
    fn apply_add(self, delta: i64, len: usize) -> Self;
    fn value(self) -> i64;
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub struct SumVal(pub i64);

impl Monoid for SumVal {
    fn identity() -> Self {
        SumVal(0)
    }
    fn combine(a: Self, b: Self) -> Self {
        SumVal(a.0 + b.0)
    }
    fn from_leaf(v: i64) -> Self {
        SumVal(v)
    }
    fn apply_add(self, delta: i64, len: usize) -> Self {
        SumVal(self.0 + delta * len as i64)
    }
    fn value(self) -> i64 {
        self.0
    }
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub struct MinVal(pub i64);

impl Monoid for MinVal {
    fn identity() -> Self {
        MinVal(i64::MAX)
    }
    fn combine(a: Self, b: Self) -> Self {
        MinVal(a.0.min(b.0))
    }
    fn from_leaf(v: i64) -> Self {
        MinVal(v)
    }
    fn apply_add(self, delta: i64, _len: usize) -> Self {
        if self.0 == i64::MAX {
            self
        } else {
            MinVal(self.0 + delta)
        }
    }
    fn value(self) -> i64 {
        self.0
    }
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub struct MaxVal(pub i64);

impl Monoid for MaxVal {
    fn identity() -> Self {
        MaxVal(i64::MIN)
    }
    fn combine(a: Self, b: Self) -> Self {
        MaxVal(a.0.max(b.0))
    }
    fn from_leaf(v: i64) -> Self {
        MaxVal(v)
    }
    fn apply_add(self, delta: i64, _len: usize) -> Self {
        if self.0 == i64::MIN {
            self
        } else {
            MaxVal(self.0 + delta)
        }
    }
    fn value(self) -> i64 {
        self.0
    }
}

pub struct SegmentTree<T: Monoid> {
    n: usize,
    tree: Vec<T>,
    lazy: Vec<i64>,
}

impl<T: Monoid> SegmentTree<T> {
    pub fn build(values: &[i64]) -> Self {
        let n = values.len();
        let size = if n == 0 { 1 } else { 4 * n };
        let mut st = SegmentTree {
            n,
            tree: vec![T::identity(); size],
            lazy: vec![0i64; size],
        };
        if n > 0 {
            st.build_rec(1, 0, n - 1, values);
        }
        st
    }

    fn build_rec(&mut self, node: usize, l: usize, r: usize, values: &[i64]) {
        if l == r {
            self.tree[node] = T::from_leaf(values[l]);
            return;
        }
        let mid = l + (r - l) / 2;
        self.build_rec(2 * node, l, mid, values);
        self.build_rec(2 * node + 1, mid + 1, r, values);
        self.tree[node] = T::combine(self.tree[2 * node], self.tree[2 * node + 1]);
    }

    fn apply(&mut self, node: usize, l: usize, r: usize, delta: i64) {
        let len = r - l + 1;
        self.tree[node] = self.tree[node].apply_add(delta, len);
        self.lazy[node] += delta;
    }

    fn push_down(&mut self, node: usize, l: usize, r: usize) {
        if self.lazy[node] != 0 {
            let mid = l + (r - l) / 2;
            let delta = self.lazy[node];
            self.apply(2 * node, l, mid, delta);
            self.apply(2 * node + 1, mid + 1, r, delta);
            self.lazy[node] = 0;
        }
    }

    pub fn update_range(&mut self, ql: usize, qr: usize, delta: i64) {
        if self.n == 0 {
            return;
        }
        self.update_rec(1, 0, self.n - 1, ql, qr, delta);
    }

    fn update_rec(&mut self, node: usize, l: usize, r: usize, ql: usize, qr: usize, delta: i64) {
        if qr < l || r < ql {
            return;
        }
        if ql <= l && r <= qr {
            self.apply(node, l, r, delta);
            return;
        }
        self.push_down(node, l, r);
        let mid = l + (r - l) / 2;
        self.update_rec(2 * node, l, mid, ql, qr, delta);
        self.update_rec(2 * node + 1, mid + 1, r, ql, qr, delta);
        self.tree[node] = T::combine(self.tree[2 * node], self.tree[2 * node + 1]);
    }

    pub fn point_update(&mut self, idx: usize, delta: i64) {
        self.update_range(idx, idx, delta);
    }

    pub fn query_range(&mut self, ql: usize, qr: usize) -> i64 {
        if self.n == 0 {
            return T::identity().value();
        }
        self.query_rec(1, 0, self.n - 1, ql, qr).value()
    }

    fn query_rec(&mut self, node: usize, l: usize, r: usize, ql: usize, qr: usize) -> T {
        if qr < l || r < ql {
            return T::identity();
        }
        if ql <= l && r <= qr {
            return self.tree[node];
        }
        self.push_down(node, l, r);
        let mid = l + (r - l) / 2;
        let left = self.query_rec(2 * node, l, mid, ql, qr);
        let right = self.query_rec(2 * node + 1, mid + 1, r, ql, qr);
        T::combine(left, right)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sum_range_query() {
        let mut st = SegmentTree::<SumVal>::build(&[1, 2, 3, 4, 5]);
        assert_eq!(st.query_range(0, 4), 15);
        assert_eq!(st.query_range(1, 3), 9);
    }

    #[test]
    fn min_max_range_query() {
        let mut min_tree = SegmentTree::<MinVal>::build(&[5, 3, 8, 1, 9]);
        let mut max_tree = SegmentTree::<MaxVal>::build(&[5, 3, 8, 1, 9]);
        assert_eq!(min_tree.query_range(0, 4), 1);
        assert_eq!(max_tree.query_range(0, 4), 9);
        assert_eq!(min_tree.query_range(0, 1), 3);
        assert_eq!(max_tree.query_range(0, 1), 5);
    }

    #[test]
    fn range_add_with_lazy_propagation() {
        let mut st = SegmentTree::<SumVal>::build(&[1, 2, 3, 4, 5]);
        st.update_range(1, 3, 10);
        assert_eq!(st.query_range(0, 4), 15 + 30);
        assert_eq!(st.query_range(1, 3), 9 + 30);
        assert_eq!(st.query_range(0, 0), 1);

        let mut min_tree = SegmentTree::<MinVal>::build(&[5, 3, 8, 1, 9]);
        min_tree.update_range(0, 4, -2);
        assert_eq!(min_tree.query_range(0, 4), -1);
    }

    #[test]
    fn point_update() {
        let mut st = SegmentTree::<SumVal>::build(&[1, 1, 1, 1]);
        st.point_update(2, 5);
        assert_eq!(st.query_range(0, 3), 9);
        assert_eq!(st.query_range(2, 2), 6);
    }
}
