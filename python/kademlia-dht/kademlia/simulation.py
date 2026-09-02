from __future__ import annotations

import random
from typing import List, Tuple

from .ids import DEFAULT_BITS, distance, random_id
from .network import Network
from .node import KademliaNode


def build_network(
    size: int,
    k: int = 20,
    alpha: int = 3,
    bits: int = DEFAULT_BITS,
    seed: int = 0,
) -> Tuple[Network, List[KademliaNode]]:
    rng = random.Random(seed)
    ids: List[int] = []
    seen = set()
    while len(ids) < size:
        candidate = random_id(rng, bits)
        if candidate in seen:
            continue
        seen.add(candidate)
        ids.append(candidate)

    network = Network()
    nodes: List[KademliaNode] = []
    for position, node_id in enumerate(ids):
        node = KademliaNode(
            node_id,
            network,
            k=k,
            alpha=alpha,
            bits=bits,
            address="n{:02d}".format(position),
        )
        network.register(node)
        if nodes:
            node.join(nodes[0].contact)
        nodes.append(node)
    return network, nodes


def brute_force_closest(node_ids: List[int], target: int, count: int) -> List[int]:
    return sorted(node_ids, key=lambda n: distance(n, target))[:count]
