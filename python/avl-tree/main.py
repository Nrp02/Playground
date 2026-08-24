import random
import sys

from avl_tree import AVLTree


def print_tree(tree: AVLTree) -> None:
    print(f"size={len(tree)} height={tree.height} balanced={tree.is_balanced()} bst={tree.is_bst()}")
    print("inorder:", [k for k, _ in tree.inorder()])


def demo_sequential_insert(n: int) -> None:
    print(f"\n=== sequential insert of 1..{n} (worst case for a plain BST) ===")
    tree = AVLTree()
    for i in range(1, n + 1):
        tree.insert(i, i * i)
    print_tree(tree)
    print(f"height {tree.height} vs unbalanced-BST worst case {n}")


def demo_random_insert_delete(n: int, seed: int) -> None:
    print(f"\n=== random insert/delete of {n} keys (seed={seed}) ===")
    rng = random.Random(seed)
    keys = list(range(n))
    rng.shuffle(keys)

    tree = AVLTree()
    for k in keys:
        tree.insert(k, f"value-{k}")
    print_tree(tree)

    to_delete = rng.sample(keys, k=n // 2)
    for k in to_delete:
        assert tree.delete(k)
    print_tree(tree)

    for k in to_delete:
        assert k not in tree
    remaining = set(keys) - set(to_delete)
    for k in remaining:
        assert tree[k] == f"value-{k}"
    print("all deletions and remaining lookups verified")


def main() -> int:
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    demo_sequential_insert(n)
    demo_random_insert_delete(n, seed=42)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
