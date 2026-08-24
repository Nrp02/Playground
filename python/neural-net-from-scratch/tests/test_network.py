"""Unit tests for network.py: loss functions and the Network container."""

import math
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from layers import Dense, ReLU, Sigmoid
from network import (
    LOSSES, Network, binary_cross_entropy_loss, categorical_cross_entropy_loss,
    mse_loss, softmax,
)


class TestMSELoss(unittest.TestCase):
    def test_known_value(self):
        y_pred = np.array([[2.0]])
        y_true = np.array([[0.0]])
        loss, grad = mse_loss(y_pred, y_true)
        self.assertAlmostEqual(loss, 4.0)
        self.assertAlmostEqual(grad[0, 0], 4.0)

    def test_zero_error_gives_zero_loss_and_grad(self):
        y_pred = np.array([[1.0], [2.0], [3.0]])
        loss, grad = mse_loss(y_pred, y_pred.copy())
        self.assertAlmostEqual(loss, 0.0)
        np.testing.assert_allclose(grad, np.zeros_like(grad))

    def test_grad_matches_numeric_gradient(self):
        rng = np.random.default_rng(0)
        y_true = rng.normal(size=(4, 1))
        y_pred = rng.normal(size=(4, 1))
        eps = 1e-6
        numeric = np.zeros_like(y_pred)
        for i in range(y_pred.shape[0]):
            bumped = y_pred.copy()
            bumped[i, 0] += eps
            loss_hi, _ = mse_loss(bumped, y_true)
            bumped[i, 0] -= 2 * eps
            loss_lo, _ = mse_loss(bumped, y_true)
            numeric[i, 0] = (loss_hi - loss_lo) / (2 * eps)
        _, analytic = mse_loss(y_pred, y_true)
        np.testing.assert_allclose(analytic, numeric, atol=1e-5)


class TestBinaryCrossEntropyLoss(unittest.TestCase):
    def test_known_value(self):
        y_pred = np.array([[0.5]])
        y_true = np.array([[1.0]])
        loss, grad = binary_cross_entropy_loss(y_pred, y_true)
        self.assertAlmostEqual(loss, -math.log(0.5))
        self.assertAlmostEqual(grad[0, 0], -2.0)

    def test_confident_correct_prediction_gives_low_loss(self):
        loss, _ = binary_cross_entropy_loss(np.array([[0.999]]), np.array([[1.0]]))
        self.assertLess(loss, 0.01)

    def test_confident_wrong_prediction_gives_high_loss(self):
        loss, _ = binary_cross_entropy_loss(np.array([[0.001]]), np.array([[1.0]]))
        self.assertGreater(loss, 4.0)

    def test_does_not_divide_by_zero_at_boundary(self):
        loss, grad = binary_cross_entropy_loss(np.array([[1.0]]), np.array([[0.0]]))
        self.assertTrue(np.isfinite(loss))
        self.assertTrue(np.isfinite(grad).all())


class TestSoftmaxAndCategoricalCrossEntropy(unittest.TestCase):
    def test_softmax_rows_sum_to_one(self):
        x = np.array([[1.0, 2.0, 3.0], [0.0, 0.0, 0.0]])
        out = softmax(x)
        np.testing.assert_allclose(out.sum(axis=1), [1.0, 1.0])

    def test_softmax_known_values(self):
        out = softmax(np.array([[1.0, 2.0, 3.0]]))
        expected = np.array([[0.09003057, 0.24472847, 0.66524096]])
        np.testing.assert_allclose(out, expected, atol=1e-6)

    def test_softmax_is_shift_invariant(self):
        x = np.array([[1.0, 2.0, 3.0]])
        np.testing.assert_allclose(softmax(x), softmax(x + 1000.0), atol=1e-6)

    def test_cce_known_value(self):
        y_pred = np.array([[0.7, 0.2, 0.1], [0.1, 0.1, 0.8]])
        y_true = np.array([[1.0, 0.0, 0.0], [0.0, 0.0, 1.0]])
        loss, grad = categorical_cross_entropy_loss(y_pred, y_true)
        expected_loss = -(math.log(0.7) + math.log(0.8)) / 2
        self.assertAlmostEqual(loss, expected_loss, places=6)
        np.testing.assert_allclose(grad, (y_pred - y_true) / 2, atol=1e-8)


class TestLossRegistry(unittest.TestCase):
    def test_all_expected_losses_registered(self):
        self.assertEqual(set(LOSSES), {"mse", "bce", "cce"})

    def test_registry_entries_are_callable(self):
        for fn in LOSSES.values():
            self.assertTrue(callable(fn))


class TestNetworkForward(unittest.TestCase):
    def test_composes_layers_in_order(self):
        rng = np.random.default_rng(0)
        net = Network([Dense(2, 3, learning_rate=0.0, rng=rng), ReLU(), Dense(3, 1, learning_rate=0.0, rng=rng)])
        x = np.array([[1.0, -1.0], [0.5, 0.5]])
        manual = x
        for layer in net.layers:
            manual = layer.forward(manual)
        # Re-run forward (fresh) and check it matches the manual replay.
        out = net.forward(x)
        np.testing.assert_allclose(out, manual)

    def test_predict_matches_forward(self):
        rng = np.random.default_rng(1)
        net = Network([Dense(2, 2, learning_rate=0.0, rng=rng), Sigmoid()])
        x = np.array([[0.1, 0.2]])
        np.testing.assert_allclose(net.predict(x), net.forward(x))

    def test_num_parameters(self):
        net = Network([
            Dense(2, 4, rng=np.random.default_rng(0)),
            ReLU(),
            Dense(4, 1, rng=np.random.default_rng(0)),
        ])
        # (2*4 weights + 4 bias) + (4*1 weights + 1 bias) = 12 + 5 = 17
        self.assertEqual(net.num_parameters(), 17)


class TestNetworkTraining(unittest.TestCase):
    def _tiny_and_dataset(self):
        # Linearly separable AND gate -- solvable even without a hidden
        # layer, so this isolates "does training work" from "is the
        # architecture expressive enough".
        x = np.array([[0.0, 0.0], [0.0, 1.0], [1.0, 0.0], [1.0, 1.0]])
        y = np.array([[0.0], [0.0], [0.0], [1.0]])
        return x, y

    def test_train_step_returns_scalar_loss(self):
        rng = np.random.default_rng(0)
        net = Network([Dense(2, 1, learning_rate=0.1, rng=rng), Sigmoid()])
        x, y = self._tiny_and_dataset()
        loss = net.train_step(x, y, binary_cross_entropy_loss)
        self.assertIsInstance(loss, float)

    def test_loss_decreases_over_training(self):
        rng = np.random.default_rng(0)
        net = Network([Dense(2, 1, learning_rate=0.5, rng=rng), Sigmoid()])
        x, y = self._tiny_and_dataset()
        history = net.train(x, y, epochs=500, loss_fn=binary_cross_entropy_loss)
        self.assertLess(history[-1], history[0])
        self.assertLess(history[-1], history[0] * 0.2)

    def test_solves_and_gate_with_high_accuracy(self):
        rng = np.random.default_rng(0)
        net = Network([Dense(2, 1, learning_rate=0.5, rng=rng), Sigmoid()])
        x, y = self._tiny_and_dataset()
        net.train(x, y, epochs=500, loss_fn=binary_cross_entropy_loss)
        predictions = (net.predict(x) >= 0.5).astype(float)
        np.testing.assert_array_equal(predictions, y)

    def test_callback_invoked_once_per_epoch_in_order(self):
        rng = np.random.default_rng(0)
        net = Network([Dense(2, 1, learning_rate=0.1, rng=rng), Sigmoid()])
        x, y = self._tiny_and_dataset()
        seen_epochs = []
        net.train(x, y, epochs=5, loss_fn=binary_cross_entropy_loss,
                  callback=lambda epoch, loss: seen_epochs.append(epoch))
        self.assertEqual(seen_epochs, [1, 2, 3, 4, 5])

    def test_minibatch_training_runs_and_reduces_loss(self):
        rng = np.random.default_rng(0)
        net = Network([Dense(2, 4, learning_rate=0.3, rng=rng), ReLU(),
                       Dense(4, 1, learning_rate=0.3, rng=rng), Sigmoid()])
        x, y = self._tiny_and_dataset()
        history = net.train(x, y, epochs=200, loss_fn=binary_cross_entropy_loss,
                             batch_size=2, rng=np.random.default_rng(0))
        self.assertLess(history[-1], history[0])

    def test_xor_requires_hidden_layer_but_is_solvable(self):
        # The historically famous case: a single Dense+Sigmoid (no hidden
        # layer) cannot separate XOR, but adding one hidden layer can.
        x = np.array([[0.0, 0.0], [0.0, 1.0], [1.0, 0.0], [1.0, 1.0]])
        y = np.array([[0.0], [1.0], [1.0], [0.0]])

        rng = np.random.default_rng(0)
        net = Network([
            Dense(2, 8, learning_rate=0.5, rng=rng),
            ReLU(),
            Dense(8, 1, learning_rate=0.5, rng=rng),
            Sigmoid(),
        ])
        net.train(x, y, epochs=2000, loss_fn=binary_cross_entropy_loss)
        predictions = (net.predict(x) >= 0.5).astype(float)
        np.testing.assert_array_equal(predictions, y)


if __name__ == "__main__":
    unittest.main()
