from __future__ import annotations

import math

from .hashing import nth_hash


class CountMinSketch:
    def __init__(self, epsilon: float = 0.01, delta: float = 0.01):
        if not 0 < epsilon < 1:
            raise ValueError("epsilon must be in (0, 1)")
        if not 0 < delta < 1:
            raise ValueError("delta must be in (0, 1)")

        self.epsilon = epsilon
        self.delta = delta
        self.width = max(1, math.ceil(math.e / epsilon))
        self.depth = max(1, math.ceil(math.log(1 / delta)))
        self._table = [[0] * self.width for _ in range(self.depth)]
        self._total = 0

    def _columns(self, item: str):
        for row in range(self.depth):
            yield row, nth_hash(item, row, self.width)

    def add(self, item: str, count: int = 1) -> None:
        for row, col in self._columns(item):
            self._table[row][col] += count
        self._total += count

    def estimate(self, item: str) -> int:
        return min(self._table[row][col] for row, col in self._columns(item))

    def total_count(self) -> int:
        return self._total
