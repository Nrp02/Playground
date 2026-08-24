import random
import sys
import unittest
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from bloom.count_min_sketch import CountMinSketch


class TestCountMinSketchBasics(unittest.TestCase):
    def test_estimate_matches_exact_count_for_single_item(self):
        sketch = CountMinSketch(epsilon=0.01, delta=0.01)
        for _ in range(37):
            sketch.add("x")
        self.assertEqual(sketch.estimate("x"), 37)

    def test_unseen_item_estimates_to_zero(self):
        sketch = CountMinSketch(epsilon=0.01, delta=0.01)
        sketch.add("x", count=10)
        self.assertEqual(sketch.estimate("never-added"), 0)

    def test_add_with_explicit_count(self):
        sketch = CountMinSketch(epsilon=0.01, delta=0.01)
        sketch.add("x", count=5)
        sketch.add("x", count=3)
        self.assertEqual(sketch.estimate("x"), 8)

    def test_total_count_tracks_all_additions(self):
        sketch = CountMinSketch(epsilon=0.01, delta=0.01)
        sketch.add("a", count=4)
        sketch.add("b", count=6)
        self.assertEqual(sketch.total_count(), 10)


class TestCountMinSketchSizing(unittest.TestCase):
    def test_rejects_invalid_epsilon_and_delta(self):
        with self.assertRaises(ValueError):
            CountMinSketch(epsilon=0, delta=0.01)
        with self.assertRaises(ValueError):
            CountMinSketch(epsilon=0.01, delta=1)

    def test_smaller_epsilon_produces_wider_table(self):
        loose = CountMinSketch(epsilon=0.1, delta=0.01)
        tight = CountMinSketch(epsilon=0.001, delta=0.01)
        self.assertGreater(tight.width, loose.width)


class TestCountMinSketchNeverUnderestimates(unittest.TestCase):
    def test_estimates_are_always_at_least_the_exact_count(self):
        rng = random.Random(7)
        vocabulary = [f"word-{i}" for i in range(50)]
        weights = [1.0 / rank for rank in range(1, len(vocabulary) + 1)]
        stream = rng.choices(vocabulary, weights=weights, k=20000)

        exact = Counter(stream)
        sketch = CountMinSketch(epsilon=0.01, delta=0.01)
        for item in stream:
            sketch.add(item)

        for item, count in exact.items():
            self.assertGreaterEqual(sketch.estimate(item), count)

    def test_heavy_hitters_are_estimated_within_a_small_relative_error(self):
        rng = random.Random(11)
        vocabulary = [f"word-{i}" for i in range(50)]
        weights = [1.0 / rank for rank in range(1, len(vocabulary) + 1)]
        stream = rng.choices(vocabulary, weights=weights, k=50000)

        exact = Counter(stream)
        sketch = CountMinSketch(epsilon=0.001, delta=0.001)
        for item in stream:
            sketch.add(item)

        top_item, top_count = exact.most_common(1)[0]
        estimate = sketch.estimate(top_item)
        relative_error = (estimate - top_count) / top_count
        self.assertLess(relative_error, 0.05)


if __name__ == "__main__":
    unittest.main()
