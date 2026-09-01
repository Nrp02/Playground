from saga.errors import SagaCrashed
from saga.log import InMemorySagaLog
from saga.orchestrator import SagaOrchestrator, SagaStatus
from saga.services import SimulatedParticipant
from saga.steps import Step


def build_steps(flights: SimulatedParticipant, hotels: SimulatedParticipant, payments: SimulatedParticipant) -> list[Step]:
    return [
        Step("book_flight", flights.action, flights.compensation),
        Step("reserve_hotel", hotels.action, hotels.compensation),
        Step("charge_payment", payments.action, payments.compensation),
    ]


def print_trace(log: InMemorySagaLog, saga_id: str) -> None:
    for event in log.events(saga_id):
        print(f"  {event}")


def run_full_success() -> None:
    print("=== scenario 1: full success, with a lossy ack proving idempotency ===")
    flights = SimulatedParticipant("flights", lose_response_after_apply=True)
    hotels = SimulatedParticipant("hotels")
    payments = SimulatedParticipant("payments")
    log = InMemorySagaLog()
    orchestrator = SagaOrchestrator(log)

    status = orchestrator.run("trip-1", build_steps(flights, hotels, payments), {"customer": "ada"})

    print(f"final status: {status.name}")
    print(f"flights.apply_count={flights.apply_count} flights.idempotent_hits={flights.idempotent_hits}")
    print_trace(log, "trip-1")
    assert status == SagaStatus.COMPLETED
    assert flights.apply_count == 1
    assert flights.idempotent_hits == 1


def run_compensation_on_failure() -> None:
    print("\n=== scenario 2: payment terminally fails, hotel and flight compensated in reverse order ===")
    flights = SimulatedParticipant("flights")
    hotels = SimulatedParticipant("hotels")
    payments = SimulatedParticipant("payments", fail_terminal=True)
    log = InMemorySagaLog()
    orchestrator = SagaOrchestrator(log)

    status = orchestrator.run("trip-2", build_steps(flights, hotels, payments), {"customer": "grace"})

    print(f"final status: {status.name}")
    print_trace(log, "trip-2")
    assert status == SagaStatus.COMPENSATED
    assert "trip-2:reserve_hotel" in hotels.compensated
    assert "trip-2:book_flight" in flights.compensated
    compensation_order = [
        event["step"] for event in log.events("trip-2") if event["type"] == "COMPENSATION_STARTED"
    ]
    assert compensation_order == ["reserve_hotel", "book_flight"]


def run_crash_and_resume() -> None:
    print("\n=== scenario 3: orchestrator crashes mid-saga, a fresh one resumes from the log ===")
    flights = SimulatedParticipant("flights")
    hotels = SimulatedParticipant("hotels")
    payments = SimulatedParticipant("payments")
    log = InMemorySagaLog()
    orchestrator_before_crash = SagaOrchestrator(log)

    try:
        orchestrator_before_crash.run(
            "trip-3", build_steps(flights, hotels, payments), {"customer": "linus"}, crash_after="reserve_hotel"
        )
        raise AssertionError("expected a simulated crash")
    except SagaCrashed:
        print("orchestrator process crashed after reserve_hotel committed")

    print(f"hotels.apply_count right after crash: {hotels.apply_count}")

    fresh_flights = flights
    fresh_hotels = hotels
    fresh_payments = payments
    orchestrator_after_restart = SagaOrchestrator(log)
    status = orchestrator_after_restart.run(
        "trip-3", build_steps(fresh_flights, fresh_hotels, fresh_payments), {"customer": "linus"}
    )

    print(f"final status: {status.name}")
    print(f"hotels.apply_count after resume: {hotels.apply_count}")
    print_trace(log, "trip-3")
    assert status == SagaStatus.COMPLETED
    assert hotels.apply_count == 1
    assert payments.apply_count == 1


def main() -> int:
    run_full_success()
    run_compensation_on_failure()
    run_crash_and_resume()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
