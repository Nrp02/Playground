from __future__ import annotations

import random
from dataclasses import dataclass
from enum import IntEnum
from typing import Dict, List, Optional, Set, Tuple

PROBE_INTERVAL = 10
PROBE_TIMEOUT = 4
INDIRECT_TIMEOUT = 8
SUSPECT_TIMEOUT = 30
INDIRECT_HELPER_COUNT = 3


class MemberState(IntEnum):
    ALIVE = 0
    SUSPECT = 1
    FAILED = 2


GossipEntry = Tuple[int, MemberState, int]


@dataclass
class MemberInfo:
    state: MemberState
    incarnation: int


@dataclass
class PendingProbe:
    target: int
    direct_deadline: int
    escalated: bool = False
    indirect_deadline: Optional[int] = None


@dataclass
class Ping:
    src: int
    gossip: List[GossipEntry]
    relay_for: Optional[int] = None


@dataclass
class Ack:
    src: int
    gossip: List[GossipEntry]
    relay_for: Optional[int] = None


@dataclass
class PingReq:
    src: int
    target: int
    gossip: List[GossipEntry]


@dataclass
class PingReqAck:
    src: int
    about: int
    gossip: List[GossipEntry]


class SwimNode:
    def __init__(self, node_id: int, peer_ids: List[int], seed: int):
        self.id = node_id
        self.rng = random.Random(seed)
        self.incarnation = 0
        self.membership: Dict[int, MemberInfo] = {node_id: MemberInfo(MemberState.ALIVE, 0)}
        for peer in peer_ids:
            self.membership[peer] = MemberInfo(MemberState.ALIVE, 0)
        self.suspicion_started: Dict[int, int] = {}
        self.next_probe_time = self.rng.randint(0, PROBE_INTERVAL - 1)
        self.pending: Optional[PendingProbe] = None

    def state_of(self, node_id: int) -> Optional[MemberState]:
        info = self.membership.get(node_id)
        return info.state if info is not None else None

    def alive_peers(self) -> List[int]:
        return [
            nid
            for nid, info in self.membership.items()
            if nid != self.id and info.state == MemberState.ALIVE
        ]

    def rejoin(self, now: int) -> None:
        self.incarnation += 1
        self.membership[self.id] = MemberInfo(MemberState.ALIVE, self.incarnation)
        self.pending = None
        self.next_probe_time = now

    def tick(self, now: int, bus: "MessageBus") -> None:
        self._check_suspicion_timeouts(now)
        if self.pending is not None:
            self._check_pending(now, bus)
        if self.pending is None and now >= self.next_probe_time:
            self._start_probe(now, bus)

    def handle_message(self, now: int, bus: "MessageBus", message: object) -> None:
        if isinstance(message, Ping):
            self._merge_all(message.gossip, now)
            bus.send(
                self.id,
                message.src,
                Ack(src=self.id, gossip=self._gossip_snapshot(), relay_for=message.relay_for),
                now,
            )
        elif isinstance(message, Ack):
            self._merge_all(message.gossip, now)
            self._handle_ack(now, bus, message)
        elif isinstance(message, PingReq):
            self._merge_all(message.gossip, now)
            bus.send(
                self.id,
                message.target,
                Ping(src=self.id, gossip=self._gossip_snapshot(), relay_for=message.src),
                now,
            )
        elif isinstance(message, PingReqAck):
            self._merge_all(message.gossip, now)
            self._handle_ping_req_ack(now, message)

    def _handle_ack(self, now: int, bus: "MessageBus", message: Ack) -> None:
        if message.relay_for is None:
            if self.pending is not None and self.pending.target == message.src:
                self._mark_alive_confirmed(message.src)
                self.pending = None
        else:
            bus.send(
                self.id,
                message.relay_for,
                PingReqAck(src=self.id, about=message.src, gossip=self._gossip_snapshot()),
                now,
            )

    def _handle_ping_req_ack(self, now: int, message: PingReqAck) -> None:
        if self.pending is None:
            return
        if self.pending.target == message.about and self.pending.escalated:
            self._mark_alive_confirmed(message.about)
            self.pending = None

    def _mark_alive_confirmed(self, node_id: int) -> None:
        info = self.membership.get(node_id)
        incarnation = info.incarnation if info is not None else 0
        self.membership[node_id] = MemberInfo(MemberState.ALIVE, incarnation)
        self.suspicion_started.pop(node_id, None)

    def _check_suspicion_timeouts(self, now: int) -> None:
        for node_id, info in list(self.membership.items()):
            if info.state != MemberState.SUSPECT:
                continue
            started = self.suspicion_started.get(node_id, now)
            if now - started >= SUSPECT_TIMEOUT:
                self.membership[node_id] = MemberInfo(MemberState.FAILED, info.incarnation)
                self.suspicion_started.pop(node_id, None)

    def _candidates(self) -> List[int]:
        return [
            nid
            for nid, info in self.membership.items()
            if nid != self.id and info.state != MemberState.FAILED
        ]

    def _start_probe(self, now: int, bus: "MessageBus") -> None:
        candidates = self._candidates()
        self.next_probe_time = now + PROBE_INTERVAL
        if not candidates:
            return
        target = self.rng.choice(candidates)
        self.pending = PendingProbe(target=target, direct_deadline=now + PROBE_TIMEOUT)
        bus.send(self.id, target, Ping(src=self.id, gossip=self._gossip_snapshot()), now)

    def _check_pending(self, now: int, bus: "MessageBus") -> None:
        pending = self.pending
        if pending is None:
            return
        if not pending.escalated and now >= pending.direct_deadline:
            self._escalate_to_indirect(now, bus, pending)
            return
        if pending.escalated and pending.indirect_deadline is not None and now >= pending.indirect_deadline:
            self._suspect(pending.target, now)
            self.pending = None

    def _escalate_to_indirect(self, now: int, bus: "MessageBus", pending: PendingProbe) -> None:
        helpers = [nid for nid in self._candidates() if nid != pending.target]
        self.rng.shuffle(helpers)
        chosen = helpers[:INDIRECT_HELPER_COUNT]
        if not chosen:
            self._suspect(pending.target, now)
            self.pending = None
            return
        for helper in chosen:
            bus.send(
                self.id,
                helper,
                PingReq(src=self.id, target=pending.target, gossip=self._gossip_snapshot()),
                now,
            )
        pending.escalated = True
        pending.indirect_deadline = now + INDIRECT_TIMEOUT

    def _suspect(self, target: int, now: int) -> None:
        info = self.membership.get(target)
        if info is None or info.state == MemberState.FAILED:
            return
        if info.state == MemberState.SUSPECT:
            return
        self.membership[target] = MemberInfo(MemberState.SUSPECT, info.incarnation)
        self.suspicion_started[target] = now

    def _gossip_snapshot(self) -> List[GossipEntry]:
        return [(nid, info.state, info.incarnation) for nid, info in self.membership.items()]

    def _merge_all(self, entries: List[GossipEntry], now: int) -> None:
        for node_id, state, incarnation in entries:
            self._merge_one(node_id, state, incarnation, now)

    def _merge_one(self, node_id: int, state: MemberState, incarnation: int, now: int) -> None:
        if node_id == self.id:
            if state != MemberState.ALIVE and incarnation >= self.incarnation:
                self.incarnation = incarnation + 1
                self.membership[self.id] = MemberInfo(MemberState.ALIVE, self.incarnation)
            return

        local = self.membership.get(node_id)
        if local is None:
            self.membership[node_id] = MemberInfo(state, incarnation)
            if state == MemberState.SUSPECT:
                self.suspicion_started[node_id] = now
            return

        upgrade = incarnation > local.incarnation or (
            incarnation == local.incarnation and state > local.state
        )
        if not upgrade:
            return

        self.membership[node_id] = MemberInfo(state, incarnation)
        if state == MemberState.SUSPECT:
            self.suspicion_started[node_id] = now
        else:
            self.suspicion_started.pop(node_id, None)
