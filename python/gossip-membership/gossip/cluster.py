from __future__ import annotations

from typing import Any, Dict, FrozenSet, List, Set, Tuple

from .node import MemberState, SwimNode


class MessageBus:
    def __init__(self, delay: int = 1):
        self.delay = delay
        self.queue: List[Tuple[int, int, int, Any]] = []
        self.broken_links: Set[FrozenSet[int]] = set()

    def break_link(self, a: int, b: int) -> None:
        self.broken_links.add(frozenset((a, b)))

    def heal_link(self, a: int, b: int) -> None:
        self.broken_links.discard(frozenset((a, b)))

    def send(self, src: int, dest: int, message: Any, now: int) -> None:
        if frozenset((src, dest)) in self.broken_links:
            return
        self.queue.append((now + self.delay, src, dest, message))

    def deliver_ready(self, now: int) -> List[Tuple[int, Any]]:
        ready = [item for item in self.queue if item[0] <= now]
        self.queue = [item for item in self.queue if item[0] > now]
        return [(dest, message) for (_, _src, dest, message) in ready]


class Cluster:
    def __init__(self, node_ids: List[int], seed: int = 0, delay: int = 1):
        self.now = 0
        self.bus = MessageBus(delay=delay)
        self.nodes: Dict[int, SwimNode] = {
            node_id: SwimNode(node_id, [n for n in node_ids if n != node_id], seed + node_id)
            for node_id in node_ids
        }
        self.silenced: Set[int] = set()

    def step(self) -> None:
        self.now += 1
        for dest, message in self.bus.deliver_ready(self.now):
            if dest in self.silenced:
                continue
            if dest in self.nodes:
                self.nodes[dest].handle_message(self.now, self.bus, message)
        for node_id, node in self.nodes.items():
            if node_id in self.silenced:
                continue
            node.tick(self.now, self.bus)

    def run(self, ticks: int) -> None:
        for _ in range(ticks):
            self.step()

    def silence(self, node_id: int) -> None:
        self.silenced.add(node_id)

    def rejoin(self, node_id: int) -> None:
        self.silenced.discard(node_id)
        self.nodes[node_id].rejoin(self.now)

    def break_link(self, a: int, b: int) -> None:
        self.bus.break_link(a, b)

    def heal_link(self, a: int, b: int) -> None:
        self.bus.heal_link(a, b)

    def state_of(self, observer_id: int, target_id: int) -> MemberState:
        state = self.nodes[observer_id].state_of(target_id)
        assert state is not None
        return state

    def observers(self, exclude: int) -> List[int]:
        return [nid for nid in self.nodes if nid != exclude and nid not in self.silenced]

    def all_agree(self, target_id: int, expected_state: MemberState) -> bool:
        return all(
            self.state_of(observer, target_id) == expected_state
            for observer in self.observers(target_id)
        )

    def fully_converged(self) -> bool:
        node_ids = list(self.nodes)
        for target in node_ids:
            reference = None
            for observer in node_ids:
                if observer == target or observer in self.silenced:
                    continue
                state = self.state_of(observer, target)
                if reference is None:
                    reference = state
                elif state != reference:
                    return False
        return True
