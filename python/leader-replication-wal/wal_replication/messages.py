from __future__ import annotations

from dataclasses import dataclass
from typing import List

from .wal import WALRecord


@dataclass
class Replicate:
    src: int
    entries: List[WALRecord]
    leader_commit_offset: int


@dataclass
class ReplicateAck:
    src: int
    follower_id: int
    applied_offset: int
    success: bool


@dataclass
class CatchupRequest:
    src: int
    from_offset: int


@dataclass
class CatchupResponse:
    src: int
    entries: List[WALRecord]
