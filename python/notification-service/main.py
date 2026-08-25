from notify import Notification, NotificationService, SimulatedChannel


class FakeClock:
    def __init__(self) -> None:
        self.total_slept = 0.0

    def sleep(self, seconds: float) -> None:
        self.total_slept += seconds
        print(f"    (simulated sleep {seconds:.3f}s, total {self.total_slept:.3f}s)")


def main() -> None:
    clock = FakeClock()
    service = NotificationService(max_retries=4, base_delay=0.2, max_delay=2.0, sleep_fn=clock.sleep)

    print("=== Fan-out with a flaky channel ===")
    email = SimulatedChannel("email")
    sms = SimulatedChannel("sms")
    push = SimulatedChannel("push", fail_times=2)
    notification = Notification(
        idempotency_key="order-42-shipped",
        subject="Your order shipped",
        body="Order 42 is on its way.",
    )
    result = service.send(notification, [email, sms, push])
    for name, outcome in result.outcomes.items():
        status = "SUCCESS" if outcome.succeeded else "FAILED"
        print(f"  {name}: {status} after {outcome.attempts} attempt(s)")

    print()
    print("=== Duplicate send with the same idempotency key ===")
    duplicate_result = service.send(notification, [email, sms, push])
    print(f"  deduplicated: {duplicate_result.deduplicated}")
    print(f"  same outcomes returned: {duplicate_result.outcomes == result.outcomes}")

    print()
    print("=== Permanent failure ===")
    broken = SimulatedChannel("carrier-pigeon", permanent_failure=True)
    urgent = Notification(
        idempotency_key="alert-99",
        subject="System alert",
        body="Disk usage above 90%.",
    )
    urgent_result = service.send(urgent, [broken])
    outcome = urgent_result.outcomes["carrier-pigeon"]
    print(f"  carrier-pigeon: succeeded={outcome.succeeded} attempts={outcome.attempts} error={outcome.error}")


if __name__ == "__main__":
    main()
