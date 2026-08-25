from __future__ import annotations

from enum import Enum, auto
from typing import Dict, List, Optional

from .messages import CatchupRequest, CatchupResponse, Replicate, ReplicateAck
from .wal import WALRecord, WriteAheadLog


class Role(Enum):
    LEADER = auto()
    FOLLOWER = auto()


class CorruptionDetected(Exception):
    pass


class KVStateMachine:
    def __init__(self) -> None:
        self.data: Dict[str, str] = {}

    def apply(self, record: WALRecord) -> None:
        if record.op == "SET":
            self.data[record.key] = record.value
        elif record.op == "DELETE":
            self.data.pop(record.key, None)
        else:
            raise ValueError(f"unknown op: {record.op}")


class ReplicaNode:
    def __init__(self, node_id: int, peer_ids: List[int]) -> None:
        self.id = node_id
        self.peer_ids = list(peer_ids)
        self.role = Role.FOLLOWER
        self.online = True
        self.wal = WriteAheadLog()
        self.state_machine = KVStateMachine()
        self.applied_offset = -1
        self.corrupted_offsets: List[int] = []

        self.follower_next_offset: Dict[int, int] = {}
        self.follower_match_offset: Dict[int, int] = {}

    def become_leader(self, remaining_peer_ids: List[int]) -> None:
        self.role = Role.LEADER
        self.peer_ids = list(remaining_peer_ids)
        next_offset = self.wal.next_offset()
        self.follower_next_offset = {peer: next_offset for peer in self.peer_ids}
        self.follower_match_offset = {peer: -1 for peer in self.peer_ids}

    def submit(self, op: str, key: str, value: Optional[str]) -> Optional[WALRecord]:
        if self.role != Role.LEADER or not self.online:
            return None
        record = self.wal.append(op, key, value)
        self.state_machine.apply(record)
        self.applied_offset = record.lsn
        return record

    def send_replication(self, now: int, bus: "MessageBus") -> None:
        if self.role != Role.LEADER or not self.online:
            return
        for peer in self.peer_ids:
            next_offset = self.follower_next_offset.get(peer, self.wal.next_offset())
            entries = self.wal.from_offset(next_offset)
            if not entries:
                continue
            bus.send(
                self.id,
                peer,
                Replicate(src=self.id, entries=entries, leader_commit_offset=self.wal.next_offset() - 1),
                now,
            )

    def handle_message(self, now: int, bus: "MessageBus", message: object) -> None:
        if isinstance(message, Replicate):
            self._handle_replicate(now, bus, message)
        elif isinstance(message, ReplicateAck):
            self._handle_replicate_ack(message)
        elif isinstance(message, CatchupRequest):
            self._handle_catchup_request(now, bus, message)
        elif isinstance(message, CatchupResponse):
            self._handle_catchup_response(now, bus, message)

    def _apply_new_record(self, record: WALRecord) -> None:
        if not record.is_valid():
            self.corrupted_offsets.append(record.lsn)
            raise CorruptionDetected(f"node {self.id}: checksum mismatch at lsn {record.lsn}")
        self.wal.append_record(record)
        self.state_machine.apply(record)
        self.applied_offset = record.lsn

    def _handle_replicate(self, now: int, bus: "MessageBus", msg: Replicate) -> None:
        gap = False
        for record in msg.entries:
            if record.lsn < self.wal.next_offset():
                continue
            if record.lsn > self.wal.next_offset():
                gap = True
                break
            self._apply_new_record(record)
        if gap:
            bus.send(self.id, msg.src, CatchupRequest(src=self.id, from_offset=self.wal.next_offset()), now)
            return
        bus.send(
            self.id,
            msg.src,
            ReplicateAck(src=self.id, follower_id=self.id, applied_offset=self.applied_offset, success=True),
            now,
        )

    def _handle_replicate_ack(self, msg: ReplicateAck) -> None:
        if self.role != Role.LEADER:
            return
        self.follower_match_offset[msg.follower_id] = msg.applied_offset
        self.follower_next_offset[msg.follower_id] = msg.applied_offset + 1

    def _handle_catchup_request(self, now: int, bus: "MessageBus", msg: CatchupRequest) -> None:
        if self.role != Role.LEADER:
            return
        entries = self.wal.from_offset(msg.from_offset)
        bus.send(self.id, msg.src, CatchupResponse(src=self.id, entries=entries), now)

    def _handle_catchup_response(self, now: int, bus: "MessageBus", msg: CatchupResponse) -> None:
        for record in msg.entries:
            if record.lsn < self.wal.next_offset():
                continue
            self._apply_new_record(record)
        bus.send(
            self.id,
            msg.src,
            ReplicateAck(src=self.id, follower_id=self.id, applied_offset=self.applied_offset, success=True),
            now,
        )
