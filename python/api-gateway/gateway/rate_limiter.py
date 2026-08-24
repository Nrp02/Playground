from __future__ import annotations

import time
from typing import Callable, Dict, Tuple


class TokenBucket:
    def __init__(self, capacity: float, refill_rate: float, now: Callable[[], float] = time.monotonic) -> None:
        self.capacity = capacity
        self.refill_rate = refill_rate
        self._now = now
        self._tokens = capacity
        self._last_refill = now()

    def _refill(self) -> None:
        current = self._now()
        elapsed = max(0.0, current - self._last_refill)
        self._tokens = min(self.capacity, self._tokens + elapsed * self.refill_rate)
        self._last_refill = current

    def allow(self, cost: float = 1.0) -> bool:
        self._refill()
        if self._tokens >= cost:
            self._tokens -= cost
            return True
        return False


class RateLimiter:
    def __init__(self, capacity: float, refill_rate: float, now: Callable[[], float] = time.monotonic) -> None:
        self.capacity = capacity
        self.refill_rate = refill_rate
        self._now = now
        self._buckets: Dict[Tuple[str, str], TokenBucket] = {}

    def _bucket_for(self, route: str, client_key: str) -> TokenBucket:
        key = (route, client_key)
        bucket = self._buckets.get(key)
        if bucket is None:
            bucket = TokenBucket(self.capacity, self.refill_rate, now=self._now)
            self._buckets[key] = bucket
        return bucket

    def allow(self, route: str, client_key: str) -> bool:
        return self._bucket_for(route, client_key).allow()
