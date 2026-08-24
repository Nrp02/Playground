from gateway import (
    ApiGateway,
    AuthenticationError,
    BackendError,
    CircuitOpenError,
    FlakyBackend,
    RateLimitExceededError,
    Request,
    echo_backend,
)


class ManualClock:
    def __init__(self, start: float = 0.0) -> None:
        self.t = start

    def now(self) -> float:
        return self.t

    def advance(self, dt: float) -> None:
        self.t += dt


def build_gateway(clock: ManualClock):
    gateway = ApiGateway(
        allowed_api_keys=["key-alice", "key-bob"],
        rate_limit_capacity=3,
        rate_limit_refill_rate=1.0,
        breaker_failure_threshold=3,
        breaker_cooldown_seconds=5.0,
        now=clock.now,
    )
    gateway.add_route("/users", "users-service")
    gateway.add_route("/orders", "orders-service")
    gateway.register_backend("users-service", echo_backend)
    orders_backend = FlakyBackend("orders-service")
    gateway.register_backend("orders-service", orders_backend)
    return gateway, orders_backend


def main() -> int:
    clock = ManualClock()
    gateway, orders_backend = build_gateway(clock)

    print("=== routing requests across backends ===")
    for path in ["/users/1", "/orders/42", "/users/2"]:
        request = Request(method="GET", path=path, api_key="key-alice")
        response = gateway.handle(request)
        print(f"{path} -> status={response.status} body={response.body}")

    print("\n=== auth check rejects an unknown key ===")
    try:
        gateway.handle(Request(method="GET", path="/users/1", api_key="key-eve"))
    except AuthenticationError:
        print("rejected: unknown api key")

    print("\n=== rate limiter rejects requests past the bucket capacity ===")
    accepted = 0
    rejected = 0
    for _ in range(6):
        try:
            gateway.handle(Request(method="GET", path="/users/9", api_key="key-bob"))
            accepted += 1
        except RateLimitExceededError:
            rejected += 1
    print(f"accepted={accepted} rejected={rejected} (bucket capacity=3)")

    print("\n=== circuit breaker trips open after repeated backend failures ===")
    orders_backend.healthy = False
    clock.advance(10.0)
    for i in range(4):
        try:
            gateway.handle(Request(method="GET", path="/orders/1", api_key="key-alice"))
            print(f"attempt {i}: succeeded")
        except BackendError:
            state = gateway.breaker_state("orders-service").value
            print(f"attempt {i}: backend failed, breaker state={state}")
        except CircuitOpenError:
            state = gateway.breaker_state("orders-service").value
            print(f"attempt {i}: short-circuited, breaker state={state}")
        clock.advance(1.0)

    print("\n=== breaker stays open and fails fast during the cooldown ===")
    try:
        gateway.handle(Request(method="GET", path="/orders/2", api_key="key-alice"))
    except CircuitOpenError:
        state = gateway.breaker_state("orders-service").value
        print(f"short-circuited without calling backend, breaker state={state}")

    print("\n=== backend heals, half-open probe succeeds, breaker closes ===")
    clock.advance(5.0)
    orders_backend.healthy = True
    response = gateway.handle(Request(method="GET", path="/orders/3", api_key="key-alice"))
    state = gateway.breaker_state("orders-service").value
    print(f"probe request -> status={response.status} breaker state={state}")

    print("\n=== further requests succeed normally now that the breaker is closed ===")
    clock.advance(1.0)
    response = gateway.handle(Request(method="GET", path="/orders/4", api_key="key-alice"))
    state = gateway.breaker_state("orders-service").value
    print(f"/orders/4 -> status={response.status} breaker state={state}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
