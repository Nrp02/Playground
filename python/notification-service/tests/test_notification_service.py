import unittest

from notify import Notification, NotificationService, SimulatedChannel


class RecordingClock:
    def __init__(self) -> None:
        self.sleeps = []

    def sleep(self, seconds: float) -> None:
        self.sleeps.append(seconds)


def make_service(max_retries: int = 3) -> tuple[NotificationService, RecordingClock]:
    clock = RecordingClock()
    service = NotificationService(
        max_retries=max_retries,
        base_delay=0.01,
        max_delay=0.05,
        sleep_fn=clock.sleep,
        rng=lambda: 0.5,
    )
    return service, clock


class TestFanOut(unittest.TestCase):
    def test_successful_fan_out_to_multiple_channels(self) -> None:
        service, _ = make_service()
        email = SimulatedChannel("email")
        sms = SimulatedChannel("sms")
        push = SimulatedChannel("push")
        notification = Notification("key-1", "hello", "world")

        result = service.send(notification, [email, sms, push])

        self.assertEqual(set(result.succeeded_channels()), {"email", "sms", "push"})
        self.assertEqual(result.failed_channels(), [])
        for outcome in result.outcomes.values():
            self.assertEqual(outcome.attempts, 1)


class TestRetry(unittest.TestCase):
    def test_retry_then_succeed_on_transient_failure(self) -> None:
        service, clock = make_service(max_retries=5)
        flaky = SimulatedChannel("flaky", fail_times=2)
        notification = Notification("key-2", "hello", "world")

        result = service.send(notification, [flaky])
        outcome = result.outcomes["flaky"]

        self.assertTrue(outcome.succeeded)
        self.assertEqual(outcome.attempts, 3)
        self.assertEqual(len(clock.sleeps), 2)

    def test_gives_up_after_max_retries_on_permanent_failure(self) -> None:
        service, clock = make_service(max_retries=3)
        broken = SimulatedChannel("broken", permanent_failure=True)
        notification = Notification("key-3", "hello", "world")

        result = service.send(notification, [broken])
        outcome = result.outcomes["broken"]

        self.assertFalse(outcome.succeeded)
        self.assertEqual(outcome.attempts, 1)
        self.assertEqual(clock.sleeps, [])

    def test_gives_up_after_max_retries_on_persistent_transient_failure(self) -> None:
        service, clock = make_service(max_retries=3)
        always_flaky = SimulatedChannel("always-flaky", fail_times=10)
        notification = Notification("key-4", "hello", "world")

        result = service.send(notification, [always_flaky])
        outcome = result.outcomes["always-flaky"]

        self.assertFalse(outcome.succeeded)
        self.assertEqual(outcome.attempts, 3)
        self.assertEqual(len(clock.sleeps), 2)


class TestDeduplication(unittest.TestCase):
    def test_duplicate_send_is_deduped_and_returns_original_result(self) -> None:
        service, clock = make_service()
        flaky = SimulatedChannel("flaky", fail_times=1)
        notification = Notification("key-5", "hello", "world")

        first = service.send(notification, [flaky])
        sleeps_after_first = len(clock.sleeps)
        second = service.send(notification, [flaky])

        self.assertFalse(first.deduplicated)
        self.assertTrue(second.deduplicated)
        self.assertEqual(first.outcomes, second.outcomes)
        self.assertEqual(flaky.attempts, 2)
        self.assertEqual(len(clock.sleeps), sleeps_after_first)

    def test_history_can_be_queried_per_notification(self) -> None:
        service, _ = make_service()
        email = SimulatedChannel("email")
        broken = SimulatedChannel("broken", permanent_failure=True)
        notification = Notification("key-6", "hello", "world")

        service.send(notification, [email, broken])
        stored = service.get_result("key-6")

        self.assertIn("email", stored.outcomes)
        self.assertIn("broken", stored.outcomes)
        self.assertTrue(stored.outcomes["email"].succeeded)
        self.assertFalse(stored.outcomes["broken"].succeeded)
        self.assertIn("key-6", service.history())


if __name__ == "__main__":
    unittest.main()
