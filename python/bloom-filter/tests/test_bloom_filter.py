import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from bloom.bloom_filter import BloomFilter


class TestBloomFilterBasics(unittest.TestCase):
    def test_added_items_are_always_reported_present(self):
        bloom = BloomFilter(expected_items=1000, target_fpr=0.01)
        items = [f"item-{i}" for i in range(500)]
        for item in items:
            bloom.add(item)
        for item in items:
            self.assertTrue(bloom.might_contain(item))

    def test_empty_filter_reports_nothing_present(self):
        bloom = BloomFilter(expected_items=1000, target_fpr=0.01)
        self.assertFalse(bloom.might_contain("anything"))

    def test_contains_operator_matches_might_contain(self):
        bloom = BloomFilter(expected_items=100, target_fpr=0.01)
        bloom.add("x")
        self.assertTrue("x" in bloom)
        self.assertFalse("y" in bloom)

    def test_len_tracks_number_of_additions(self):
        bloom = BloomFilter(expected_items=100, target_fpr=0.01)
        for i in range(10):
            bloom.add(f"item-{i}")
        self.assertEqual(len(bloom), 10)


class TestBloomFilterSizing(unittest.TestCase):
    def test_rejects_non_positive_expected_items(self):
        with self.assertRaises(ValueError):
            BloomFilter(expected_items=0, target_fpr=0.01)

    def test_rejects_invalid_target_fpr(self):
        with self.assertRaises(ValueError):
            BloomFilter(expected_items=100, target_fpr=0)
        with self.assertRaises(ValueError):
            BloomFilter(expected_items=100, target_fpr=1)

    def test_tighter_target_fpr_produces_larger_bit_array(self):
        loose = BloomFilter(expected_items=1000, target_fpr=0.1)
        tight = BloomFilter(expected_items=1000, target_fpr=0.0001)
        self.assertGreater(tight.size_bits, loose.size_bits)
        self.assertGreaterEqual(tight.num_hashes, loose.num_hashes)


class TestBloomFilterEmpiricalRate(unittest.TestCase):
    def test_empirical_false_positive_rate_is_within_order_of_magnitude_of_target(self):
        rng = random.Random(42)
        target_fpr = 0.02
        num_items = 5000
        bloom = BloomFilter(expected_items=num_items, target_fpr=target_fpr)

        inserted = [f"in-{i}" for i in range(num_items)]
        for item in inserted:
            bloom.add(item)

        probes = [f"out-{rng.randrange(1 << 30)}" for _ in range(20000)]
        false_positives = sum(1 for item in probes if bloom.might_contain(item))
        empirical_fpr = false_positives / len(probes)

        self.assertLess(empirical_fpr, target_fpr * 5)


if __name__ == "__main__":
    unittest.main()
