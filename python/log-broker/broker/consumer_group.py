from __future__ import annotations

from typing import Dict


class ConsumerGroup:
    def __init__(self, group_id: str, num_partitions: int):
        self.group_id = group_id
        self.committed_offsets: Dict[int, int] = {i: 0 for i in range(num_partitions)}

    def committed(self, partition_index: int) -> int:
        return self.committed_offsets.get(partition_index, 0)

    def commit(self, partition_index: int, offset: int) -> None:
        current = self.committed_offsets.get(partition_index, 0)
        if offset > current:
            self.committed_offsets[partition_index] = offset
