from dataclasses import dataclass, field
from typing import Dict, List

from twopc.log import DecisionLog
from twopc.messages import Decision, Vote
from twopc.participant import Participant


@dataclass
class TransactionResult:
    txn_id: str
    decision: Decision
    votes: Dict[str, Vote] = field(default_factory=dict)
    delivered: Dict[str, bool] = field(default_factory=dict)


class Coordinator:
    def __init__(self, participants: List[Participant]) -> None:
        self.participants = participants
        self.decision_log = DecisionLog()
        self.history: List[TransactionResult] = []

    def run_transaction(self, txn_id: str) -> TransactionResult:
        votes: Dict[str, Vote] = {}
        for participant in self.participants:
            votes[participant.name] = participant.prepare(txn_id)

        decision = Decision.COMMIT if all(vote == Vote.COMMIT for vote in votes.values()) else Decision.ABORT
        self.decision_log.record(txn_id, decision)

        delivered: Dict[str, bool] = {}
        for participant in self.participants:
            delivered[participant.name] = participant.receive_decision(txn_id, decision)

        result = TransactionResult(txn_id=txn_id, decision=decision, votes=votes, delivered=delivered)
        self.history.append(result)
        return result

    def outcome(self, txn_id: str) -> Decision:
        decision = self.decision_log.get(txn_id)
        if decision is None:
            raise KeyError(txn_id)
        return decision
