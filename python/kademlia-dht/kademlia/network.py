from __future__ import annotations

from typing import TYPE_CHECKING, Any, Dict, List, Set

from .routing import Contact

if TYPE_CHECKING:
    from .node import KademliaNode

PING = "PING"
STORE = "STORE"
FIND_NODE = "FIND_NODE"
FIND_VALUE = "FIND_VALUE"


class Timeout(Exception):
    pass


class Network:
    def __init__(self) -> None:
        self.nodes: Dict[int, "KademliaNode"] = {}
        self.online: Set[int] = set()
        self.clock = 0
        self.rpc_count = 0
        self.timeouts = 0
        self.rpc_counts: Dict[str, int] = {}
        self.eviction_depth = 0

    def now(self) -> int:
        self.clock += 1
        return self.clock

    def register(self, node: "KademliaNode") -> None:
        if node.id in self.nodes:
            raise ValueError("duplicate node id")
        self.nodes[node.id] = node
        self.online.add(node.id)

    def kill(self, node_id: int) -> None:
        self.online.discard(node_id)

    def revive(self, node_id: int) -> None:
        if node_id in self.nodes:
            self.online.add(node_id)

    def is_online(self, node_id: int) -> bool:
        return node_id in self.online

    def alive_nodes(self) -> List["KademliaNode"]:
        return [self.nodes[n] for n in sorted(self.online)]

    def reset_counters(self) -> None:
        self.rpc_count = 0
        self.timeouts = 0
        self.rpc_counts = {}

    def call(self, sender: Contact, dest_id: int, rpc: str, *args: Any) -> Any:
        self.rpc_count += 1
        self.rpc_counts[rpc] = self.rpc_counts.get(rpc, 0) + 1
        node = self.nodes.get(dest_id)
        if node is None or dest_id not in self.online:
            self.timeouts += 1
            raise Timeout("{} to {} timed out".format(rpc, dest_id))
        if rpc == PING:
            return node.rpc_ping(sender)
        if rpc == STORE:
            return node.rpc_store(sender, *args)
        if rpc == FIND_NODE:
            return node.rpc_find_node(sender, *args)
        if rpc == FIND_VALUE:
            return node.rpc_find_value(sender, *args)
        raise ValueError("unknown rpc {}".format(rpc))
