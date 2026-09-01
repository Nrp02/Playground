from __future__ import annotations

import random
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional, Set, Tuple

from .bus import MessageBus
from .messages import AppMessage, Marker

SEND_PROBABILITY = 0.35
MAX_TRANSFER = 10

EventLogger = Callable[[int, int, str], None]


@dataclass
class SnapshotState:
    local_balance: int
    pending_channels: Set[int]
    channel_messages: Dict[int, List[int]]


class Node:
    def __init__(
        self,
        node_id: int,
        peer_ids: List[int],
        initial_balance: int,
        seed: int,
        logger: Optional[EventLogger] = None,
    ) -> None:
        self.id = node_id
        self.peers = list(peer_ids)
        self.balance = initial_balance
        self.rng = random.Random(seed)
        self.snapshots: Dict[int, SnapshotState] = {}
        self.logger = logger

    def _log(self, now: int, message: str) -> None:
        if self.logger is not None:
            self.logger(now, self.id, message)

    def tick(self, now: int, bus: MessageBus) -> None:
        if not self.peers or self.balance <= 0:
            return
        if self.rng.random() >= SEND_PROBABILITY:
            return
        amount = self.rng.randint(1, min(self.balance, MAX_TRANSFER))
        dest = self.rng.choice(self.peers)
        self.balance -= amount
        bus.send(self.id, dest, AppMessage(self.id, dest, amount), now)

    def initiate_snapshot(self, now: int, bus: MessageBus, snapshot_id: int) -> None:
        if snapshot_id in self.snapshots:
            return
        self._start_snapshot(now, snapshot_id)
        self._broadcast_markers(now, bus, snapshot_id)

    def _start_snapshot(self, now: int, snapshot_id: int) -> None:
        self.snapshots[snapshot_id] = SnapshotState(
            local_balance=self.balance,
            pending_channels=set(self.peers),
            channel_messages={peer: [] for peer in self.peers},
        )
        self._log(now, f"records local state for snapshot {snapshot_id} (balance={self.balance})")

    def _broadcast_markers(self, now: int, bus: MessageBus, snapshot_id: int) -> None:
        for peer in self.peers:
            bus.send(self.id, peer, Marker(snapshot_id, self.id), now)
        self._log(now, f"sends markers on all outgoing channels for snapshot {snapshot_id}")

    def handle_message(self, now: int, bus: MessageBus, src: int, message: object) -> None:
        if isinstance(message, AppMessage):
            self._handle_app_message(src, message)
        elif isinstance(message, Marker):
            self._handle_marker(now, bus, src, message.snapshot_id)

    def _handle_app_message(self, src: int, message: AppMessage) -> None:
        self.balance += message.amount
        for state in self.snapshots.values():
            if src in state.pending_channels:
                state.channel_messages[src].append(message.amount)

    def _handle_marker(self, now: int, bus: MessageBus, src: int, snapshot_id: int) -> None:
        first = snapshot_id not in self.snapshots
        if first:
            self._start_snapshot(now, snapshot_id)
            self._broadcast_markers(now, bus, snapshot_id)
        state = self.snapshots[snapshot_id]
        state.pending_channels.discard(src)
        if first:
            self._log(now, f"receives first marker (from {src}) for snapshot {snapshot_id}, channel recorded empty")
        else:
            recorded = state.channel_messages[src]
            self._log(now, f"receives marker from {src} for snapshot {snapshot_id}, channel recorded {recorded}")
        if not state.pending_channels:
            self._log(now, f"has markers on all incoming channels, snapshot {snapshot_id} done locally")

    def is_done(self, snapshot_id: int) -> bool:
        state = self.snapshots.get(snapshot_id)
        return state is not None and not state.pending_channels

    def result(self, snapshot_id: int) -> Tuple[int, Dict[int, List[int]]]:
        state = self.snapshots[snapshot_id]
        return state.local_balance, {peer: list(messages) for peer, messages in state.channel_messages.items()}
