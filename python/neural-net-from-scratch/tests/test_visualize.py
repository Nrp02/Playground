"""Tests for visualize.py: the ASCII decision-boundary renderer."""

import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from visualize import _shade_for_probability, _to_cell_index, render_decision_boundary


class _ConstantNet:
    """Stub network that predicts a fixed probability everywhere."""

    def __init__(self, probability: float):
        self.probability = probability

    def predict(self, x: np.ndarray) -> np.ndarray:
        return np.full((len(x), 1), self.probability)


class _AxisAlignedNet:
    """Stub network: predicts class 1 iff x-coordinate > 0."""

    def predict(self, x: np.ndarray) -> np.ndarray:
        return (x[:, :1] > 0).astype(float)


class TestShadeForProbability(unittest.TestCase):
    def test_zero_gives_first_char(self):
        self.assertEqual(_shade_for_probability(0.0), " ")

    def test_one_gives_last_char(self):
        self.assertEqual(_shade_for_probability(1.0), "@")

    def test_out_of_range_is_clamped(self):
        self.assertEqual(_shade_for_probability(-5.0), " ")
        self.assertEqual(_shade_for_probability(5.0), "@")


class TestToCellIndex(unittest.TestCase):
    def test_lo_maps_to_zero(self):
        self.assertEqual(_to_cell_index(0.0, 0.0, 10.0, 21), 0)

    def test_hi_maps_to_last_index(self):
        self.assertEqual(_to_cell_index(10.0, 0.0, 10.0, 21), 20)

    def test_midpoint_maps_to_middle_index(self):
        self.assertEqual(_to_cell_index(5.0, 0.0, 10.0, 21), 10)

    def test_degenerate_range_does_not_divide_by_zero(self):
        # hi == lo would divide by zero in a naive implementation.
        idx = _to_cell_index(3.0, 3.0, 3.0, 11)
        self.assertTrue(0 <= idx < 11)


class TestRenderDecisionBoundary(unittest.TestCase):
    def test_output_has_correct_number_of_grid_rows(self):
        x = np.array([[0.0, 0.0], [1.0, 1.0]])
        y = np.array([[0.0], [1.0]])
        net = _ConstantNet(0.5)
        text = render_decision_boundary(net, x, y, width=20, height=10)
        grid_lines = text.splitlines()[:10]
        self.assertEqual(len(grid_lines), 10)
        for line in grid_lines:
            self.assertEqual(len(line), 20)

    def test_uniform_low_probability_fills_background_with_first_char(self):
        x = np.array([[0.0, 0.0]])
        y = np.array([[0.0]])
        net = _ConstantNet(0.0)
        text = render_decision_boundary(net, x, y, width=15, height=8)
        grid_lines = text.splitlines()[:8]
        # Every cell not overwritten by a data point should be the
        # "confident class 0" shading character.
        combined = "".join(grid_lines).replace("0", "").replace("1", "")
        self.assertTrue(all(ch == " " for ch in combined))

    def test_data_points_are_stamped_with_their_true_label(self):
        x = np.array([[-5.0, -5.0], [5.0, 5.0]])
        y = np.array([[0.0], [1.0]])
        net = _ConstantNet(0.5)
        text = render_decision_boundary(net, x, y, width=41, height=21)
        grid_lines = text.splitlines()[:21]
        combined = "".join(grid_lines)
        self.assertIn("0", combined)
        self.assertIn("1", combined)

    def test_boundary_visible_for_axis_aligned_classifier(self):
        rng = np.random.default_rng(0)
        x = rng.uniform(-3, 3, size=(30, 2))
        y = (x[:, :1] > 0).astype(float)
        net = _AxisAlignedNet()
        text = render_decision_boundary(net, x, y, width=41, height=21)
        grid_lines = text.splitlines()[:21]
        # Left half of each row should be the "class 0" shade, right half
        # should be the "class 1" shade (allowing for the middle column
        # and any stamped data points).
        left_chars = set()
        right_chars = set()
        for line in grid_lines:
            for i, ch in enumerate(line):
                if ch in "01":
                    continue
                (left_chars if i < len(line) // 3 else right_chars).add(ch)
        self.assertIn(" ", left_chars)
        self.assertIn("@", right_chars)

    def test_legend_present(self):
        x = np.array([[0.0, 0.0], [1.0, 1.0]])
        y = np.array([[0.0], [1.0]])
        net = _ConstantNet(0.5)
        text = render_decision_boundary(net, x, y, width=15, height=8)
        self.assertIn("x in [", text)
        self.assertIn("y in [", text)


if __name__ == "__main__":
    unittest.main()
