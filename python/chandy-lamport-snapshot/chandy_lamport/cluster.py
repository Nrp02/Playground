from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Dict, List, Optional, Tuple

from .bus import MessageBus
from .node import EventLogger, Node


@dataclass
class SnapshotResult:
    snapshot_id: int
    local_states: Dict[int, int]
    channel_states: Dict[Tuple[int, int], List[int]]

    def local_total(self) -> int:
        return sum(self.local_states.values())

    def channel_total(self) -> int:
        return sum(sum(messages) for messages in self.channel_states.values())

    def total(self) -> int:
        return self.local_total() + self.channel_total()


class Cluster:
    def __init__(
        self,
        node_ids: List[int],
        initial_balance: int = 100,
        seed: int = 0,
        min_delay: int = 1,
        max_delay: int = 3,
        on_event: Optional[Callable[[int, int, str], None]] = None,
    ) -> None:
        self.now = 0
        self.bus = MessageBus(min_delay=min_delay, max_delay=max_delay, seed=seed)
        self.on_event = on_event
        self.nodes: Dict[int, Node] = {
            node_id: Node(
                node_id,
                [n for n in node_ids if n != node_id],
                initial_balance,
                seed + node_id,
                logger=self._make_logger(),
            )
            for node_id in node_ids
        }
        self._next_snapshot_id = 0
        self.invariant = initial_balance * len(node_ids)

    def _make_logger(self) -> EventLogger:
        def emit(now: int, node_id: int, message: str) -> None:
            if self.on_event is not None:
                self.on_event(now, node_id, message)

        return emit

    def step(self) -> None:
        self.now += 1
        for src, dest, message in self.bus.deliver_ready(self.now):
            self.nodes[dest].handle_message(self.now, self.bus, src, message)
        for node in self.nodes.values():
            node.tick(self.now, self.bus)

    def run(self, ticks: int) -> None:
        for _ in range(ticks):
            self.step()

    def initiate_snapshot(self, node_id: int) -> int:
        snapshot_id = self._next_snapshot_id
        self._next_snapshot_id += 1
        self.nodes[node_id].initiate_snapshot(self.now, self.bus, snapshot_id)
        return snapshot_id

    def snapshot_complete(self, snapshot_id: int) -> bool:
        return all(node.is_done(snapshot_id) for node in self.nodes.values())

    def collect_snapshot(self, snapshot_id: int) -> SnapshotResult:
        local_states: Dict[int, int] = {}
        channel_states: Dict[Tuple[int, int], List[int]] = {}
        for node_id, node in self.nodes.items():
            balance, channel_messages = node.result(snapshot_id)
            local_states[node_id] = balance
            for src, messages in channel_messages.items():
                if messages:
                    channel_states[(src, node_id)] = messages
        return SnapshotResult(snapshot_id, local_states, channel_states)

    def current_balance_total(self) -> int:
        return sum(node.balance for node in self.nodes.values())

    def in_flight_total(self) -> int:
        return self.bus.pending_amount_sum()
