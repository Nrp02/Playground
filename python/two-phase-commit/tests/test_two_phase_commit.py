import unittest

from twopc.coordinator import Coordinator
from twopc.log import DecisionLog, DuplicateDecisionError
from twopc.messages import Decision, Vote
from twopc.participant import Participant, ParticipantState, UnknownTransactionError


class TestAllCommit(unittest.TestCase):
    def test_all_participants_commit(self) -> None:
        participants = [Participant("a"), Participant("b"), Participant("c")]
        coordinator = Coordinator(participants)
        result = coordinator.run_transaction("txn-1")
        self.assertEqual(result.decision, Decision.COMMIT)
        for participant in participants:
            self.assertEqual(participant.state, ParticipantState.COMMITTED)
            self.assertEqual(participant.outcome_of("txn-1"), Decision.COMMIT)
        self.assertTrue(all(vote == Vote.COMMIT for vote in result.votes.values()))
        self.assertTrue(all(result.delivered.values()))

    def test_decision_log_records_commit(self) -> None:
        participants = [Participant("a"), Participant("b")]
        coordinator = Coordinator(participants)
        coordinator.run_transaction("txn-1")
        self.assertEqual(coordinator.decision_log.get("txn-1"), Decision.COMMIT)
        self.assertEqual(coordinator.outcome("txn-1"), Decision.COMMIT)


class TestAbort(unittest.TestCase):
    def test_single_no_vote_aborts_transaction(self) -> None:
        participants = [
            Participant("a"),
            Participant("b", vote_commit=False),
            Participant("c"),
        ]
        coordinator = Coordinator(participants)
        result = coordinator.run_transaction("txn-2")
        self.assertEqual(result.decision, Decision.ABORT)
        for participant in participants:
            self.assertEqual(participant.state, ParticipantState.ABORTED)
        self.assertEqual(coordinator.decision_log.get("txn-2"), Decision.ABORT)

    def test_no_voter_records_abort_immediately(self) -> None:
        no_voter = Participant("b", vote_commit=False)
        coordinator = Coordinator([Participant("a"), no_voter])
        result = coordinator.run_transaction("txn-2")
        self.assertEqual(result.votes["b"], Vote.ABORT)
        self.assertEqual(no_voter.state, ParticipantState.ABORTED)
        self.assertEqual(result.decision, Decision.ABORT)

    def test_multiple_no_votes_still_aborts(self) -> None:
        participants = [
            Participant("a", vote_commit=False),
            Participant("b", vote_commit=False),
            Participant("c"),
        ]
        coordinator = Coordinator(participants)
        result = coordinator.run_transaction("txn-2")
        self.assertEqual(result.decision, Decision.ABORT)


class TestCrashRecovery(unittest.TestCase):
    def test_crashed_participant_does_not_receive_decision(self) -> None:
        crasher = Participant("c", crash_after_vote=True)
        coordinator = Coordinator([Participant("a"), Participant("b"), crasher])
        result = coordinator.run_transaction("txn-3")
        self.assertEqual(result.decision, Decision.COMMIT)
        self.assertTrue(crasher.crashed)
        self.assertEqual(crasher.state, ParticipantState.CRASHED)
        self.assertFalse(result.delivered["c"])
        self.assertIsNone(crasher.outcome_of("txn-3"))

    def test_recovery_asks_coordinator_for_outcome(self) -> None:
        crasher = Participant("c", crash_after_vote=True)
        coordinator = Coordinator([Participant("a"), Participant("b"), crasher])
        coordinator.run_transaction("txn-3")

        decision = crasher.recover(coordinator, "txn-3")

        self.assertEqual(decision, Decision.COMMIT)
        self.assertFalse(crasher.crashed)
        self.assertEqual(crasher.state, ParticipantState.COMMITTED)
        self.assertEqual(crasher.outcome_of("txn-3"), Decision.COMMIT)
        self.assertEqual(crasher.recoveries, 1)

    def test_recovery_matches_decision_log_as_source_of_truth(self) -> None:
        crasher = Participant("c", crash_after_vote=True)
        coordinator = Coordinator([Participant("a"), Participant("b"), crasher])
        coordinator.run_transaction("txn-3")

        recovered = crasher.recover(coordinator, "txn-3")

        self.assertEqual(recovered, coordinator.decision_log.get("txn-3"))
        self.assertEqual(crasher.outcome_of("txn-3"), coordinator.decision_log.get("txn-3"))

    def test_recovery_for_unknown_transaction_raises(self) -> None:
        crasher = Participant("c", crash_after_vote=True)
        coordinator = Coordinator([Participant("a"), Participant("b"), crasher])
        with self.assertRaises(UnknownTransactionError):
            crasher.recover(coordinator, "txn-does-not-exist")

    def test_recovery_after_abort_decision(self) -> None:
        crasher = Participant("c", crash_after_vote=True)
        no_voter = Participant("b", vote_commit=False)
        coordinator = Coordinator([Participant("a"), no_voter, crasher])
        result = coordinator.run_transaction("txn-4")
        self.assertEqual(result.decision, Decision.ABORT)

        recovered = crasher.recover(coordinator, "txn-4")
        self.assertEqual(recovered, Decision.ABORT)
        self.assertEqual(crasher.state, ParticipantState.ABORTED)


class TestDecisionLog(unittest.TestCase):
    def test_record_and_get(self) -> None:
        log = DecisionLog()
        log.record("txn-1", Decision.COMMIT)
        self.assertEqual(log.get("txn-1"), Decision.COMMIT)
        self.assertTrue(log.has("txn-1"))

    def test_get_missing_returns_none(self) -> None:
        log = DecisionLog()
        self.assertIsNone(log.get("missing"))
        self.assertFalse(log.has("missing"))

    def test_recording_same_decision_twice_is_idempotent(self) -> None:
        log = DecisionLog()
        log.record("txn-1", Decision.COMMIT)
        log.record("txn-1", Decision.COMMIT)
        self.assertEqual(log.get("txn-1"), Decision.COMMIT)

    def test_conflicting_decision_raises(self) -> None:
        log = DecisionLog()
        log.record("txn-1", Decision.COMMIT)
        with self.assertRaises(DuplicateDecisionError):
            log.record("txn-1", Decision.ABORT)

    def test_transactions_returns_snapshot(self) -> None:
        log = DecisionLog()
        log.record("txn-1", Decision.COMMIT)
        log.record("txn-2", Decision.ABORT)
        snapshot = log.transactions()
        self.assertEqual(snapshot, {"txn-1": Decision.COMMIT, "txn-2": Decision.ABORT})
        snapshot["txn-3"] = Decision.COMMIT
        self.assertFalse(log.has("txn-3"))


class TestMultipleTransactions(unittest.TestCase):
    def test_coordinator_handles_sequential_transactions(self) -> None:
        participants = [Participant("a"), Participant("b")]
        coordinator = Coordinator(participants)

        first = coordinator.run_transaction("txn-1")
        self.assertEqual(first.decision, Decision.COMMIT)

        participants[0].vote_commit = False
        second = coordinator.run_transaction("txn-2")
        self.assertEqual(second.decision, Decision.ABORT)

        self.assertEqual(coordinator.decision_log.get("txn-1"), Decision.COMMIT)
        self.assertEqual(coordinator.decision_log.get("txn-2"), Decision.ABORT)
        self.assertEqual(len(coordinator.history), 2)


if __name__ == "__main__":
    unittest.main()
