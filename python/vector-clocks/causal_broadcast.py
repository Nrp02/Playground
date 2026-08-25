from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List

from vector_clock import VectorClock


@dataclass(frozen=True)
class Message:
    sender: str
    vector: Dict[str, int]
    payload: Any


class CausalNode:
    def __init__(self, node_id: str, node_ids: List[str]) -> None:
        self.node_id = node_id
        self.node_ids = list(node_ids)
        self.clock = VectorClock(node_ids)
        self.pending: List[Message] = []
        self.delivered: List[Message] = []
        self.delivered_payloads: List[Any] = []

    def local_event(self) -> Dict[str, int]:
        return self.clock.increment(self.node_id)

    def send(self, payload: Any) -> Message:
        vector = self.clock.increment(self.node_id)
        return Message(sender=self.node_id, vector=vector, payload=payload)

    def receive(self, message: Message) -> None:
        self.pending.append(message)
        self._drain()

    def _is_deliverable(self, message: Message) -> bool:
        current = self.clock.snapshot()
        for node_id in self.node_ids:
            expected = current.get(node_id, 0)
            incoming = message.vector.get(node_id, 0)
            if node_id == message.sender:
                if incoming != expected + 1:
                    return False
            else:
                if incoming > expected:
                    return False
        return True

    def _drain(self) -> None:
        progressed = True
        while progressed:
            progressed = False
            for message in list(self.pending):
                if self._is_deliverable(message):
                    self.pending.remove(message)
                    self.clock.merge(message.vector)
                    self.delivered.append(message)
                    self.delivered_payloads.append(message.payload)
                    progressed = True
                    break

    def pending_count(self) -> int:
        return len(self.pending)
