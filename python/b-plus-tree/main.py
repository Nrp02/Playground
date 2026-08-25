import random
import sys

from b_plus_tree import BPlusTree


def describe(tree: BPlusTree) -> None:
    print(f"size={len(tree)} valid={tree.is_valid()} balanced={tree.is_balanced()}")


def demo_bulk_build_and_lookup(n: int, seed: int) -> BPlusTree:
    print(f"\n=== building tree from {n} keys (seed={seed}) ===")
    rng = random.Random(seed)
    keys = list(range(n))
    rng.shuffle(keys)

    tree = BPlusTree(order=32)
    for key in keys:
        tree.insert(key, f"value-{key}")
    describe(tree)

    sample = rng.sample(keys, k=20)
    for key in sample:
        assert tree.search(key) == f"value-{key}"
    assert tree.search(-1) is None
    assert tree.search(n) is None
    print(f"verified {len(sample)} random point lookups plus two misses")
    return tree


def demo_range_scans(tree: BPlusTree, n: int) -> None:
    print("\n=== range scans ===")
    for start, end in [(0, 9), (n // 2, n // 2 + 24), (n - 5, n + 100)]:
        result = list(tree.range(start, end))
        expected = [k for k in range(max(start, 0), min(end, n - 1) + 1)]
        assert [k for k, _ in result] == expected
        print(f"range({start}, {end}) -> {len(result)} entries, first={result[0] if result else None}, last={result[-1] if result else None}")


def demo_bulk_delete(tree: BPlusTree, n: int, seed: int) -> None:
    print(f"\n=== deleting a third of the keys ===")
    rng = random.Random(seed)
    all_keys = list(range(n))
    to_delete = set(rng.sample(all_keys, k=n // 3))
    for key in to_delete:
        tree.delete(key)
    describe(tree)

    remaining = [k for k in all_keys if k not in to_delete]
    for key in to_delete:
        assert tree.search(key) is None
    for key in remaining:
        assert tree.search(key) == f"value-{key}"
    print(f"verified {len(to_delete)} deletions and {len(remaining)} survivors")

    scanned = [k for k, _ in tree.range(0, n - 1)]
    assert scanned == sorted(remaining)
    print("full range scan after deletion still matches sorted survivors")


def main() -> int:
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    tree = demo_bulk_build_and_lookup(n, seed=17)
    demo_range_scans(tree, n)
    demo_bulk_delete(tree, n, seed=17)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
