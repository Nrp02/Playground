from __future__ import annotations

import random
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Any, Dict, List, Optional

ELECTION_TIMEOUT_RANGE = (150, 300)
HEARTBEAT_INTERVAL = 50


class Role(Enum):
    FOLLOWER = auto()
    CANDIDATE = auto()
    LEADER = auto()


@dataclass
class LogEntry:
    term: int
    command: Any


@dataclass
class RequestVote:
    src: int
    term: int
    candidate_id: int
    last_log_index: int
    last_log_term: int


@dataclass
class RequestVoteReply:
    src: int
    term: int
    voter_id: int
    vote_granted: bool


@dataclass
class AppendEntries:
    src: int
    term: int
    leader_id: int
    prev_log_index: int
    prev_log_term: int
    entries: List[LogEntry]
    leader_commit: int


@dataclass
class AppendEntriesReply:
    src: int
    term: int
    follower_id: int
    success: bool
    match_index: int


class RaftNode:
    def __init__(self, node_id: int, peer_ids: List[int], seed: int):
        self.id = node_id
        self.peer_ids = peer_ids
        self.rng = random.Random(seed)

        self.current_term = 0
        self.voted_for: Optional[int] = None
        self.log: List[LogEntry] = []
        self.commit_index = -1
        self.last_applied = -1

        self.role = Role.FOLLOWER
        self.leader_id: Optional[int] = None
        self.votes_received: set = set()

        self.next_index: Dict[int, int] = {}
        self.match_index: Dict[int, int] = {}

        self.election_deadline = 0
        self.heartbeat_deadline = 0
        self._reset_election_deadline(0)

    def _reset_election_deadline(self, now: int) -> None:
        timeout = self.rng.randint(*ELECTION_TIMEOUT_RANGE)
        self.election_deadline = now + timeout

    def last_log_index(self) -> int:
        return len(self.log) - 1

    def last_log_term(self) -> int:
        return self.log[-1].term if self.log else 0

    def is_log_up_to_date(self, candidate_last_term: int, candidate_last_index: int) -> bool:
        my_term = self.last_log_term()
        if candidate_last_term != my_term:
            return candidate_last_term > my_term
        return candidate_last_index >= self.last_log_index()

    def step_down(self, term: int) -> None:
        self.current_term = term
        self.role = Role.FOLLOWER
        self.voted_for = None
        self.leader_id = None
        self.votes_received = set()

    def tick(self, now: int, bus: "MessageBus") -> None:
        if self.role in (Role.FOLLOWER, Role.CANDIDATE) and now >= self.election_deadline:
            self._start_election(now, bus)
        elif self.role == Role.LEADER and now >= self.heartbeat_deadline:
            self._send_append_entries_to_all(now, bus)
            self.heartbeat_deadline = now + HEARTBEAT_INTERVAL

    def _start_election(self, now: int, bus: "MessageBus") -> None:
        self.current_term += 1
        self.role = Role.CANDIDATE
        self.voted_for = self.id
        self.votes_received = {self.id}
        self.leader_id = None
        self._reset_election_deadline(now)

        for peer in self.peer_ids:
            bus.send(
                self.id,
                peer,
                RequestVote(
                    src=self.id,
                    term=self.current_term,
                    candidate_id=self.id,
                    last_log_index=self.last_log_index(),
                    last_log_term=self.last_log_term(),
                ),
                now,
            )

        if len(self.votes_received) * 2 > len(self.peer_ids) + 1:
            self._become_leader(now, bus)

    def _become_leader(self, now: int, bus: "MessageBus") -> None:
        self.role = Role.LEADER
        self.leader_id = self.id
        for peer in self.peer_ids:
            self.next_index[peer] = len(self.log)
            self.match_index[peer] = -1
        self.heartbeat_deadline = now
        self._send_append_entries_to_all(now, bus)

    def _send_append_entries_to_all(self, now: int, bus: "MessageBus") -> None:
        for peer in self.peer_ids:
            prev_index = self.next_index.get(peer, len(self.log)) - 1
            prev_term = self.log[prev_index].term if prev_index >= 0 else 0
            entries = self.log[prev_index + 1:]
            bus.send(
                self.id,
                peer,
                AppendEntries(
                    src=self.id,
                    term=self.current_term,
                    leader_id=self.id,
                    prev_log_index=prev_index,
                    prev_log_term=prev_term,
                    entries=list(entries),
                    leader_commit=self.commit_index,
                ),
                now,
            )

    def submit(self, command: Any) -> bool:
        if self.role != Role.LEADER:
            return False
        self.log.append(LogEntry(term=self.current_term, command=command))
        return True

    def handle_message(self, now: int, bus: "MessageBus", message: Any) -> None:
        if isinstance(message, RequestVote):
            self._handle_request_vote(now, bus, message)
        elif isinstance(message, RequestVoteReply):
            self._handle_request_vote_reply(now, bus, message)
        elif isinstance(message, AppendEntries):
            self._handle_append_entries(now, bus, message)
        elif isinstance(message, AppendEntriesReply):
            self._handle_append_entries_reply(now, bus, message)

    def _handle_request_vote(self, now: int, bus: "MessageBus", msg: RequestVote) -> None:
        if msg.term > self.current_term:
            self.step_down(msg.term)

        grant = False
        if msg.term == self.current_term and self.voted_for in (None, msg.candidate_id):
            if self.is_log_up_to_date(msg.last_log_term, msg.last_log_index):
                grant = True
                self.voted_for = msg.candidate_id
                self._reset_election_deadline(now)

        bus.send(
            self.id,
            msg.src,
            RequestVoteReply(src=self.id, term=self.current_term, voter_id=self.id, vote_granted=grant),
            now,
        )

    def _handle_request_vote_reply(self, now: int, bus: "MessageBus", msg: RequestVoteReply) -> None:
        if msg.term > self.current_term:
            self.step_down(msg.term)
            return
        if self.role != Role.CANDIDATE or msg.term != self.current_term:
            return
        if msg.vote_granted:
            self.votes_received.add(msg.voter_id)
            if len(self.votes_received) * 2 > len(self.peer_ids) + 1:
                self._become_leader(now, bus)

    def _handle_append_entries(self, now: int, bus: "MessageBus", msg: AppendEntries) -> None:
        if msg.term > self.current_term:
            self.step_down(msg.term)

        if msg.term < self.current_term:
            bus.send(self.id, msg.src, AppendEntriesReply(
                src=self.id, term=self.current_term, follower_id=self.id, success=False, match_index=-1), now)
            return

        self.role = Role.FOLLOWER
        self.leader_id = msg.leader_id
        self._reset_election_deadline(now)

        log_ok = msg.prev_log_index == -1 or (
            msg.prev_log_index < len(self.log) and self.log[msg.prev_log_index].term == msg.prev_log_term
        )
        if not log_ok:
            bus.send(self.id, msg.src, AppendEntriesReply(
                src=self.id, term=self.current_term, follower_id=self.id, success=False, match_index=-1), now)
            return

        insert_at = msg.prev_log_index + 1
        for offset, entry in enumerate(msg.entries):
            idx = insert_at + offset
            if idx < len(self.log) and self.log[idx].term != entry.term:
                self.log = self.log[:idx]
            if idx >= len(self.log):
                self.log.append(entry)

        if msg.leader_commit > self.commit_index:
            self.commit_index = min(msg.leader_commit, len(self.log) - 1)

        match_index = insert_at + len(msg.entries) - 1
        bus.send(self.id, msg.src, AppendEntriesReply(
            src=self.id, term=self.current_term, follower_id=self.id, success=True, match_index=match_index), now)

    def _handle_append_entries_reply(self, now: int, bus: "MessageBus", msg: AppendEntriesReply) -> None:
        if msg.term > self.current_term:
            self.step_down(msg.term)
            return
        if self.role != Role.LEADER or msg.term != self.current_term:
            return

        if not msg.success:
            self.next_index[msg.follower_id] = max(0, self.next_index.get(msg.follower_id, 1) - 1)
            return

        self.match_index[msg.follower_id] = max(self.match_index.get(msg.follower_id, -1), msg.match_index)
        self.next_index[msg.follower_id] = self.match_index[msg.follower_id] + 1

        for candidate_index in range(len(self.log) - 1, self.commit_index, -1):
            if self.log[candidate_index].term != self.current_term:
                continue
            replicated = 1 + sum(1 for p in self.peer_ids if self.match_index.get(p, -1) >= candidate_index)
            if replicated * 2 > len(self.peer_ids) + 1:
                self.commit_index = candidate_index
                break
