use std::time::Instant;

use arena_allocator::Arena;

struct ListNode<'a> {
    value: i64,
    next: Option<&'a ListNode<'a>>,
}

struct TreeNode<'a> {
    value: i64,
    left: Option<&'a TreeNode<'a>>,
    right: Option<&'a TreeNode<'a>>,
}

fn push_list<'a>(arena: &'a Arena, value: i64, next: Option<&'a ListNode<'a>>) -> &'a ListNode<'a> {
    &*arena.alloc(ListNode { value, next })
}

fn sum_list(node: &ListNode) -> i64 {
    let mut total = 0;
    let mut current = Some(node);
    while let Some(n) = current {
        total += n.value;
        current = n.next;
    }
    total
}

fn build_balanced_tree<'a>(arena: &'a Arena, values: &[i64]) -> Option<&'a TreeNode<'a>> {
    if values.is_empty() {
        return None;
    }
    let mid = values.len() / 2;
    let left = build_balanced_tree(arena, &values[..mid]);
    let right = build_balanced_tree(arena, &values[mid + 1..]);
    Some(&*arena.alloc(TreeNode {
        value: values[mid],
        left,
        right,
    }))
}

fn count_tree(node: Option<&TreeNode>) -> usize {
    match node {
        None => 0,
        Some(n) => 1 + count_tree(n.left) + count_tree(n.right),
    }
}

fn sum_tree(node: Option<&TreeNode>) -> i64 {
    match node {
        None => 0,
        Some(n) => n.value + sum_tree(n.left) + sum_tree(n.right),
    }
}

fn main() {
    let list_len: i64 = 5_000;
    let expected_list_sum: i64 = (0..list_len).sum();

    let mut arena = Arena::new();
    let mut head: Option<&ListNode> = None;
    for i in 0..list_len {
        head = Some(push_list(&arena, i, head));
    }
    let sum = sum_list(head.unwrap());
    assert_eq!(sum, expected_list_sum);
    println!(
        "linked list: built {} nodes in the arena, sum = {} (expected {})",
        list_len, sum, expected_list_sum
    );
    println!(
        "  chunks used: {}, bytes allocated: {}, bytes reserved: {}",
        arena.chunk_count(),
        arena.bytes_allocated(),
        arena.bytes_reserved()
    );

    arena.reset();
    println!(
        "after reset: chunks retained = {}, bytes allocated = {}",
        arena.chunk_count(),
        arena.bytes_allocated()
    );

    let values: Vec<i64> = (0..2_000).collect();
    let tree = build_balanced_tree(&arena, &values);
    let node_count = count_tree(tree);
    let tree_sum = sum_tree(tree);
    assert_eq!(node_count, values.len());
    println!(
        "binary tree: built {} nodes after reset (memory reused), sum = {}",
        node_count, tree_sum
    );
    println!(
        "  chunks used: {}, bytes allocated: {}",
        arena.chunk_count(),
        arena.bytes_allocated()
    );

    let n: usize = 200_000;

    let start = Instant::now();
    let bench_arena = Arena::new();
    let mut arena_checksum: i64 = 0;
    for i in 0..n {
        let node = bench_arena.alloc(ListNode {
            value: i as i64,
            next: None,
        });
        arena_checksum += node.value;
    }
    let arena_elapsed = start.elapsed();

    let start = Instant::now();
    let mut boxed: Vec<Box<ListNode>> = Vec::with_capacity(n);
    let mut box_checksum: i64 = 0;
    for i in 0..n {
        let node = Box::new(ListNode {
            value: i as i64,
            next: None,
        });
        box_checksum += node.value;
        boxed.push(node);
    }
    let box_elapsed = start.elapsed();

    assert_eq!(arena_checksum, box_checksum);
    println!("throughput comparison: {} nodes allocated", n);
    println!("  arena: {:?}", arena_elapsed);
    println!("  Box:   {:?}", box_elapsed);
}
