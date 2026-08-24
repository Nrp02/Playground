"""
Synthetic dataset generators. Everything here is produced locally with
numpy's random number generator -- nothing is downloaded or read from
disk, so the "dataset" is really just a reproducible function of a seed.

Two datasets are provided, both deliberately NOT linearly separable so
that a network without a hidden layer provably cannot solve them (this
is the whole historical point of the XOR problem for neural nets):

  - make_xor_dataset:   the classic 2-input XOR toggle, replicated with
                         small Gaussian jitter around each of the 4
                         corners so there's an actual batch of training
                         examples rather than 4 bare points.
  - make_blobs_dataset: four 2D Gaussian clusters positioned at the
                         corners of a square and labeled in a
                         checkerboard (XOR-like) pattern.
"""

from __future__ import annotations

import numpy as np

XOR_BASE_POINTS = np.array([
    [0.0, 0.0],
    [0.0, 1.0],
    [1.0, 0.0],
    [1.0, 1.0],
])
XOR_BASE_LABELS = np.array([0.0, 1.0, 1.0, 0.0])

BLOB_CENTERS = np.array([
    [-2.0, -2.0],
    [-2.0, 2.0],
    [2.0, -2.0],
    [2.0, 2.0],
])
BLOB_LABELS = np.array([0.0, 1.0, 1.0, 0.0])


def make_xor_dataset(n_samples_per_point: int = 64, noise: float = 0.05,
                       seed: int = 0) -> tuple[np.ndarray, np.ndarray]:
    """
    Build a jittered-XOR dataset: `n_samples_per_point` noisy copies of
    each of the 4 XOR corners, shuffled together.

    Returns (x, y) with x shape (4 * n_samples_per_point, 2) and
    y shape (4 * n_samples_per_point, 1), labels in {0.0, 1.0}.
    """
    rng = np.random.default_rng(seed)

    xs, ys = [], []
    for point, label in zip(XOR_BASE_POINTS, XOR_BASE_LABELS):
        jittered = point + rng.normal(0.0, noise, size=(n_samples_per_point, 2))
        xs.append(jittered)
        ys.append(np.full(n_samples_per_point, label))

    x = np.concatenate(xs, axis=0)
    y = np.concatenate(ys, axis=0).reshape(-1, 1)

    perm = rng.permutation(len(x))
    return x[perm], y[perm]


def make_blobs_dataset(n_samples: int = 400, noise: float = 0.6,
                         seed: int = 0) -> tuple[np.ndarray, np.ndarray]:
    """
    Four Gaussian blobs at the corners of a square, labeled in a
    checkerboard pattern (diagonal corners share a class) so the classes
    are not linearly separable -- a straight decision boundary can get at
    best ~50% on this, forcing the network to use its hidden layer(s).

    Returns (x, y) with x shape (n_samples, 2) and y shape (n_samples, 1).
    """
    rng = np.random.default_rng(seed)

    per_blob = n_samples // len(BLOB_CENTERS)
    xs, ys = [], []
    for center, label in zip(BLOB_CENTERS, BLOB_LABELS):
        points = center + rng.normal(0.0, noise, size=(per_blob, 2))
        xs.append(points)
        ys.append(np.full(per_blob, label))

    x = np.concatenate(xs, axis=0)
    y = np.concatenate(ys, axis=0).reshape(-1, 1)

    perm = rng.permutation(len(x))
    return x[perm], y[perm]


def train_test_split(x: np.ndarray, y: np.ndarray, test_ratio: float = 0.2,
                       seed: int = 0) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Shuffle and split (x, y) into (x_train, y_train, x_test, y_test)."""
    rng = np.random.default_rng(seed)
    n = len(x)
    idx = rng.permutation(n)
    n_test = int(round(n * test_ratio))
    test_idx, train_idx = idx[:n_test], idx[n_test:]
    return x[train_idx], y[train_idx], x[test_idx], y[test_idx]


def standardize(x: np.ndarray, mean: np.ndarray | None = None,
                  std: np.ndarray | None = None) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Zero-mean, unit-variance normalization per feature column. If `mean`
    and `std` are not given they're computed from `x` (fit on train,
    reuse on test). Returns (x_scaled, mean, std).
    """
    if mean is None:
        mean = x.mean(axis=0, keepdims=True)
    if std is None:
        std = x.std(axis=0, keepdims=True)
        std = np.where(std < 1e-8, 1.0, std)
    return (x - mean) / std, mean, std
