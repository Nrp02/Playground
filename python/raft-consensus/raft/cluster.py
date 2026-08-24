from __future__ import annotations

from typing import Any, Dict, List, Optional, Set, Tuple

from .node import RaftNode, Role


class MessageBus:
    def __init__(self, delay: int = 5):
        self.delay = delay
        self.queue: List[Tuple[int, int, int, Any]] = []
        self.partitions: Optional[List[Set[int]]] = None

    def partition(self, groups: List[Set[int]]) -> None:
        self.partitions = groups

    def heal(self) -> None:
        self.partitions = None

    def _connected(self, a: int, b: int) -> bool:
        if self.partitions is None:
            return True
        for group in self.partitions:
            if a in group and b in group:
                return True
        return False

    def send(self, src: int, dest: int, message: Any, now: int) -> None:
        if not self._connected(src, dest):
            return
        self.queue.append((now + self.delay, src, dest, message))

    def deliver_ready(self, now: int) -> List[Tuple[int, Any]]:
        ready = [item for item in self.queue if item[0] <= now]
        self.queue = [item for item in self.queue if item[0] > now]
        return [(dest, message) for (_, _src, dest, message) in ready]


class Cluster:
    def __init__(self, node_ids: List[int], seed: int = 0, delay: int = 5):
        self.now = 0
        self.bus = MessageBus(delay=delay)
        self.nodes: Dict[int, RaftNode] = {
            node_id: RaftNode(node_id, [n for n in node_ids if n != node_id], seed + node_id)
            for node_id in node_ids
        }

    def step(self) -> None:
        self.now += 1
        for dest, message in self.bus.deliver_ready(self.now):
            if dest in self.nodes:
                self.nodes[dest].handle_message(self.now, self.bus, message)
        for node in self.nodes.values():
            node.tick(self.now, self.bus)

    def run(self, ticks: int) -> None:
        for _ in range(ticks):
            self.step()

    def leader(self) -> Optional[RaftNode]:
        leaders = [n for n in self.nodes.values() if n.role == Role.LEADER]
        return leaders[0] if len(leaders) == 1 else None

    def leaders(self) -> List[RaftNode]:
        return [n for n in self.nodes.values() if n.role == Role.LEADER]

    def submit(self, command: Any) -> bool:
        leader = self.leader()
        if leader is None:
            return False
        return leader.submit(command)

    def committed_log(self, node_id: int) -> List[Any]:
        node = self.nodes[node_id]
        return [entry.command for entry in node.log[: node.commit_index + 1]]

    def partition(self, groups: List[Set[int]]) -> None:
        self.bus.partition(groups)

    def heal_partition(self) -> None:
        self.bus.heal()
