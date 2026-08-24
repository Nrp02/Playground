from __future__ import annotations

import zlib
from typing import Any, List, Optional, Tuple

from .partition import Partition


class Topic:
    def __init__(self, name: str, num_partitions: int):
        if num_partitions < 1:
            raise ValueError("num_partitions must be at least 1")
        self.name = name
        self.num_partitions = num_partitions
        self.partitions: List[Partition] = [Partition(i) for i in range(num_partitions)]
        self._round_robin_counter = 0

    def _partition_for_key(self, key: Optional[str]) -> int:
        if key is None:
            index = self._round_robin_counter % self.num_partitions
            self._round_robin_counter += 1
            return index
        digest = zlib.crc32(key.encode("utf-8"))
        return digest % self.num_partitions

    def append(self, value: Any, key: Optional[str] = None) -> Tuple[int, int]:
        partition_index = self._partition_for_key(key)
        offset = self.partitions[partition_index].append(key, value)
        return partition_index, offset
