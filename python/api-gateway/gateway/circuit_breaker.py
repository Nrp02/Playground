from __future__ import annotations

import time
from enum import Enum
from typing import Callable

from .errors import CircuitOpenError


class CircuitState(Enum):
    CLOSED = "CLOSED"
    OPEN = "OPEN"
    HALF_OPEN = "HALF_OPEN"


class CircuitBreaker:
    def __init__(
        self,
        failure_threshold: int,
        cooldown_seconds: float,
        now: Callable[[], float] = time.monotonic,
    ) -> None:
        self.failure_threshold = failure_threshold
        self.cooldown_seconds = cooldown_seconds
        self._now = now
        self._state = CircuitState.CLOSED
        self._consecutive_failures = 0
        self._opened_at = 0.0
        self._probe_in_flight = False

    @property
    def state(self) -> CircuitState:
        return self._state

    def before_call(self) -> None:
        if self._state is CircuitState.OPEN:
            if self._now() - self._opened_at >= self.cooldown_seconds:
                self._state = CircuitState.HALF_OPEN
                self._probe_in_flight = True
                return
            raise CircuitOpenError()
        if self._state is CircuitState.HALF_OPEN:
            if self._probe_in_flight:
                raise CircuitOpenError()
            self._probe_in_flight = True
            return

    def record_success(self) -> None:
        if self._state is CircuitState.HALF_OPEN:
            self._state = CircuitState.CLOSED
            self._probe_in_flight = False
        self._consecutive_failures = 0

    def record_failure(self) -> None:
        if self._state is CircuitState.HALF_OPEN:
            self._state = CircuitState.OPEN
            self._opened_at = self._now()
            self._probe_in_flight = False
            self._consecutive_failures = 0
            return
        self._consecutive_failures += 1
        if self._consecutive_failures >= self.failure_threshold:
            self._state = CircuitState.OPEN
            self._opened_at = self._now()
            self._consecutive_failures = 0
