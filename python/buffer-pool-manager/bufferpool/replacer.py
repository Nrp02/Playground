from __future__ import annotations

import itertools
from abc import ABC, abstractmethod
from collections import OrderedDict, deque
from typing import Deque, Dict, List, Optional, Set


class Replacer(ABC):
    name: str = "replacer"

    @abstractmethod
    def record_access(self, frame_id: int) -> None:
        raise NotImplementedError

    @abstractmethod
    def set_evictable(self, frame_id: int, evictable: bool) -> None:
        raise NotImplementedError

    @abstractmethod
    def evict(self) -> Optional[int]:
        raise NotImplementedError

    @abstractmethod
    def remove(self, frame_id: int) -> None:
        raise NotImplementedError

    @abstractmethod
    def evictable_size(self) -> int:
        raise NotImplementedError

    @abstractmethod
    def tracked_size(self) -> int:
        raise NotImplementedError


class LRUReplacer(Replacer):
    name = "LRU"

    def __init__(self) -> None:
        self._order: "OrderedDict[int, None]" = OrderedDict()
        self._evictable: Set[int] = set()

    def record_access(self, frame_id: int) -> None:
        self._order[frame_id] = None
        self._order.move_to_end(frame_id)

    def set_evictable(self, frame_id: int, evictable: bool) -> None:
        if frame_id not in self._order:
            return
        if evictable:
            self._evictable.add(frame_id)
        else:
            self._evictable.discard(frame_id)

    def evict(self) -> Optional[int]:
        for frame_id in self._order:
            if frame_id in self._evictable:
                self.remove(frame_id)
                return frame_id
        return None

    def remove(self, frame_id: int) -> None:
        self._order.pop(frame_id, None)
        self._evictable.discard(frame_id)

    def evictable_size(self) -> int:
        return len(self._evictable)

    def tracked_size(self) -> int:
        return len(self._order)


class ClockReplacer(Replacer):
    name = "CLOCK"

    def __init__(self) -> None:
        self._ring: List[int] = []
        self._reference: Dict[int, bool] = {}
        self._evictable: Set[int] = set()
        self._hand = 0

    def record_access(self, frame_id: int) -> None:
        if frame_id not in self._reference:
            self._ring.insert(self._hand, frame_id)
            self._hand = (self._hand + 1) % len(self._ring)
        self._reference[frame_id] = True

    def set_evictable(self, frame_id: int, evictable: bool) -> None:
        if frame_id not in self._reference:
            return
        if evictable:
            self._evictable.add(frame_id)
        else:
            self._evictable.discard(frame_id)

    def evict(self) -> Optional[int]:
        if not self._evictable:
            return None
        size = len(self._ring)
        for _ in range(2 * size):
            frame_id = self._ring[self._hand]
            self._hand = (self._hand + 1) % size
            if frame_id not in self._evictable:
                continue
            if self._reference.get(frame_id, False):
                self._reference[frame_id] = False
                continue
            self.remove(frame_id)
            return frame_id
        return None

    def remove(self, frame_id: int) -> None:
        if frame_id not in self._reference:
            return
        index = self._ring.index(frame_id)
        self._ring.pop(index)
        self._reference.pop(frame_id, None)
        self._evictable.discard(frame_id)
        if not self._ring:
            self._hand = 0
        else:
            if index < self._hand:
                self._hand -= 1
            self._hand %= len(self._ring)

    def evictable_size(self) -> int:
        return len(self._evictable)

    def tracked_size(self) -> int:
        return len(self._ring)


class LRUKReplacer(Replacer):
    def __init__(self, k: int = 2) -> None:
        if k < 1:
            raise ValueError("k must be at least 1")
        self._k = k
        self.name = f"LRU-{k}"
        self._history: Dict[int, Deque[int]] = {}
        self._evictable: Set[int] = set()
        self._clock = itertools.count(1)

    @property
    def k(self) -> int:
        return self._k

    def record_access(self, frame_id: int) -> None:
        history = self._history.get(frame_id)
        if history is None:
            history = deque(maxlen=self._k)
            self._history[frame_id] = history
        history.append(next(self._clock))

    def set_evictable(self, frame_id: int, evictable: bool) -> None:
        if frame_id not in self._history:
            return
        if evictable:
            self._evictable.add(frame_id)
        else:
            self._evictable.discard(frame_id)

    def evict(self) -> Optional[int]:
        victim: Optional[int] = None
        victim_key = (0, 0)
        for frame_id in self._evictable:
            history = self._history[frame_id]
            if len(history) < self._k:
                key = (1, -history[0])
            else:
                key = (0, -history[0])
            if victim is None or key > victim_key:
                victim = frame_id
                victim_key = key
        if victim is None:
            return None
        self.remove(victim)
        return victim

    def remove(self, frame_id: int) -> None:
        self._history.pop(frame_id, None)
        self._evictable.discard(frame_id)

    def evictable_size(self) -> int:
        return len(self._evictable)

    def tracked_size(self) -> int:
        return len(self._history)


def make_replacer(policy: str, k: int = 2) -> Replacer:
    key = policy.strip().lower()
    if key == "lru":
        return LRUReplacer()
    if key == "clock":
        return ClockReplacer()
    if key in ("lru-k", "lruk"):
        return LRUKReplacer(k)
    raise ValueError(f"unknown replacement policy {policy!r}")
