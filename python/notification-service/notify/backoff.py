from __future__ import annotations

import random
from typing import Callable


def exponential_backoff_with_jitter(
    attempt: int,
    base_delay: float,
    max_delay: float,
    rng: Callable[[], float] = random.random,
) -> float:
    uncapped = base_delay * (2 ** (attempt - 1))
    capped = min(uncapped, max_delay)
    return capped * (0.5 + rng())
