from enum import Enum, auto
from typing import Dict, Optional, TYPE_CHECKING

from twopc.messages import Decision, Vote

if TYPE_CHECKING:
    from twopc.coordinator import Coordinator


class ParticipantState(Enum):
    IDLE = auto()
    PREPARED = auto()
    COMMITTED = auto()
    ABORTED = auto()
    CRASHED = auto()


class UnknownTransactionError(Exception):
    pass


class Participant:
    def __init__(self, name: str, vote_commit: bool = True, crash_after_vote: bool = False) -> None:
        self.name = name
        self.vote_commit = vote_commit
        self.crash_after_vote = crash_after_vote
        self.state: ParticipantState = ParticipantState.IDLE
        self.crashed = False
        self._local_log: Dict[str, Decision] = {}
        self.recoveries = 0

    def prepare(self, txn_id: str) -> Vote:
        if not self.vote_commit:
            self.state = ParticipantState.ABORTED
            return Vote.ABORT
        self.state = ParticipantState.PREPARED
        if self.crash_after_vote:
            self.crashed = True
            self.state = ParticipantState.CRASHED
        return Vote.COMMIT

    def receive_decision(self, txn_id: str, decision: Decision) -> bool:
        if self.crashed:
            return False
        self._apply(txn_id, decision)
        return True

    def recover(self, coordinator: "Coordinator", txn_id: str) -> Decision:
        decision = coordinator.decision_log.get(txn_id)
        if decision is None:
            raise UnknownTransactionError(txn_id)
        self.crashed = False
        self._apply(txn_id, decision)
        self.recoveries += 1
        return decision

    def outcome_of(self, txn_id: str) -> Optional[Decision]:
        return self._local_log.get(txn_id)

    def _apply(self, txn_id: str, decision: Decision) -> None:
        self._local_log[txn_id] = decision
        self.state = ParticipantState.COMMITTED if decision == Decision.COMMIT else ParticipantState.ABORTED
