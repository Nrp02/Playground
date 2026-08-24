"""Unit tests for layers.py: activation math and the Dense layer."""

import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from layers import (
    Dense, LeakyReLU, ReLU, Sigmoid, Tanh,
    leaky_relu, leaky_relu_derivative,
    relu, relu_derivative,
    sigmoid, sigmoid_derivative,
    tanh, tanh_derivative,
)


class TestSigmoid(unittest.TestCase):
    def test_zero_maps_to_half(self):
        self.assertAlmostEqual(sigmoid(np.array([0.0]))[0], 0.5)

    def test_large_positive_saturates_to_one(self):
        self.assertAlmostEqual(sigmoid(np.array([50.0]))[0], 1.0, places=6)

    def test_large_negative_saturates_to_zero(self):
        self.assertAlmostEqual(sigmoid(np.array([-50.0]))[0], 0.0, places=6)

    def test_no_overflow_warning_for_very_negative_input(self):
        # A naive `1 / (1 + exp(-x))` overflows exp() for very negative x.
        # This should compute cleanly without producing inf/nan.
        result = sigmoid(np.array([-1000.0]))
        self.assertTrue(np.isfinite(result).all())

    def test_derivative_peak_at_zero(self):
        self.assertAlmostEqual(sigmoid_derivative(np.array([0.0]))[0], 0.25)

    def test_derivative_matches_numeric_gradient(self):
        x = np.array([0.3, -0.7, 1.5])
        eps = 1e-6
        numeric = (sigmoid(x + eps) - sigmoid(x - eps)) / (2 * eps)
        analytic = sigmoid_derivative(x)
        np.testing.assert_allclose(analytic, numeric, atol=1e-5)


class TestReLU(unittest.TestCase):
    def test_negative_clamped_to_zero(self):
        np.testing.assert_array_equal(relu(np.array([-3.0, -0.1])), [0.0, 0.0])

    def test_positive_passes_through(self):
        np.testing.assert_array_equal(relu(np.array([2.0, 5.5])), [2.0, 5.5])

    def test_derivative(self):
        x = np.array([-1.0, 0.5, 3.0])
        np.testing.assert_array_equal(relu_derivative(x), [0.0, 1.0, 1.0])


class TestTanh(unittest.TestCase):
    def test_matches_numpy_tanh(self):
        x = np.array([-2.0, 0.0, 1.5])
        np.testing.assert_allclose(tanh(x), np.tanh(x))

    def test_zero_derivative_is_one(self):
        self.assertAlmostEqual(tanh_derivative(np.array([0.0]))[0], 1.0)

    def test_derivative_matches_numeric_gradient(self):
        x = np.array([0.3, -0.7, 1.5])
        eps = 1e-6
        numeric = (tanh(x + eps) - tanh(x - eps)) / (2 * eps)
        analytic = tanh_derivative(x)
        np.testing.assert_allclose(analytic, numeric, atol=1e-5)


class TestLeakyReLU(unittest.TestCase):
    def test_negative_slope(self):
        out = leaky_relu(np.array([-2.0]), alpha=0.1)
        self.assertAlmostEqual(out[0], -0.2)

    def test_positive_passthrough(self):
        out = leaky_relu(np.array([2.0]), alpha=0.1)
        self.assertAlmostEqual(out[0], 2.0)

    def test_derivative(self):
        x = np.array([-1.0, 1.0])
        d = leaky_relu_derivative(x, alpha=0.1)
        np.testing.assert_allclose(d, [0.1, 1.0])


class TestDenseForward(unittest.TestCase):
    def test_output_shape(self):
        layer = Dense(3, 4, rng=np.random.default_rng(0))
        x = np.zeros((5, 3))
        out = layer.forward(x)
        self.assertEqual(out.shape, (5, 4))

    def test_zero_input_yields_bias_only(self):
        layer = Dense(3, 2, rng=np.random.default_rng(0))
        layer.bias[:] = [1.0, -1.0]
        out = layer.forward(np.zeros((1, 3)))
        np.testing.assert_allclose(out, [[1.0, -1.0]])

    def test_matches_manual_matrix_multiply(self):
        layer = Dense(2, 2, rng=np.random.default_rng(0))
        layer.weights = np.array([[1.0, 2.0], [3.0, 4.0]])
        layer.bias = np.array([[0.5, -0.5]])
        x = np.array([[1.0, 1.0], [2.0, 0.0]])
        expected = x @ layer.weights + layer.bias
        np.testing.assert_allclose(layer.forward(x), expected)

    def test_weight_init_within_xavier_bound(self):
        in_f, out_f = 10, 20
        layer = Dense(in_f, out_f, rng=np.random.default_rng(0))
        limit = np.sqrt(6.0 / (in_f + out_f))
        self.assertTrue(np.all(np.abs(layer.weights) <= limit))

    def test_bias_initialized_to_zero(self):
        layer = Dense(4, 3, rng=np.random.default_rng(0))
        np.testing.assert_array_equal(layer.bias, np.zeros((1, 3)))


class TestDenseBackward(unittest.TestCase):
    def test_backward_before_forward_raises(self):
        layer = Dense(2, 2, rng=np.random.default_rng(0))
        with self.assertRaises(RuntimeError):
            layer.backward(np.zeros((1, 2)))

    def test_zero_learning_rate_leaves_weights_unchanged(self):
        layer = Dense(2, 2, learning_rate=0.0, rng=np.random.default_rng(0))
        original_weights = layer.weights.copy()
        original_bias = layer.bias.copy()
        layer.forward(np.array([[1.0, 2.0]]))
        layer.backward(np.array([[0.5, -0.5]]))
        np.testing.assert_array_equal(layer.weights, original_weights)
        np.testing.assert_array_equal(layer.bias, original_bias)

    def test_positive_learning_rate_moves_weights(self):
        layer = Dense(2, 2, learning_rate=0.1, rng=np.random.default_rng(0))
        original_weights = layer.weights.copy()
        layer.forward(np.array([[1.0, 2.0], [3.0, 1.0]]))
        layer.backward(np.array([[0.5, -0.5], [0.2, 0.1]]))
        self.assertFalse(np.allclose(layer.weights, original_weights))

    def test_grad_input_shape(self):
        layer = Dense(3, 5, rng=np.random.default_rng(0))
        layer.forward(np.zeros((7, 3)))
        grad_input = layer.backward(np.ones((7, 5)))
        self.assertEqual(grad_input.shape, (7, 3))

    def test_gradient_descent_reduces_squared_error(self):
        # A single Dense layer should be able to drive down squared error
        # on a simple fixed target via a handful of gradient steps.
        layer = Dense(2, 1, learning_rate=0.1, rng=np.random.default_rng(0))
        x = np.array([[1.0, 2.0], [2.0, 1.0], [0.5, 0.5]])
        target = np.array([[5.0], [3.0], [1.0]])

        def squared_error(pred):
            return float(np.mean((pred - target) ** 2))

        first_loss = squared_error(layer.forward(x))
        for _ in range(200):
            pred = layer.forward(x)
            grad = 2 * (pred - target) / target.size
            layer.backward(grad)
        last_loss = squared_error(layer.forward(x))

        self.assertLess(last_loss, first_loss)
        self.assertLess(last_loss, 1e-3)


class TestActivationLayers(unittest.TestCase):
    def test_sigmoid_layer_matches_plain_function(self):
        x = np.array([[0.0, 1.0, -1.0]])
        layer = Sigmoid()
        np.testing.assert_allclose(layer.forward(x), sigmoid(x))

    def test_relu_layer_backward_zeroes_negative_branch(self):
        layer = ReLU()
        layer.forward(np.array([[-1.0, 2.0]]))
        grad = layer.backward(np.array([[1.0, 1.0]]))
        np.testing.assert_array_equal(grad, [[0.0, 1.0]])

    def test_tanh_layer_round_trip_shape(self):
        layer = Tanh()
        x = np.random.default_rng(0).normal(size=(4, 6))
        out = layer.forward(x)
        grad = layer.backward(np.ones_like(out))
        self.assertEqual(out.shape, x.shape)
        self.assertEqual(grad.shape, x.shape)

    def test_leaky_relu_layer_alpha_applied(self):
        layer = LeakyReLU(alpha=0.2)
        out = layer.forward(np.array([[-5.0]]))
        self.assertAlmostEqual(out[0, 0], -1.0)


if __name__ == "__main__":
    unittest.main()
