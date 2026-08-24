from __future__ import annotations

import numpy as np


def dot(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    return np.sum(a * b, axis=-1)


def length(a: np.ndarray) -> np.ndarray:
    return np.sqrt(dot(a, a))


def normalize(a: np.ndarray) -> np.ndarray:
    n = length(a)
    n = np.where(n == 0, 1.0, n)
    return a / n[..., np.newaxis]


def reflect(v: np.ndarray, n: np.ndarray) -> np.ndarray:
    d = dot(v, n)[..., np.newaxis]
    return v - 2.0 * d * n


def clamp01(a: np.ndarray) -> np.ndarray:
    return np.clip(a, 0.0, 1.0)


def random_unit_vectors(n: int, rng: np.random.Generator) -> np.ndarray:
    vecs = rng.normal(size=(n, 3))
    return normalize(vecs)
