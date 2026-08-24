from __future__ import annotations

import random
from typing import Any, Dict, Iterator, List, Optional, Tuple

MAX_LEVEL = 16
P = 0.5


class _Node:
    __slots__ = ("key", "value", "forward")

    def __init__(self, key: Any, value: Any, level: int):
        self.key = key
        self.value = value
        self.forward: List[Optional["_Node"]] = [None] * (level + 1)


class SkipList:
    def __init__(self, seed: Optional[int] = None, p: float = P, max_level: int = MAX_LEVEL):
        self._rng = random.Random(seed)
        self._p = p
        self._max_level = max_level
        self._head = _Node(None, None, max_level)
        self._level = 0
        self._size = 0

    def _random_level(self) -> int:
        level = 0
        while self._rng.random() < self._p and level < self._max_level:
            level += 1
        return level

    def _find_predecessors(self, key: Any) -> List[_Node]:
        update = [self._head] * (self._max_level + 1)
        current = self._head
        for level in range(self._level, -1, -1):
            while current.forward[level] is not None and current.forward[level].key < key:
                current = current.forward[level]
            update[level] = current
        return update

    def insert(self, key: Any, value: Any) -> None:
        update = self._find_predecessors(key)
        candidate = update[0].forward[0]
        if candidate is not None and candidate.key == key:
            candidate.value = value
            return

        level = self._random_level()
        if level > self._level:
            for i in range(self._level + 1, level + 1):
                update[i] = self._head
            self._level = level

        node = _Node(key, value, level)
        for i in range(level + 1):
            node.forward[i] = update[i].forward[i]
            update[i].forward[i] = node
        self._size += 1

    def search(self, key: Any) -> Tuple[bool, Any]:
        current = self._head
        for level in range(self._level, -1, -1):
            while current.forward[level] is not None and current.forward[level].key < key:
                current = current.forward[level]
        candidate = current.forward[0]
        if candidate is not None and candidate.key == key:
            return True, candidate.value
        return False, None

    def __contains__(self, key: Any) -> bool:
        found, _ = self.search(key)
        return found

    def __getitem__(self, key: Any) -> Any:
        found, value = self.search(key)
        if not found:
            raise KeyError(key)
        return value

    def delete(self, key: Any) -> bool:
        update = self._find_predecessors(key)
        candidate = update[0].forward[0]
        if candidate is None or candidate.key != key:
            return False

        for level in range(self._level + 1):
            if update[level].forward[level] is not candidate:
                break
            update[level].forward[level] = candidate.forward[level]

        while self._level > 0 and self._head.forward[self._level] is None:
            self._level -= 1

        self._size -= 1
        return True

    def __len__(self) -> int:
        return self._size

    def __iter__(self) -> Iterator[Tuple[Any, Any]]:
        current = self._head.forward[0]
        while current is not None:
            yield current.key, current.value
            current = current.forward[0]

    def keys(self) -> List[Any]:
        return [key for key, _ in self]

    def range(self, start: Any, end: Any) -> Iterator[Tuple[Any, Any]]:
        current = self._head
        for level in range(self._level, -1, -1):
            while current.forward[level] is not None and current.forward[level].key < start:
                current = current.forward[level]
        current = current.forward[0]
        while current is not None and current.key < end:
            yield current.key, current.value
            current = current.forward[0]

    def level(self) -> int:
        return self._level

    def height_of(self, key: Any) -> Optional[int]:
        current = self._head
        for level in range(self._level, -1, -1):
            while current.forward[level] is not None and current.forward[level].key < key:
                current = current.forward[level]
        candidate = current.forward[0]
        if candidate is not None and candidate.key == key:
            return len(candidate.forward) - 1
        return None

    def height_distribution(self) -> Dict[int, int]:
        distribution: Dict[int, int] = {}
        current = self._head.forward[0]
        while current is not None:
            height = len(current.forward) - 1
            distribution[height] = distribution.get(height, 0) + 1
            current = current.forward[0]
        return distribution

    def stats(self) -> Dict[str, Any]:
        distribution = self.height_distribution()
        total_forward_slots = sum((height + 1) * count for height, count in distribution.items())
        average_height = (
            sum(height * count for height, count in distribution.items()) / self._size
            if self._size
            else 0.0
        )
        return {
            "size": self._size,
            "max_level": self._level,
            "height_distribution": distribution,
            "average_height": average_height,
            "total_forward_slots": total_forward_slots,
        }
