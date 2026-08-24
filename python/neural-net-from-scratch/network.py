"""
Network: composes a list of Layer objects (Dense + Activation) into a
full feedforward model, wires up the forward pass, and drives
backpropagation through the layer stack via the chain rule. Loss
functions live here too since they're the starting point of that chain --
each one returns both the scalar loss (for logging) and the gradient of
the loss with respect to the network's output (to hand to `backward`).
"""

from __future__ import annotations

import numpy as np

from layers import Layer


def mse_loss(y_pred: np.ndarray, y_true: np.ndarray) -> tuple[float, np.ndarray]:
    """Mean squared error, averaged over every element (batch * outputs)."""
    diff = y_pred - y_true
    loss = float(np.mean(diff ** 2))
    grad = 2.0 * diff / y_true.size
    return loss, grad


def binary_cross_entropy_loss(y_pred: np.ndarray, y_true: np.ndarray,
                                eps: float = 1e-12) -> tuple[float, np.ndarray]:
    """
    Binary cross-entropy for a sigmoid output layer, y_pred and y_true in
    [0, 1] with shape (batch, 1). Predictions are clipped away from the
    exact 0/1 boundary to keep log() finite.
    """
    p = np.clip(y_pred, eps, 1.0 - eps)
    loss = float(-np.mean(y_true * np.log(p) + (1.0 - y_true) * np.log(1.0 - p)))
    grad = (p - y_true) / (p * (1.0 - p) * y_true.size)
    return loss, grad


def categorical_cross_entropy_loss(y_pred: np.ndarray, y_true_onehot: np.ndarray,
                                     eps: float = 1e-12) -> tuple[float, np.ndarray]:
    """
    Cross-entropy for a softmax output layer. y_pred is (batch, classes)
    of probabilities that sum to 1 per row; y_true_onehot is a one-hot
    matrix of the same shape. Assumes y_pred came directly from softmax,
    so the returned gradient is the standard (p - y) simplification.
    """
    p = np.clip(y_pred, eps, 1.0 - eps)
    loss = float(-np.mean(np.sum(y_true_onehot * np.log(p), axis=1)))
    grad = (p - y_true_onehot) / y_true_onehot.shape[0]
    return loss, grad


def softmax(x: np.ndarray) -> np.ndarray:
    shifted = x - np.max(x, axis=1, keepdims=True)
    exp = np.exp(shifted)
    return exp / np.sum(exp, axis=1, keepdims=True)


LOSSES = {
    "mse": mse_loss,
    "bce": binary_cross_entropy_loss,
    "cce": categorical_cross_entropy_loss,
}


class Network:
    """A stack of layers evaluated in order forward, and reverse order backward."""

    def __init__(self, layers: list[Layer]):
        self.layers = layers

    def forward(self, x: np.ndarray) -> np.ndarray:
        out = x
        for layer in self.layers:
            out = layer.forward(out)
        return out

    def backward(self, grad_loss: np.ndarray) -> np.ndarray:
        grad = grad_loss
        for layer in reversed(self.layers):
            grad = layer.backward(grad)
        return grad

    def predict(self, x: np.ndarray) -> np.ndarray:
        """Forward pass without tracking anything beyond what forward() needs."""
        return self.forward(x)

    def train_step(self, x: np.ndarray, y: np.ndarray, loss_fn=mse_loss) -> float:
        """One full-batch step: forward -> loss -> backward -> weight update."""
        y_pred = self.forward(x)
        loss, grad = loss_fn(y_pred, y)
        self.backward(grad)
        return loss

    def train(self, x: np.ndarray, y: np.ndarray, epochs: int, loss_fn=mse_loss,
               batch_size: int | None = None, rng: np.random.Generator | None = None,
               callback=None) -> list[float]:
        """
        Train for `epochs` passes over the data. If `batch_size` is given,
        each epoch shuffles the data and steps through it in mini-batches;
        otherwise each epoch is a single full-batch gradient step.
        Returns the per-epoch loss history (mean loss for mini-batch mode).
        """
        rng = rng if rng is not None else np.random.default_rng()
        history = []

        for epoch in range(1, epochs + 1):
            if batch_size is None:
                loss = self.train_step(x, y, loss_fn)
            else:
                perm = rng.permutation(len(x))
                x_shuf, y_shuf = x[perm], y[perm]
                batch_losses = []
                for start in range(0, len(x), batch_size):
                    end = start + batch_size
                    batch_losses.append(self.train_step(x_shuf[start:end], y_shuf[start:end], loss_fn))
                loss = float(np.mean(batch_losses))

            history.append(loss)
            if callback is not None:
                callback(epoch, loss)

        return history

    def num_parameters(self) -> int:
        total = 0
        for layer in self.layers:
            weights = getattr(layer, "weights", None)
            bias = getattr(layer, "bias", None)
            if weights is not None:
                total += weights.size
            if bias is not None:
                total += bias.size
        return total

    def __repr__(self) -> str:  # pragma: no cover - debugging helper
        parts = " -> ".join(repr(layer) for layer in self.layers)
        return f"Network[{parts}]"
