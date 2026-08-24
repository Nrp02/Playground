from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Callable, Dict, Iterable

from .auth import AuthGate
from .circuit_breaker import CircuitBreaker, CircuitState
from .errors import BackendError, RateLimitExceededError
from .rate_limiter import RateLimiter
from .router import Router


@dataclass
class Request:
    method: str
    path: str
    api_key: str
    body: str = ""


@dataclass
class Response:
    status: int
    body: str = ""


BackendHandler = Callable[[Request], Response]


class ApiGateway:
    def __init__(
        self,
        allowed_api_keys: Iterable[str],
        rate_limit_capacity: float,
        rate_limit_refill_rate: float,
        breaker_failure_threshold: int,
        breaker_cooldown_seconds: float,
        now: Callable[[], float] = time.monotonic,
    ) -> None:
        self._now = now
        self.auth = AuthGate(allowed_api_keys)
        self.router = Router()
        self.rate_limiter = RateLimiter(rate_limit_capacity, rate_limit_refill_rate, now=now)
        self._breaker_failure_threshold = breaker_failure_threshold
        self._breaker_cooldown_seconds = breaker_cooldown_seconds
        self._breakers: Dict[str, CircuitBreaker] = {}
        self._backends: Dict[str, BackendHandler] = {}

    def add_route(self, prefix: str, backend_name: str) -> None:
        self.router.add_route(prefix, backend_name)

    def register_backend(self, name: str, handler: BackendHandler) -> None:
        self._backends[name] = handler
        self._breakers[name] = CircuitBreaker(
            self._breaker_failure_threshold,
            self._breaker_cooldown_seconds,
            now=self._now,
        )

    def breaker_state(self, backend_name: str) -> CircuitState:
        return self._breakers[backend_name].state

    def handle(self, request: Request) -> Response:
        backend_name = self.router.resolve(request.path)

        self.auth.check(request.api_key)

        if not self.rate_limiter.allow(backend_name, request.api_key):
            raise RateLimitExceededError(f"{request.api_key}@{backend_name}")

        breaker = self._breakers[backend_name]
        breaker.before_call()

        try:
            response = self._backends[backend_name](request)
        except BackendError:
            breaker.record_failure()
            raise
        else:
            breaker.record_success()
            return response
