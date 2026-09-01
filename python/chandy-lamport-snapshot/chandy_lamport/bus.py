from __future__ import annotations

import heapq
import random
from typing import Any, Dict, List, Tuple


class MessageBus:
    def __init__(self, min_delay: int = 1, max_delay: int = 3, seed: int = 0) -> None:
        self.rng = random.Random(seed)
        self.min_delay = min_delay
        self.max_delay = max_delay
        self._heap: List[Tuple[int, int, int, int, Any]] = []
        self._seq = 0
        self._channel_last: Dict[Tuple[int, int], int] = {}

    def send(self, src: int, dest: int, message: Any, now: int) -> None:
        delay = self.rng.randint(self.min_delay, self.max_delay)
        deliver_at = now + delay
        key = (src, dest)
        last = self._channel_last.get(key, -1)
        if deliver_at <= last:
            deliver_at = last + 1
        self._channel_last[key] = deliver_at
        self._seq += 1
        heapq.heappush(self._heap, (deliver_at, self._seq, src, dest, message))

    def deliver_ready(self, now: int) -> List[Tuple[int, int, Any]]:
        ready: List[Tuple[int, int, Any]] = []
        while self._heap and self._heap[0][0] <= now:
            deliver_at, seq, src, dest, message = heapq.heappop(self._heap)
            ready.append((src, dest, message))
        return ready

    def pending_count(self) -> int:
        return len(self._heap)

    def pending_amount_sum(self) -> int:
        total = 0
        for _, _, _, _, message in self._heap:
            amount = getattr(message, "amount", None)
            if amount is not None:
                total += amount
        return total
