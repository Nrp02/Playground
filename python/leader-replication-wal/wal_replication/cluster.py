from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple

from .node import CorruptionDetected, ReplicaNode, Role


class MessageBus:
    def __init__(self, delay: int = 5) -> None:
        self.delay = delay
        self.queue: List[Tuple[int, int, int, Any]] = []

    def send(self, src: int, dest: int, message: Any, now: int) -> None:
        self.queue.append((now + self.delay, src, dest, message))

    def deliver_ready(self, now: int) -> List[Tuple[int, Any]]:
        ready = [item for item in self.queue if item[0] <= now]
        self.queue = [item for item in self.queue if item[0] > now]
        return [(dest, message) for (_, _src, dest, message) in ready]


class ReplicationCluster:
    def __init__(self, node_ids: List[int], leader_id: int, delay: int = 5) -> None:
        self.now = 0
        self.bus = MessageBus(delay=delay)
        self.nodes: Dict[int, ReplicaNode] = {
            node_id: ReplicaNode(node_id, [n for n in node_ids if n != node_id])
            for node_id in node_ids
        }
        self.nodes[leader_id].become_leader([n for n in node_ids if n != leader_id])

    def step(self) -> None:
        self.now += 1
        for dest, message in self.bus.deliver_ready(self.now):
            node = self.nodes.get(dest)
            if node is None or not node.online:
                continue
            try:
                node.handle_message(self.now, self.bus, message)
            except CorruptionDetected:
                node.online = False
        for node in self.nodes.values():
            node.send_replication(self.now, self.bus)

    def run(self, ticks: int) -> None:
        for _ in range(ticks):
            self.step()

    def leader(self) -> Optional[ReplicaNode]:
        leaders = [n for n in self.nodes.values() if n.role == Role.LEADER and n.online]
        return leaders[0] if len(leaders) == 1 else None

    def submit(self, op: str, key: str, value: Optional[str] = None):
        leader = self.leader()
        if leader is None:
            return None
        return leader.submit(op, key, value)

    def take_offline(self, node_id: int) -> None:
        self.nodes[node_id].online = False

    def bring_online(self, node_id: int) -> None:
        self.nodes[node_id].online = True

    def promote(self, new_leader_id: int) -> ReplicaNode:
        old_leader = self.leader()
        if old_leader is not None and old_leader.id != new_leader_id:
            old_leader.role = Role.FOLLOWER
        remaining = [n for n in self.nodes if n != new_leader_id]
        new_leader = self.nodes[new_leader_id]
        new_leader.become_leader(remaining)
        return new_leader
