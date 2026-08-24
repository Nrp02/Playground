from __future__ import annotations

import math

from .hashing import nth_hash


class BloomFilter:
    def __init__(self, expected_items: int, target_fpr: float = 0.01):
        if expected_items <= 0:
            raise ValueError("expected_items must be positive")
        if not 0 < target_fpr < 1:
            raise ValueError("target_fpr must be in (0, 1)")

        self.expected_items = expected_items
        self.target_fpr = target_fpr
        self.size_bits = self._optimal_size_bits(expected_items, target_fpr)
        self.num_hashes = self._optimal_num_hashes(self.size_bits, expected_items)
        self._bits = bytearray((self.size_bits + 7) // 8)
        self._count = 0

    @staticmethod
    def _optimal_size_bits(n: int, p: float) -> int:
        m = -(n * math.log(p)) / (math.log(2) ** 2)
        return max(8, math.ceil(m))

    @staticmethod
    def _optimal_num_hashes(m: int, n: int) -> int:
        k = (m / n) * math.log(2)
        return max(1, round(k))

    def _bit_positions(self, item: str):
        for i in range(self.num_hashes):
            yield nth_hash(item, i, self.size_bits)

    def add(self, item: str) -> None:
        for pos in self._bit_positions(item):
            self._bits[pos // 8] |= 1 << (pos % 8)
        self._count += 1

    def might_contain(self, item: str) -> bool:
        for pos in self._bit_positions(item):
            if not self._bits[pos // 8] & (1 << (pos % 8)):
                return False
        return True

    def __contains__(self, item: str) -> bool:
        return self.might_contain(item)

    def __len__(self) -> int:
        return self._count

    def estimated_false_positive_rate(self) -> float:
        exponent = -self.num_hashes * self._count / self.size_bits
        return (1 - math.exp(exponent)) ** self.num_hashes
