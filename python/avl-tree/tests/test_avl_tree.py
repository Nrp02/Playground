import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from avl_tree import AVLTree


class TestBasicOperations(unittest.TestCase):
    def test_empty_tree(self):
        tree = AVLTree()
        self.assertEqual(len(tree), 0)
        self.assertEqual(tree.height, 0)
        self.assertIsNone(tree.root)
        self.assertTrue(tree.is_balanced())
        self.assertNotIn(1, tree)

    def test_insert_and_get(self):
        tree = AVLTree()
        tree.insert(5, "five")
        tree.insert(3, "three")
        tree.insert(8, "eight")
        self.assertEqual(len(tree), 3)
        self.assertEqual(tree.get(5), "five")
        self.assertEqual(tree[3], "three")
        self.assertEqual(tree.get(100, "missing"), "missing")

    def test_getitem_raises_keyerror(self):
        tree = AVLTree()
        tree.insert(1, "a")
        with self.assertRaises(KeyError):
            _ = tree[999]

    def test_insert_duplicate_updates_value(self):
        tree = AVLTree()
        tree.insert(1, "first")
        tree.insert(1, "second")
        self.assertEqual(len(tree), 1)
        self.assertEqual(tree[1], "second")

    def test_contains(self):
        tree = AVLTree()
        for k in [10, 20, 30]:
            tree.insert(k)
        self.assertIn(20, tree)
        self.assertNotIn(25, tree)

    def test_min_max(self):
        tree = AVLTree()
        for k in [50, 20, 80, 10, 30, 70, 90]:
            tree.insert(k)
        self.assertEqual(tree.min_key(), 10)
        self.assertEqual(tree.max_key(), 90)

    def test_min_max_empty_raises(self):
        tree = AVLTree()
        with self.assertRaises(KeyError):
            tree.min_key()
        with self.assertRaises(KeyError):
            tree.max_key()

    def test_iteration_yields_sorted_keys(self):
        tree = AVLTree()
        for k in [5, 1, 9, 3, 7]:
            tree.insert(k)
        self.assertEqual(list(tree), [1, 3, 5, 7, 9])


class TestRotationsAndBalance(unittest.TestCase):
    def test_left_left_rotation(self):
        tree = AVLTree()
        for k in [30, 20, 10]:
            tree.insert(k)
        self.assertEqual(tree.root.key, 20)
        self.assertTrue(tree.is_balanced())
        self.assertEqual(tree.height, 2)

    def test_right_right_rotation(self):
        tree = AVLTree()
        for k in [10, 20, 30]:
            tree.insert(k)
        self.assertEqual(tree.root.key, 20)
        self.assertTrue(tree.is_balanced())
        self.assertEqual(tree.height, 2)

    def test_left_right_rotation(self):
        tree = AVLTree()
        for k in [30, 10, 20]:
            tree.insert(k)
        self.assertEqual(tree.root.key, 20)
        self.assertTrue(tree.is_balanced())

    def test_right_left_rotation(self):
        tree = AVLTree()
        for k in [10, 30, 20]:
            tree.insert(k)
        self.assertEqual(tree.root.key, 20)
        self.assertTrue(tree.is_balanced())

    def test_sequential_insert_stays_logarithmic(self):
        tree = AVLTree()
        n = 10_000
        for i in range(n):
            tree.insert(i)
        self.assertTrue(tree.is_balanced())
        self.assertTrue(tree.is_bst())
        import math

        self.assertLessEqual(tree.height, 1.45 * math.log2(n + 2))

    def test_random_insert_stays_balanced(self):
        rng = random.Random(1)
        keys = list(range(2000))
        rng.shuffle(keys)
        tree = AVLTree()
        for k in keys:
            tree.insert(k)
            self.assertTrue(tree.is_balanced())
        self.assertEqual(len(tree), 2000)
        self.assertTrue(tree.is_bst())


class TestDeletion(unittest.TestCase):
    def test_delete_leaf(self):
        tree = AVLTree()
        for k in [10, 5, 15]:
            tree.insert(k)
        self.assertTrue(tree.delete(5))
        self.assertNotIn(5, tree)
        self.assertEqual(len(tree), 2)
        self.assertTrue(tree.is_balanced())

    def test_delete_node_with_one_child(self):
        tree = AVLTree()
        for k in [10, 5, 15, 3]:
            tree.insert(k)
        self.assertTrue(tree.delete(5))
        self.assertNotIn(5, tree)
        self.assertIn(3, tree)
        self.assertTrue(tree.is_balanced())
        self.assertTrue(tree.is_bst())

    def test_delete_node_with_two_children(self):
        tree = AVLTree()
        for k in [10, 5, 15, 3, 7, 12, 20]:
            tree.insert(k)
        self.assertTrue(tree.delete(10))
        self.assertNotIn(10, tree)
        self.assertEqual(len(tree), 6)
        self.assertTrue(tree.is_balanced())
        self.assertTrue(tree.is_bst())

    def test_delete_missing_key_returns_false(self):
        tree = AVLTree()
        tree.insert(1)
        self.assertFalse(tree.delete(999))
        self.assertEqual(len(tree), 1)

    def test_delete_all_leaves_empty_tree(self):
        tree = AVLTree()
        keys = [10, 5, 15, 3, 7, 12, 20, 1, 4, 6, 8]
        for k in keys:
            tree.insert(k)
        for k in keys:
            self.assertTrue(tree.delete(k))
        self.assertEqual(len(tree), 0)
        self.assertIsNone(tree.root)
        self.assertEqual(tree.height, 0)

    def test_random_insert_delete_stays_balanced_and_correct(self):
        rng = random.Random(7)
        keys = list(range(500))
        rng.shuffle(keys)

        tree = AVLTree()
        for k in keys:
            tree.insert(k, k * 2)

        to_delete = rng.sample(keys, k=250)
        for k in to_delete:
            self.assertTrue(tree.delete(k))
            self.assertTrue(tree.is_balanced())

        remaining = sorted(set(keys) - set(to_delete))
        self.assertEqual([k for k, _ in tree.inorder()], remaining)
        for k in remaining:
            self.assertEqual(tree[k], k * 2)
        for k in to_delete:
            self.assertNotIn(k, tree)


if __name__ == "__main__":
    unittest.main()
