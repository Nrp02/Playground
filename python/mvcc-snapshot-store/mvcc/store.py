from __future__ import annotations

import itertools
import threading
from dataclasses import dataclass, field
from typing import Any, Dict, Hashable, List, Optional

from .errors import TransactionClosedError, WriteConflictError

_MISSING = object()


@dataclass
class Version:
    commit_ts: int
    value: Any
    deleted: bool


@dataclass
class WriteEntry:
    value: Any
    deleted: bool


class TransactionStatus:
    ACTIVE = "ACTIVE"
    COMMITTED = "COMMITTED"
    ABORTED = "ABORTED"


@dataclass
class Transaction:
    txn_id: int
    snapshot_ts: int
    store: "MVCCStore"
    status: str = TransactionStatus.ACTIVE
    write_set: Dict[Hashable, WriteEntry] = field(default_factory=dict)

    def get(self, key: Hashable) -> Optional[Any]:
        self._require_active()
        return self.store._get(self, key)

    def put(self, key: Hashable, value: Any) -> None:
        self._require_active()
        self.write_set[key] = WriteEntry(value=value, deleted=False)

    def delete(self, key: Hashable) -> None:
        self._require_active()
        self.write_set[key] = WriteEntry(value=None, deleted=True)

    def commit(self) -> None:
        self._require_active()
        self.store._commit(self)

    def abort(self) -> None:
        self._require_active()
        self.store._abort(self)

    def _require_active(self) -> None:
        if self.status != TransactionStatus.ACTIVE:
            raise TransactionClosedError(self.txn_id)


class MVCCStore:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._sequence = itertools.count(1)
        self._committed_up_to = 0
        self._versions: Dict[Hashable, List[Version]] = {}
        self._active_transactions: Dict[int, Transaction] = {}

    def begin_transaction(self) -> Transaction:
        with self._lock:
            txn_id = next(self._sequence)
            snapshot_ts = self._committed_up_to
            txn = Transaction(txn_id=txn_id, snapshot_ts=snapshot_ts, store=self)
            self._active_transactions[txn_id] = txn
            return txn

    def get(self, txn: Transaction, key: Hashable) -> Optional[Any]:
        return txn.get(key)

    def put(self, txn: Transaction, key: Hashable, value: Any) -> None:
        txn.put(key, value)

    def commit(self, txn: Transaction) -> None:
        txn.commit()

    def abort(self, txn: Transaction) -> None:
        txn.abort()

    def _get(self, txn: Transaction, key: Hashable) -> Optional[Any]:
        entry = txn.write_set.get(key, _MISSING)
        if entry is not _MISSING:
            return None if entry.deleted else entry.value
        with self._lock:
            versions = self._versions.get(key, [])
            visible = None
            for version in versions:
                if version.commit_ts <= txn.snapshot_ts:
                    visible = version
                else:
                    break
            if visible is None or visible.deleted:
                return None
            return visible.value

    def _commit(self, txn: Transaction) -> None:
        with self._lock:
            for key in txn.write_set:
                versions = self._versions.get(key, [])
                for version in versions:
                    if version.commit_ts > txn.snapshot_ts:
                        self._active_transactions.pop(txn.txn_id, None)
                        txn.status = TransactionStatus.ABORTED
                        raise WriteConflictError(key)
            commit_ts = next(self._sequence)
            for key, entry in txn.write_set.items():
                bucket = self._versions.setdefault(key, [])
                bucket.append(Version(commit_ts=commit_ts, value=entry.value, deleted=entry.deleted))
            self._committed_up_to = commit_ts
            self._active_transactions.pop(txn.txn_id, None)
            txn.status = TransactionStatus.COMMITTED

    def _abort(self, txn: Transaction) -> None:
        with self._lock:
            txn.write_set.clear()
            self._active_transactions.pop(txn.txn_id, None)
            txn.status = TransactionStatus.ABORTED

    def active_snapshot_floor(self) -> int:
        with self._lock:
            if not self._active_transactions:
                return self._committed_up_to
            return min(t.snapshot_ts for t in self._active_transactions.values())

    def garbage_collect(self) -> int:
        with self._lock:
            if self._active_transactions:
                floor = min(t.snapshot_ts for t in self._active_transactions.values())
            else:
                floor = self._committed_up_to
            removed = 0
            for key, versions in list(self._versions.items()):
                versions.sort(key=lambda v: v.commit_ts)
                newest_below_floor: Optional[Version] = None
                kept: List[Version] = []
                for version in versions:
                    if version.commit_ts <= floor:
                        if newest_below_floor is not None:
                            removed += 1
                        newest_below_floor = version
                    else:
                        kept.append(version)
                if newest_below_floor is not None:
                    kept.insert(0, newest_below_floor)
                self._versions[key] = kept
            return removed

    def version_count(self, key: Hashable) -> int:
        with self._lock:
            return len(self._versions.get(key, []))
