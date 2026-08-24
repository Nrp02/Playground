import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from gateway import (
    ApiGateway,
    AuthenticationError,
    BackendError,
    CircuitBreaker,
    CircuitOpenError,
    CircuitState,
    FlakyBackend,
    RateLimitExceededError,
    RateLimiter,
    Request,
    RouteNotFoundError,
    Router,
    TokenBucket,
    echo_backend,
)


class FakeClock:
    def __init__(self, start: float = 0.0) -> None:
        self.t = start

    def now(self) -> float:
        return self.t

    def advance(self, dt: float) -> None:
        self.t += dt


class TestRouter(unittest.TestCase):
    def test_resolves_longest_matching_prefix(self):
        router = Router()
        router.add_route("/api", "generic-service")
        router.add_route("/api/users", "users-service")
        self.assertEqual(router.resolve("/api/users/1"), "users-service")
        self.assertEqual(router.resolve("/api/orders/1"), "generic-service")

    def test_unmatched_path_raises(self):
        router = Router()
        router.add_route("/api", "generic-service")
        with self.assertRaises(RouteNotFoundError):
            router.resolve("/health")


class TestTokenBucket(unittest.TestCase):
    def test_denies_once_capacity_exhausted_and_refills_over_time(self):
        clock = FakeClock()
        bucket = TokenBucket(capacity=2, refill_rate=1.0, now=clock.now)
        self.assertTrue(bucket.allow())
        self.assertTrue(bucket.allow())
        self.assertFalse(bucket.allow())
        clock.advance(1.0)
        self.assertTrue(bucket.allow())
        self.assertFalse(bucket.allow())


class TestRateLimiter(unittest.TestCase):
    def test_limits_are_independent_per_route_and_client(self):
        clock = FakeClock()
        limiter = RateLimiter(capacity=1, refill_rate=0.0, now=clock.now)
        self.assertTrue(limiter.allow("route-a", "client-1"))
        self.assertFalse(limiter.allow("route-a", "client-1"))
        self.assertTrue(limiter.allow("route-a", "client-2"))
        self.assertTrue(limiter.allow("route-b", "client-1"))


class TestCircuitBreaker(unittest.TestCase):
    def test_trips_open_after_consecutive_failures(self):
        clock = FakeClock()
        breaker = CircuitBreaker(failure_threshold=3, cooldown_seconds=10.0, now=clock.now)
        for _ in range(2):
            breaker.before_call()
            breaker.record_failure()
        self.assertEqual(breaker.state, CircuitState.CLOSED)
        breaker.before_call()
        breaker.record_failure()
        self.assertEqual(breaker.state, CircuitState.OPEN)

    def test_short_circuits_while_open(self):
        clock = FakeClock()
        breaker = CircuitBreaker(failure_threshold=1, cooldown_seconds=10.0, now=clock.now)
        breaker.before_call()
        breaker.record_failure()
        self.assertEqual(breaker.state, CircuitState.OPEN)
        with self.assertRaises(CircuitOpenError):
            breaker.before_call()

    def test_allows_half_open_probe_after_cooldown_and_closes_on_success(self):
        clock = FakeClock()
        breaker = CircuitBreaker(failure_threshold=1, cooldown_seconds=5.0, now=clock.now)
        breaker.before_call()
        breaker.record_failure()
        self.assertEqual(breaker.state, CircuitState.OPEN)
        clock.advance(5.0)
        breaker.before_call()
        self.assertEqual(breaker.state, CircuitState.HALF_OPEN)
        breaker.record_success()
        self.assertEqual(breaker.state, CircuitState.CLOSED)

    def test_half_open_probe_failure_reopens_circuit(self):
        clock = FakeClock()
        breaker = CircuitBreaker(failure_threshold=1, cooldown_seconds=5.0, now=clock.now)
        breaker.before_call()
        breaker.record_failure()
        clock.advance(5.0)
        breaker.before_call()
        self.assertEqual(breaker.state, CircuitState.HALF_OPEN)
        breaker.record_failure()
        self.assertEqual(breaker.state, CircuitState.OPEN)

    def test_only_one_probe_allowed_in_flight_during_half_open(self):
        clock = FakeClock()
        breaker = CircuitBreaker(failure_threshold=1, cooldown_seconds=5.0, now=clock.now)
        breaker.before_call()
        breaker.record_failure()
        clock.advance(5.0)
        breaker.before_call()
        self.assertEqual(breaker.state, CircuitState.HALF_OPEN)
        with self.assertRaises(CircuitOpenError):
            breaker.before_call()

    def test_still_open_before_cooldown_elapses(self):
        clock = FakeClock()
        breaker = CircuitBreaker(failure_threshold=1, cooldown_seconds=5.0, now=clock.now)
        breaker.before_call()
        breaker.record_failure()
        clock.advance(4.0)
        with self.assertRaises(CircuitOpenError):
            breaker.before_call()
        self.assertEqual(breaker.state, CircuitState.OPEN)


class TestApiGateway(unittest.TestCase):
    def _build_gateway(
        self,
        clock: FakeClock,
        rate_limit_capacity: float = 10,
        rate_limit_refill_rate: float = 0.0,
        breaker_failure_threshold: int = 2,
        breaker_cooldown_seconds: float = 5.0,
    ):
        gateway = ApiGateway(
            allowed_api_keys=["good-key"],
            rate_limit_capacity=rate_limit_capacity,
            rate_limit_refill_rate=rate_limit_refill_rate,
            breaker_failure_threshold=breaker_failure_threshold,
            breaker_cooldown_seconds=breaker_cooldown_seconds,
            now=clock.now,
        )
        gateway.add_route("/users", "users-service")
        gateway.add_route("/orders", "orders-service")
        gateway.register_backend("users-service", echo_backend)
        flaky = FlakyBackend("orders-service")
        gateway.register_backend("orders-service", flaky)
        return gateway, flaky

    def test_routes_requests_to_correct_backend(self):
        clock = FakeClock()
        gateway, _ = self._build_gateway(clock)
        response = gateway.handle(Request(method="GET", path="/users/1", api_key="good-key"))
        self.assertEqual(response.status, 200)
        self.assertEqual(response.body, "ok:/users/1")

    def test_unknown_path_raises_route_not_found(self):
        clock = FakeClock()
        gateway, _ = self._build_gateway(clock)
        with self.assertRaises(RouteNotFoundError):
            gateway.handle(Request(method="GET", path="/health", api_key="good-key"))

    def test_bad_api_key_is_rejected(self):
        clock = FakeClock()
        gateway, _ = self._build_gateway(clock)
        with self.assertRaises(AuthenticationError):
            gateway.handle(Request(method="GET", path="/users/1", api_key="bad-key"))

    def test_rate_limiter_rejects_excess_requests_per_client_per_route(self):
        clock = FakeClock()
        gateway, _ = self._build_gateway(clock, rate_limit_capacity=2)
        gateway.handle(Request(method="GET", path="/users/1", api_key="good-key"))
        gateway.handle(Request(method="GET", path="/users/1", api_key="good-key"))
        with self.assertRaises(RateLimitExceededError):
            gateway.handle(Request(method="GET", path="/users/1", api_key="good-key"))

    def test_circuit_breaker_trips_and_recovers_through_gateway(self):
        clock = FakeClock()
        gateway, flaky = self._build_gateway(clock)
        flaky.healthy = False

        with self.assertRaises(BackendError):
            gateway.handle(Request(method="GET", path="/orders/1", api_key="good-key"))
        with self.assertRaises(BackendError):
            gateway.handle(Request(method="GET", path="/orders/2", api_key="good-key"))
        self.assertEqual(gateway.breaker_state("orders-service"), CircuitState.OPEN)

        with self.assertRaises(CircuitOpenError):
            gateway.handle(Request(method="GET", path="/orders/3", api_key="good-key"))

        clock.advance(5.0)
        flaky.healthy = True
        response = gateway.handle(Request(method="GET", path="/orders/4", api_key="good-key"))
        self.assertEqual(response.status, 200)
        self.assertEqual(gateway.breaker_state("orders-service"), CircuitState.CLOSED)

    def test_circuit_breaker_reopens_when_half_open_probe_fails(self):
        clock = FakeClock()
        gateway, flaky = self._build_gateway(clock)
        flaky.healthy = False

        with self.assertRaises(BackendError):
            gateway.handle(Request(method="GET", path="/orders/1", api_key="good-key"))
        with self.assertRaises(BackendError):
            gateway.handle(Request(method="GET", path="/orders/2", api_key="good-key"))
        self.assertEqual(gateway.breaker_state("orders-service"), CircuitState.OPEN)

        clock.advance(5.0)
        with self.assertRaises(BackendError):
            gateway.handle(Request(method="GET", path="/orders/3", api_key="good-key"))
        self.assertEqual(gateway.breaker_state("orders-service"), CircuitState.OPEN)


if __name__ == "__main__":
    unittest.main()
