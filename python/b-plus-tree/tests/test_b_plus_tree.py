import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from b_plus_tree import BPlusTree, InternalNode, LeafNode


class TestBasicOperations(unittest.TestCase):
    def test_empty_tree(self):
        tree = BPlusTree()
        self.assertEqual(len(tree), 0)
        self.assertNotIn(1, tree)
        self.assertIsNone(tree.search(1))
        self.assertEqual(list(tree.items()), [])
        self.assertTrue(tree.is_valid())

    def test_invalid_order_raises(self):
        with self.assertRaises(ValueError):
            BPlusTree(order=2)

    def test_insert_and_search(self):
        tree = BPlusTree(order=4)
        tree.insert(5, "five")
        tree.insert(3, "three")
        tree.insert(8, "eight")
        self.assertEqual(len(tree), 3)
        self.assertEqual(tree.search(5), "five")
        self.assertEqual(tree.search(3), "three")
        self.assertIn(8, tree)
        self.assertIsNone(tree.search(100))

    def test_insert_duplicate_updates_value(self):
        tree = BPlusTree(order=4)
        tree.insert(1, "first")
        tree.insert(1, "second")
        self.assertEqual(len(tree), 1)
        self.assertEqual(tree.search(1), "second")

    def test_delete_missing_key_raises(self):
        tree = BPlusTree(order=4)
        tree.insert(1, "a")
        with self.assertRaises(KeyError):
            tree.delete(999)

    def test_delete_reduces_size_and_removes_key(self):
        tree = BPlusTree(order=4)
        for key in range(10):
            tree.insert(key, key * 10)
        tree.delete(5)
        self.assertEqual(len(tree), 9)
        self.assertIsNone(tree.search(5))
        self.assertNotIn(5, tree)


class TestOrderedIterationAndRange(unittest.TestCase):
    def test_items_are_sorted(self):
        tree = BPlusTree(order=4)
        keys = [7, 2, 9, 4, 1, 8, 3, 6, 5, 0]
        for key in keys:
            tree.insert(key, key)
        self.assertEqual(list(tree), sorted(keys))
        self.assertEqual([k for k, _ in tree.items()], sorted(keys))

    def test_range_inclusive_bounds(self):
        tree = BPlusTree(order=4)
        for key in range(20):
            tree.insert(key, key * 2)
        result = list(tree.range(5, 10))
        self.assertEqual(result, [(k, k * 2) for k in range(5, 11)])

    def test_range_partial_overlap_and_empty(self):
        tree = BPlusTree(order=4)
        for key in range(20):
            tree.insert(key, key)
        self.assertEqual(list(tree.range(-5, 3)), [(0, 0), (1, 1), (2, 2), (3, 3)])
        self.assertEqual(list(tree.range(17, 100)), [(17, 17), (18, 18), (19, 19)])
        self.assertEqual(list(tree.range(50, 60)), [])
        self.assertEqual(list(tree.range(10, 2)), [])

    def test_range_walks_multiple_leaves(self):
        tree = BPlusTree(order=4)
        for key in range(500):
            tree.insert(key, key)
        result = list(tree.range(100, 300))
        self.assertEqual(result, [(k, k) for k in range(100, 301)])


class TestSplitEdgeCases(unittest.TestCase):
    def test_root_leaf_split_creates_internal_root(self):
        tree = BPlusTree(order=4)
        self.assertIsInstance(tree.root, LeafNode)
        for key in [1, 2, 3]:
            tree.insert(key, key)
        self.assertIsInstance(tree.root, LeafNode)
        tree.insert(4, 4)
        self.assertIsInstance(tree.root, InternalNode)
        self.assertTrue(tree.is_valid())

    def test_root_internal_split_grows_height(self):
        tree = BPlusTree(order=4)
        for key in range(50):
            tree.insert(key, key)
        self.assertTrue(tree.is_valid())
        self.assertTrue(tree.is_balanced())
        for key in range(50):
            self.assertEqual(tree.search(key), key)

    def test_leaf_linked_list_intact_after_many_splits(self):
        tree = BPlusTree(order=4)
        keys = list(range(200))
        random.Random(1).shuffle(keys)
        for key in keys:
            tree.insert(key, key)
        leaf = tree._leftmost_leaf()
        seen = []
        while leaf is not None:
            seen.extend(leaf.keys)
            leaf = leaf.next
        self.assertEqual(seen, sorted(keys))


class TestMergeEdgeCases(unittest.TestCase):
    def test_leaf_underflow_borrows_from_right_sibling(self):
        tree = BPlusTree(order=4)
        for key in [10, 20, 30, 40, 50, 60]:
            tree.insert(key, key)
        tree.delete(10)
        self.assertTrue(tree.is_valid())
        for key in [20, 30, 40, 50, 60]:
            self.assertEqual(tree.search(key), key)

    def test_leaf_underflow_merges_when_siblings_are_minimal(self):
        tree = BPlusTree(order=4)
        for key in [1, 2, 3, 4]:
            tree.insert(key, key)
        tree.delete(1)
        self.assertTrue(tree.is_valid())
        self.assertEqual(list(tree), [2, 3, 4])

    def test_deleting_down_to_empty_tree(self):
        tree = BPlusTree(order=4)
        keys = list(range(30))
        for key in keys:
            tree.insert(key, key)
        random.Random(2).shuffle(keys)
        for key in keys:
            tree.delete(key)
            self.assertTrue(tree.is_valid())
        self.assertEqual(len(tree), 0)
        self.assertIsInstance(tree.root, LeafNode)
        self.assertEqual(tree.root.keys, [])

    def test_internal_node_underflow_triggers_merge(self):
        tree = BPlusTree(order=4)
        for key in range(60):
            tree.insert(key, key)
        for key in range(0, 45):
            tree.delete(key)
            self.assertTrue(tree.is_valid())
        remaining = list(range(45, 60))
        self.assertEqual(list(tree), remaining)


class TestRandomizedAgainstSortedDict(unittest.TestCase):
    def test_randomized_insert_delete_range_matches_reference(self):
        for order in (3, 4, 5, 8, 16):
            with self.subTest(order=order):
                rng = random.Random(order * 1000 + 7)
                tree = BPlusTree(order=order)
                reference = {}
                universe = list(range(400))

                for _ in range(3000):
                    op = rng.random()
                    key = rng.choice(universe)
                    if op < 0.55:
                        value = rng.randint(0, 1_000_000)
                        tree.insert(key, value)
                        reference[key] = value
                    elif op < 0.85:
                        if key in reference:
                            tree.delete(key)
                            del reference[key]
                        else:
                            with self.assertRaises(KeyError):
                                tree.delete(key)
                    else:
                        start = rng.choice(universe)
                        end = start + rng.randint(0, 50)
                        expected = sorted((k, v) for k, v in reference.items() if start <= k <= end)
                        self.assertEqual(list(tree.range(start, end)), expected)

                self.assertEqual(len(tree), len(reference))
                self.assertEqual(dict(tree.items()), reference)
                self.assertEqual(list(tree), sorted(reference))
                self.assertTrue(tree.is_valid())


if __name__ == "__main__":
    unittest.main()
