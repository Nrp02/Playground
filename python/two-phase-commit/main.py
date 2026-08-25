from twopc.coordinator import Coordinator
from twopc.messages import Decision
from twopc.participant import Participant, ParticipantState


def describe(participants: list[Participant]) -> None:
    for participant in participants:
        print(f"  {participant.name}: state={participant.state.name} crashed={participant.crashed}")


def run_all_commit() -> None:
    print("=== scenario 1: all participants vote commit ===")
    participants = [
        Participant("inventory"),
        Participant("payments"),
        Participant("shipping"),
    ]
    coordinator = Coordinator(participants)
    result = coordinator.run_transaction("txn-1")
    print(f"decision: {result.decision.name}")
    describe(participants)
    assert result.decision == Decision.COMMIT
    assert all(p.state == ParticipantState.COMMITTED for p in participants)


def run_abort_on_one_no() -> None:
    print("\n=== scenario 2: one participant votes abort ===")
    participants = [
        Participant("inventory"),
        Participant("payments", vote_commit=False),
        Participant("shipping"),
    ]
    coordinator = Coordinator(participants)
    result = coordinator.run_transaction("txn-2")
    print(f"decision: {result.decision.name}")
    describe(participants)
    assert result.decision == Decision.ABORT
    assert participants[0].state == ParticipantState.ABORTED
    assert participants[2].state == ParticipantState.ABORTED


def run_crash_recovery() -> None:
    print("\n=== scenario 3: participant crashes after voting commit ===")
    shipping = Participant("shipping", crash_after_vote=True)
    participants = [
        Participant("inventory"),
        Participant("payments"),
        shipping,
    ]
    coordinator = Coordinator(participants)
    result = coordinator.run_transaction("txn-3")
    print(f"decision: {result.decision.name}")
    print(f"decision delivered to shipping: {result.delivered['shipping']}")
    describe(participants)
    assert result.decision == Decision.COMMIT
    assert shipping.state == ParticipantState.CRASHED
    assert result.delivered["shipping"] is False

    print("shipping restarts and asks the coordinator's decision log for the outcome")
    recovered_decision = shipping.recover(coordinator, "txn-3")
    print(f"recovered decision: {recovered_decision.name}")
    describe(participants)
    assert recovered_decision == Decision.COMMIT
    assert shipping.state == ParticipantState.COMMITTED
    assert shipping.crashed is False
    assert coordinator.decision_log.get("txn-3") == shipping.outcome_of("txn-3")


def main() -> int:
    run_all_commit()
    run_abort_on_one_no()
    run_crash_recovery()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
