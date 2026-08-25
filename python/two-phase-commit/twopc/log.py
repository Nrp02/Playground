from typing import Dict, Optional

from twopc.messages import Decision


class DuplicateDecisionError(Exception):
    pass


class DecisionLog:
    def __init__(self) -> None:
        self._entries: Dict[str, Decision] = {}

    def record(self, txn_id: str, decision: Decision) -> None:
        existing = self._entries.get(txn_id)
        if existing is not None and existing != decision:
            raise DuplicateDecisionError(txn_id)
        self._entries[txn_id] = decision

    def get(self, txn_id: str) -> Optional[Decision]:
        return self._entries.get(txn_id)

    def has(self, txn_id: str) -> bool:
        return txn_id in self._entries

    def transactions(self) -> Dict[str, Decision]:
        return dict(self._entries)
