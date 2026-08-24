"""Unit tests for data.py: synthetic dataset generation."""

import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from data import make_blobs_dataset, make_xor_dataset, standardize, train_test_split


class TestXORDataset(unittest.TestCase):
    def test_shapes(self):
        x, y = make_xor_dataset(n_samples_per_point=10, seed=0)
        self.assertEqual(x.shape, (40, 2))
        self.assertEqual(y.shape, (40, 1))

    def test_labels_are_balanced(self):
        x, y = make_xor_dataset(n_samples_per_point=10, seed=0)
        self.assertEqual(int(np.sum(y == 1.0)), 20)
        self.assertEqual(int(np.sum(y == 0.0)), 20)

    def test_points_cluster_near_corners(self):
        x, y = make_xor_dataset(n_samples_per_point=200, noise=0.01, seed=0)
        corners = np.array([[0, 0], [0, 1], [1, 0], [1, 1]])
        for point in x:
            distances = np.linalg.norm(corners - point, axis=1)
            self.assertLess(distances.min(), 0.2)

    def test_labels_match_xor_of_nearest_corner(self):
        x, y = make_xor_dataset(n_samples_per_point=50, noise=0.02, seed=1)
        corners = np.array([[0, 0], [0, 1], [1, 0], [1, 1]])
        corner_labels = np.array([0.0, 1.0, 1.0, 0.0])
        for point, label in zip(x, y):
            nearest = np.argmin(np.linalg.norm(corners - point, axis=1))
            self.assertEqual(corner_labels[nearest], label[0])

    def test_reproducible_with_same_seed(self):
        x1, y1 = make_xor_dataset(seed=42)
        x2, y2 = make_xor_dataset(seed=42)
        np.testing.assert_array_equal(x1, x2)
        np.testing.assert_array_equal(y1, y2)

    def test_different_seeds_give_different_data(self):
        x1, _ = make_xor_dataset(seed=1)
        x2, _ = make_xor_dataset(seed=2)
        self.assertFalse(np.array_equal(x1, x2))


class TestBlobsDataset(unittest.TestCase):
    def test_shapes(self):
        x, y = make_blobs_dataset(n_samples=400, seed=0)
        self.assertEqual(x.shape, (400, 2))
        self.assertEqual(y.shape, (400, 1))

    def test_labels_are_balanced(self):
        x, y = make_blobs_dataset(n_samples=400, seed=0)
        self.assertEqual(int(np.sum(y == 1.0)), 200)
        self.assertEqual(int(np.sum(y == 0.0)), 200)

    def test_low_noise_is_not_linearly_separable_by_a_single_axis(self):
        # Checkerboard label assignment: neither x nor y coordinate alone
        # predicts the label, unlike e.g. two side-by-side blobs would.
        x, y = make_blobs_dataset(n_samples=400, noise=0.3, seed=0)
        # A rule of "label = (x[:,0] > 0)" should do little better than
        # chance on a checkerboard pattern.
        naive_rule = (x[:, 0] > 0).astype(float).reshape(-1, 1)
        agreement = np.mean(naive_rule == y)
        self.assertLess(abs(agreement - 0.5), 0.15)


class TestTrainTestSplit(unittest.TestCase):
    def test_sizes_add_up(self):
        x = np.arange(100).reshape(50, 2).astype(float)
        y = np.arange(50).reshape(50, 1).astype(float)
        x_train, y_train, x_test, y_test = train_test_split(x, y, test_ratio=0.2, seed=0)
        self.assertEqual(len(x_train) + len(x_test), 50)
        self.assertEqual(len(x_test), 10)
        self.assertEqual(len(y_train), len(x_train))
        self.assertEqual(len(y_test), len(x_test))

    def test_train_and_test_do_not_overlap(self):
        x = np.arange(200).reshape(100, 2).astype(float)
        y = np.arange(100).reshape(100, 1).astype(float)
        x_train, _, x_test, _ = train_test_split(x, y, test_ratio=0.3, seed=0)
        train_rows = {tuple(row) for row in x_train}
        test_rows = {tuple(row) for row in x_test}
        self.assertEqual(train_rows & test_rows, set())

    def test_x_y_correspondence_preserved(self):
        # y[i] should still be the label for x[i] after shuffling+splitting.
        x = np.arange(20).reshape(10, 2).astype(float)
        y = (x[:, :1] + x[:, 1:]) * 10  # y derived deterministically from x
        x_train, y_train, x_test, y_test = train_test_split(x, y, test_ratio=0.4, seed=3)
        for xi, yi in zip(x_train, y_train):
            self.assertAlmostEqual(yi[0], (xi[0] + xi[1]) * 10)
        for xi, yi in zip(x_test, y_test):
            self.assertAlmostEqual(yi[0], (xi[0] + xi[1]) * 10)


class TestStandardize(unittest.TestCase):
    def test_output_has_zero_mean_unit_std(self):
        rng = np.random.default_rng(0)
        x = rng.normal(loc=5.0, scale=3.0, size=(1000, 2))
        scaled, mean, std = standardize(x)
        np.testing.assert_allclose(scaled.mean(axis=0), [0.0, 0.0], atol=1e-8)
        np.testing.assert_allclose(scaled.std(axis=0), [1.0, 1.0], atol=1e-8)

    def test_reuses_given_mean_and_std_for_test_data(self):
        x_train = np.array([[0.0], [2.0], [4.0]])
        _, mean, std = standardize(x_train)
        x_test = np.array([[2.0]])
        scaled_test, _, _ = standardize(x_test, mean=mean, std=std)
        # x_train mean is 2.0, so the test point (which equals the mean)
        # should scale to exactly 0.
        np.testing.assert_allclose(scaled_test, [[0.0]], atol=1e-8)

    def test_handles_zero_variance_column_without_dividing_by_zero(self):
        x = np.array([[1.0, 5.0], [1.0, 7.0], [1.0, 9.0]])
        scaled, _, _ = standardize(x)
        self.assertTrue(np.isfinite(scaled).all())
        np.testing.assert_allclose(scaled[:, 0], [0.0, 0.0, 0.0])


if __name__ == "__main__":
    unittest.main()
