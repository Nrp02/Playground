from __future__ import annotations

from typing import Dict, Iterable, List


class VectorClock:
    def __init__(self, node_ids: Iterable[str]) -> None:
        self._node_ids: List[str] = list(node_ids)
        self.counters: Dict[str, int] = {node_id: 0 for node_id in self._node_ids}

    def increment(self, node_id: str) -> Dict[str, int]:
        if node_id not in self.counters:
            raise KeyError(node_id)
        self.counters[node_id] += 1
        return self.snapshot()

    def merge(self, other: Dict[str, int]) -> Dict[str, int]:
        for node_id, value in other.items():
            self.counters[node_id] = max(self.counters.get(node_id, 0), value)
        return self.snapshot()

    def snapshot(self) -> Dict[str, int]:
        return dict(self.counters)

    def node_ids(self) -> List[str]:
        return list(self._node_ids)


def compare_clocks(left: Dict[str, int], right: Dict[str, int]) -> str:
    keys = set(left.keys()) | set(right.keys())
    left_le_right = True
    right_le_left = True
    for key in keys:
        left_value = left.get(key, 0)
        right_value = right.get(key, 0)
        if left_value > right_value:
            left_le_right = False
        if right_value > left_value:
            right_le_left = False
    if left_le_right and right_le_left:
        return "equal"
    if left_le_right:
        return "happens-before"
    if right_le_left:
        return "happens-after"
    return "concurrent"
