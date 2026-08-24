"""
Layer primitives for the from-scratch feedforward network.

Every layer implements a shared two-method interface:

    forward(x)          -> y            shape (batch, out_features)
    backward(grad_out)  -> grad_in      shape (batch, in_features)

`grad_out` is dL/d(layer_output), with the loss function's batch
normalization (the 1/N mean factor) already folded in by whatever
produced it -- either the loss function itself, or the layer that sits
downstream in the chain. Parametric layers (Dense) use `grad_out` to
update their own weights in place via plain gradient descent, and also
return dL/d(layer_input) so the caller can keep propagating backward.
Because the 1/N factor is already baked into `grad_out` from the start of
the chain, Dense.backward *sums* over the batch axis when computing
parameter gradients rather than averaging again -- averaging twice would
silently shrink every gradient by an extra factor of the batch size.
"""

from __future__ import annotations

import numpy as np


class Layer:
    """Abstract base class for anything that can sit in a Network."""

    def forward(self, x: np.ndarray) -> np.ndarray:
        raise NotImplementedError

    def backward(self, grad_output: np.ndarray) -> np.ndarray:
        raise NotImplementedError


class Dense(Layer):
    """
    Fully connected ("linear") layer: y = x @ W + b

    Weights are initialized with Glorot/Xavier uniform scaling based on
    fan-in and fan-out, which keeps the variance of activations roughly
    stable across layers at the start of training instead of exploding
    or vanishing.
    """

    def __init__(self, in_features: int, out_features: int, learning_rate: float = 0.1,
                 rng: np.random.Generator | None = None):
        rng = rng if rng is not None else np.random.default_rng()
        limit = np.sqrt(6.0 / (in_features + out_features))
        self.weights = rng.uniform(-limit, limit, size=(in_features, out_features))
        self.bias = np.zeros((1, out_features))
        self.learning_rate = learning_rate

        self.in_features = in_features
        self.out_features = out_features

        self._last_input: np.ndarray | None = None
        # Populated after backward(); handy for gradient-magnitude debugging.
        self.grad_weights: np.ndarray | None = None
        self.grad_bias: np.ndarray | None = None

    def forward(self, x: np.ndarray) -> np.ndarray:
        self._last_input = x
        return x @ self.weights + self.bias

    def backward(self, grad_output: np.ndarray) -> np.ndarray:
        if self._last_input is None:
            raise RuntimeError("Dense.backward called before forward")

        x = self._last_input

        # grad_output already carries the loss's 1/N normalization, so the
        # batch axis is contracted with a plain sum, not a mean.
        self.grad_weights = x.T @ grad_output
        self.grad_bias = np.sum(grad_output, axis=0, keepdims=True)
        grad_input = grad_output @ self.weights.T

        self.weights = self.weights - self.learning_rate * self.grad_weights
        self.bias = self.bias - self.learning_rate * self.grad_bias

        return grad_input

    def __repr__(self) -> str:  # pragma: no cover - debugging helper
        return f"Dense({self.in_features} -> {self.out_features})"


class Activation(Layer):
    """Base class for elementwise activation layers (no learnable params)."""

    def __init__(self):
        self._last_input: np.ndarray | None = None

    def forward(self, x: np.ndarray) -> np.ndarray:
        self._last_input = x
        return self._apply(x)

    def backward(self, grad_output: np.ndarray) -> np.ndarray:
        return grad_output * self._derivative(self._last_input)

    def _apply(self, x: np.ndarray) -> np.ndarray:
        raise NotImplementedError

    def _derivative(self, x: np.ndarray) -> np.ndarray:
        raise NotImplementedError

    def __repr__(self) -> str:  # pragma: no cover - debugging helper
        return f"{type(self).__name__}()"


def sigmoid(x: np.ndarray) -> np.ndarray:
    """Numerically stable logistic sigmoid (avoids overflow for large |x|)."""
    x = np.asarray(x, dtype=float)
    out = np.empty_like(x)
    positive = x >= 0
    negative = ~positive

    out[positive] = 1.0 / (1.0 + np.exp(-x[positive]))
    exp_x = np.exp(x[negative])
    out[negative] = exp_x / (1.0 + exp_x)
    return out


def sigmoid_derivative(x: np.ndarray) -> np.ndarray:
    s = sigmoid(x)
    return s * (1.0 - s)


def relu(x: np.ndarray) -> np.ndarray:
    return np.maximum(0.0, x)


def relu_derivative(x: np.ndarray) -> np.ndarray:
    return (x > 0.0).astype(x.dtype if hasattr(x, "dtype") else float)


def tanh(x: np.ndarray) -> np.ndarray:
    return np.tanh(x)


def tanh_derivative(x: np.ndarray) -> np.ndarray:
    return 1.0 - np.tanh(x) ** 2


def leaky_relu(x: np.ndarray, alpha: float = 0.01) -> np.ndarray:
    return np.where(x > 0, x, alpha * x)


def leaky_relu_derivative(x: np.ndarray, alpha: float = 0.01) -> np.ndarray:
    return np.where(x > 0, 1.0, alpha)


class Sigmoid(Activation):
    def _apply(self, x):
        return sigmoid(x)

    def _derivative(self, x):
        return sigmoid_derivative(x)


class ReLU(Activation):
    def _apply(self, x):
        return relu(x)

    def _derivative(self, x):
        return relu_derivative(x)


class Tanh(Activation):
    def _apply(self, x):
        return tanh(x)

    def _derivative(self, x):
        return tanh_derivative(x)


class LeakyReLU(Activation):
    def __init__(self, alpha: float = 0.01):
        super().__init__()
        self.alpha = alpha

    def _apply(self, x):
        return leaky_relu(x, self.alpha)

    def _derivative(self, x):
        return leaky_relu_derivative(x, self.alpha)
