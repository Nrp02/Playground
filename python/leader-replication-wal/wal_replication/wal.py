from __future__ import annotations

import hashlib
from dataclasses import dataclass
from typing import List, Optional


class ChecksumMismatchError(Exception):
    pass


class NonContiguousRecordError(Exception):
    pass


def _compute_checksum(lsn: int, op: str, key: str, value: Optional[str]) -> str:
    payload = f"{lsn}|{op}|{key}|{value}"
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


@dataclass
class WALRecord:
    lsn: int
    op: str
    key: str
    value: Optional[str]
    checksum: str

    def is_valid(self) -> bool:
        return self.checksum == _compute_checksum(self.lsn, self.op, self.key, self.value)


class WriteAheadLog:
    def __init__(self) -> None:
        self.records: List[WALRecord] = []

    def append(self, op: str, key: str, value: Optional[str]) -> WALRecord:
        lsn = len(self.records)
        checksum = _compute_checksum(lsn, op, key, value)
        record = WALRecord(lsn=lsn, op=op, key=key, value=value, checksum=checksum)
        self.records.append(record)
        return record

    def append_record(self, record: WALRecord) -> None:
        expected_lsn = len(self.records)
        if record.lsn != expected_lsn:
            raise NonContiguousRecordError(
                f"non-contiguous lsn: expected {expected_lsn}, got {record.lsn}"
            )
        if not record.is_valid():
            raise ChecksumMismatchError(f"checksum mismatch at lsn {record.lsn}")
        self.records.append(record)

    def next_offset(self) -> int:
        return len(self.records)

    def from_offset(self, offset: int) -> List[WALRecord]:
        return list(self.records[offset:])

    def get(self, lsn: int) -> WALRecord:
        return self.records[lsn]

    def corrupt(self, lsn: int, new_value: Optional[str]) -> None:
        record = self.records[lsn]
        record.value = new_value

    def truncate(self, keep: int) -> None:
        self.records = self.records[:keep]
