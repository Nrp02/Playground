import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from skip_list import SkipList


class TestInsertAndSearch(unittest.TestCase):
    def test_search_missing_key_returns_false(self):
        skip_list = SkipList(seed=1)
        found, value = skip_list.search("a")
        self.assertFalse(found)
        self.assertIsNone(value)

    def test_insert_and_search_single_key(self):
        skip_list = SkipList(seed=1)
        skip_list.insert("a", 1)
        found, value = skip_list.search("a")
        self.assertTrue(found)
        self.assertEqual(value, 1)

    def test_insert_many_keys_all_findable(self):
        skip_list = SkipList(seed=2)
        for i in range(500):
            skip_list.insert(i, i * i)
        self.assertEqual(len(skip_list), 500)
        for i in range(500):
            found, value = skip_list.search(i)
            self.assertTrue(found)
            self.assertEqual(value, i * i)

    def test_contains_and_getitem(self):
        skip_list = SkipList(seed=3)
        skip_list.insert("k", "v")
        self.assertIn("k", skip_list)
        self.assertNotIn("missing", skip_list)
        self.assertEqual(skip_list["k"], "v")
        with self.assertRaises(KeyError):
            _ = skip_list["missing"]


class TestDuplicateKeyUpdate(unittest.TestCase):
    def test_insert_duplicate_key_updates_value(self):
        skip_list = SkipList(seed=4)
        skip_list.insert("a", 1)
        skip_list.insert("a", 2)
        found, value = skip_list.search("a")
        self.assertTrue(found)
        self.assertEqual(value, 2)

    def test_insert_duplicate_key_does_not_grow_size(self):
        skip_list = SkipList(seed=5)
        skip_list.insert("a", 1)
        skip_list.insert("a", 2)
        skip_list.insert("b", 3)
        self.assertEqual(len(skip_list), 2)


class TestDelete(unittest.TestCase):
    def test_delete_existing_key_returns_true(self):
        skip_list = SkipList(seed=6)
        skip_list.insert("a", 1)
        self.assertTrue(skip_list.delete("a"))
        self.assertNotIn("a", skip_list)
        self.assertEqual(len(skip_list), 0)

    def test_delete_missing_key_returns_false(self):
        skip_list = SkipList(seed=7)
        skip_list.insert("a", 1)
        self.assertFalse(skip_list.delete("b"))
        self.assertEqual(len(skip_list), 1)

    def test_delete_many_keys_leaves_rest_intact(self):
        skip_list = SkipList(seed=8)
        for i in range(300):
            skip_list.insert(i, i)
        for i in range(0, 300, 2):
            self.assertTrue(skip_list.delete(i))
        self.assertEqual(len(skip_list), 150)
        for i in range(300):
            found, _ = skip_list.search(i)
            self.assertEqual(found, i % 2 == 1)

    def test_delete_all_resets_level(self):
        skip_list = SkipList(seed=9)
        for i in range(200):
            skip_list.insert(i, i)
        for i in range(200):
            skip_list.delete(i)
        self.assertEqual(len(skip_list), 0)
        self.assertEqual(skip_list.level(), 0)
        self.assertEqual(list(skip_list), [])


class TestIterationOrder(unittest.TestCase):
    def test_iteration_yields_sorted_order(self):
        skip_list = SkipList(seed=10)
        values = [5, 3, 8, 1, 9, 2, 7, 4, 6, 0]
        for v in values:
            skip_list.insert(v, str(v))
        self.assertEqual(skip_list.keys(), sorted(values))

    def test_iteration_yields_key_value_pairs(self):
        skip_list = SkipList(seed=11)
        skip_list.insert(2, "two")
        skip_list.insert(1, "one")
        skip_list.insert(3, "three")
        self.assertEqual(list(skip_list), [(1, "one"), (2, "two"), (3, "three")])

    def test_range_query_returns_half_open_interval(self):
        skip_list = SkipList(seed=12)
        for i in range(20):
            skip_list.insert(i, i)
        result = list(skip_list.range(5, 10))
        self.assertEqual(result, [(i, i) for i in range(5, 10)])

    def test_range_query_empty_when_no_keys_in_bounds(self):
        skip_list = SkipList(seed=13)
        for i in range(20):
            skip_list.insert(i, i)
        self.assertEqual(list(skip_list.range(100, 200)), [])


class TestLevelIntrospection(unittest.TestCase):
    def test_empty_list_has_level_zero(self):
        skip_list = SkipList(seed=14)
        self.assertEqual(skip_list.level(), 0)

    def test_height_distribution_totals_match_size(self):
        skip_list = SkipList(seed=15)
        for i in range(1000):
            skip_list.insert(i, i)
        distribution = skip_list.height_distribution()
        self.assertEqual(sum(distribution.values()), len(skip_list))

    def test_height_of_known_and_missing_key(self):
        skip_list = SkipList(seed=16)
        skip_list.insert("a", 1)
        self.assertIsNotNone(skip_list.height_of("a"))
        self.assertIsNone(skip_list.height_of("missing"))

    def test_stats_shape(self):
        skip_list = SkipList(seed=17)
        for i in range(100):
            skip_list.insert(i, i)
        stats = skip_list.stats()
        self.assertEqual(stats["size"], 100)
        self.assertEqual(stats["max_level"], skip_list.level())
        self.assertGreaterEqual(stats["average_height"], 0.0)


if __name__ == "__main__":
    unittest.main()
