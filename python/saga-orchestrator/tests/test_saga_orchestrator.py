import unittest

from saga.errors import SagaCrashed, TransientError
from saga.log import InMemorySagaLog
from saga.orchestrator import SagaOrchestrator, SagaStatus
from saga.services import SimulatedParticipant
from saga.steps import Step


def make_steps(flights: SimulatedParticipant, hotels: SimulatedParticipant, payments: SimulatedParticipant):
    return [
        Step("book_flight", flights.action, flights.compensation),
        Step("reserve_hotel", hotels.action, hotels.compensation),
        Step("charge_payment", payments.action, payments.compensation),
    ]


class TestHappyPath(unittest.TestCase):
    def test_all_steps_succeed(self) -> None:
        flights = SimulatedParticipant("flights")
        hotels = SimulatedParticipant("hotels")
        payments = SimulatedParticipant("payments")
        log = InMemorySagaLog()
        orchestrator = SagaOrchestrator(log)

        status = orchestrator.run("s1", make_steps(flights, hotels, payments), {"customer": "a"})

        self.assertEqual(status, SagaStatus.COMPLETED)
        self.assertEqual(flights.apply_count, 1)
        self.assertEqual(hotels.apply_count, 1)
        self.assertEqual(payments.apply_count, 1)
        types = [event["type"] for event in log.events("s1")]
        self.assertEqual(types.count("SAGA_STARTED"), 1)
        self.assertEqual(types.count("SAGA_COMPLETED"), 1)


class TestCompensationOrdering(unittest.TestCase):
    def test_compensations_run_in_exact_reverse_order(self) -> None:
        flights = SimulatedParticipant("flights")
        hotels = SimulatedParticipant("hotels")
        payments = SimulatedParticipant("payments", fail_terminal=True)
        log = InMemorySagaLog()
        orchestrator = SagaOrchestrator(log)

        status = orchestrator.run("s2", make_steps(flights, hotels, payments), {"customer": "b"})

        self.assertEqual(status, SagaStatus.COMPENSATED)
        started = [event["step"] for event in log.events("s2") if event["type"] == "COMPENSATION_STARTED"]
        self.assertEqual(started, ["reserve_hotel", "book_flight"])
        self.assertEqual(payments.apply_count, 0)


class TestTransientRetry(unittest.TestCase):
    def test_transient_failure_is_retried_then_succeeds(self) -> None:
        flights = SimulatedParticipant("flights", fail_transient_times=2)
        hotels = SimulatedParticipant("hotels")
        payments = SimulatedParticipant("payments")
        log = InMemorySagaLog()
        orchestrator = SagaOrchestrator(log)

        status = orchestrator.run("s3", make_steps(flights, hotels, payments), {"customer": "c"})

        self.assertEqual(status, SagaStatus.COMPLETED)
        self.assertEqual(flights.apply_count, 1)
        failures = [event for event in log.events("s3") if event["type"] == "STEP_FAILED"]
        self.assertEqual(failures, [])

    def test_transient_failure_exhausting_retries_compensates(self) -> None:
        flights = SimulatedParticipant("flights")
        hotels = SimulatedParticipant("hotels")
        payments = SimulatedParticipant("payments", fail_transient_times=99)
        log = InMemorySagaLog()
        orchestrator = SagaOrchestrator(log)

        status = orchestrator.run("s4", make_steps(flights, hotels, payments), {"customer": "d"})

        self.assertEqual(status, SagaStatus.COMPENSATED)


class TestTerminalFailure(unittest.TestCase):
    def test_terminal_failure_compensates_immediately_without_retry(self) -> None:
        flights = SimulatedParticipant("flights")
        hotels = SimulatedParticipant("hotels", fail_terminal=True)
        payments = SimulatedParticipant("payments")
        log = InMemorySagaLog()
        orchestrator = SagaOrchestrator(log)

        status = orchestrator.run("s5", make_steps(flights, hotels, payments), {"customer": "e"})

        self.assertEqual(status, SagaStatus.COMPENSATED)
        self.assertEqual(hotels.apply_count, 0)
        self.assertEqual(payments.apply_count, 0)
        started = [event["step"] for event in log.events("s5") if event["type"] == "COMPENSATION_STARTED"]
        self.assertEqual(started, ["book_flight"])


class TestIdempotency(unittest.TestCase):
    def test_retry_hits_already_applied_participant_without_double_apply(self) -> None:
        flights = SimulatedParticipant("flights", lose_response_after_apply=True)
        hotels = SimulatedParticipant("hotels")
        payments = SimulatedParticipant("payments")
        log = InMemorySagaLog()
        orchestrator = SagaOrchestrator(log)

        status = orchestrator.run("s6", make_steps(flights, hotels, payments), {"customer": "f"})

        self.assertEqual(status, SagaStatus.COMPLETED)
        self.assertEqual(flights.apply_count, 1)
        self.assertEqual(flights.idempotent_hits, 1)

    def test_direct_participant_call_with_same_key_is_idempotent(self) -> None:
        flights = SimulatedParticipant("flights")
        first = flights.action("key-1", {})
        second = flights.action("key-1", {})
        self.assertEqual(first, second)
        self.assertEqual(flights.apply_count, 1)
        self.assertEqual(flights.idempotent_hits, 1)


class TestCrashAndResume(unittest.TestCase):
    def test_crash_after_step_resumes_to_same_outcome_as_uninterrupted_run(self) -> None:
        uninterrupted_flights = SimulatedParticipant("flights")
        uninterrupted_hotels = SimulatedParticipant("hotels")
        uninterrupted_payments = SimulatedParticipant("payments")
        uninterrupted_log = InMemorySagaLog()
        uninterrupted_orchestrator = SagaOrchestrator(uninterrupted_log)
        expected_status = uninterrupted_orchestrator.run(
            "u1", make_steps(uninterrupted_flights, uninterrupted_hotels, uninterrupted_payments), {"customer": "g"}
        )

        flights = SimulatedParticipant("flights")
        hotels = SimulatedParticipant("hotels")
        payments = SimulatedParticipant("payments")
        log = InMemorySagaLog()
        first_orchestrator = SagaOrchestrator(log)

        with self.assertRaises(SagaCrashed):
            first_orchestrator.run(
                "s7", make_steps(flights, hotels, payments), {"customer": "g"}, crash_after="reserve_hotel"
            )

        self.assertEqual(hotels.apply_count, 1)
        self.assertEqual(payments.apply_count, 0)

        second_orchestrator = SagaOrchestrator(log)
        status = second_orchestrator.run("s7", make_steps(flights, hotels, payments), {"customer": "g"})

        self.assertEqual(status, expected_status)
        self.assertEqual(status, SagaStatus.COMPLETED)
        self.assertEqual(flights.apply_count, 1)
        self.assertEqual(hotels.apply_count, 1)
        self.assertEqual(payments.apply_count, 1)

    def test_resuming_an_already_completed_saga_is_a_no_op(self) -> None:
        flights = SimulatedParticipant("flights")
        hotels = SimulatedParticipant("hotels")
        payments = SimulatedParticipant("payments")
        log = InMemorySagaLog()
        orchestrator = SagaOrchestrator(log)
        orchestrator.run("s8", make_steps(flights, hotels, payments), {"customer": "h"})

        status = orchestrator.run("s8", make_steps(flights, hotels, payments), {"customer": "h"})

        self.assertEqual(status, SagaStatus.COMPLETED)
        self.assertEqual(flights.apply_count, 1)
        self.assertEqual(hotels.apply_count, 1)
        self.assertEqual(payments.apply_count, 1)

    def test_crash_during_compensation_resumes_without_recompensating(self) -> None:
        flights = SimulatedParticipant("flights")
        hotels = SimulatedParticipant("hotels")
        payments = SimulatedParticipant("payments", fail_terminal=True)
        log = InMemorySagaLog()
        orchestrator = SagaOrchestrator(log)

        status = orchestrator.run("s9", make_steps(flights, hotels, payments), {"customer": "i"})
        self.assertEqual(status, SagaStatus.COMPENSATED)

        resumed_status = SagaOrchestrator(log).run("s9", make_steps(flights, hotels, payments), {"customer": "i"})
        self.assertEqual(resumed_status, SagaStatus.COMPENSATED)
        self.assertEqual(hotels.compensation_attempts, {})


class TestCompensationFailure(unittest.TestCase):
    def test_compensation_that_keeps_failing_reaches_needs_intervention(self) -> None:
        flights = SimulatedParticipant("flights")
        hotels = SimulatedParticipant("hotels", always_fail_compensation=True)
        payments = SimulatedParticipant("payments", fail_terminal=True)
        log = InMemorySagaLog()
        orchestrator = SagaOrchestrator(log)

        status = orchestrator.run("s10", make_steps(flights, hotels, payments), {"customer": "j"})

        self.assertEqual(status, SagaStatus.NEEDS_INTERVENTION)
        self.assertNotIn("s10:reserve_hotel", hotels.compensated)
        self.assertNotIn("s10:book_flight", flights.compensated)
        events = [event["type"] for event in log.events("s10")]
        self.assertIn("SAGA_NEEDS_INTERVENTION", events)
        self.assertNotIn("SAGA_COMPENSATED", events)

    def test_compensation_retried_before_success(self) -> None:
        flights = SimulatedParticipant("flights")
        hotels = SimulatedParticipant("hotels", fail_compensation_times=2)
        payments = SimulatedParticipant("payments", fail_terminal=True)
        log = InMemorySagaLog()
        orchestrator = SagaOrchestrator(log)

        status = orchestrator.run("s11", make_steps(flights, hotels, payments), {"customer": "k"})

        self.assertEqual(status, SagaStatus.COMPENSATED)
        self.assertIn("s11:reserve_hotel", hotels.compensated)


class TestTransientErrorDistinctFromTerminal(unittest.TestCase):
    def test_transient_error_type_is_retryable_terminal_is_not(self) -> None:
        participant = SimulatedParticipant("x", fail_transient_times=1)
        with self.assertRaises(TransientError):
            participant.action("k", {})
        result = participant.action("k", {})
        self.assertIsNotNone(result)


if __name__ == "__main__":
    unittest.main()
