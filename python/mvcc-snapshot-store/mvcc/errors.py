from __future__ import annotations


class MVCCError(Exception):
    pass


class WriteConflictError(MVCCError):
    def __init__(self, key: object) -> None:
        super().__init__(f"write-write conflict on key {key!r}")
        self.key = key


class TransactionClosedError(MVCCError):
    def __init__(self, txn_id: int) -> None:
        super().__init__(f"transaction {txn_id} is no longer active")
        self.txn_id = txn_id
