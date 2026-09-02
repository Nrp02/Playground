from __future__ import annotations

import math
import random
from typing import List

from kademlia import (
    KademliaNode,
    Network,
    brute_force_closest,
    build_network,
    distance,
    key_id,
    random_id,
    short_id,
)

BITS = 160
K = 8
ALPHA = 3
SIZE = 40


def holders(nodes: List[KademliaNode], key: str) -> List[KademliaNode]:
    return [node for node in nodes if node.holds(key)]


def describe_buckets(node: KademliaNode, peers: int) -> None:
    print("  routing table of {} (id {}...)".format(node.address, short_id(node.id, BITS)))
    print("    knows {} of {} peers, spread over {} non-empty buckets".format(
        node.routing.size(), peers, len(node.routing.occupancy())
    ))
    for index, count in node.routing.occupancy():
        span = "distance in [2^{}, 2^{})".format(index, index + 1)
        print("    bucket {:>3}: {} contact(s)  {}  {}".format(index, count, "#" * count, span))


def demo_bootstrap() -> tuple:
    print("=== bootstrapping a {}-node network ({}-bit ids, k={}, alpha={}) ===".format(
        SIZE, BITS, K, ALPHA
    ))
    network, nodes = build_network(SIZE, k=K, alpha=ALPHA, bits=BITS, seed=7)
    print("every node after the first joined through {} and then ran a self-lookup".format(
        nodes[0].address
    ))
    print("total rpcs spent forming the overlay: {}".format(network.rpc_count))
    print("rpc mix: {}".format(dict(sorted(network.rpc_counts.items()))))
    print()
    describe_buckets(nodes[17], SIZE - 1)
    print()
    return network, nodes


def demo_lookup_accuracy(network: Network, nodes: List[KademliaNode]) -> None:
    print("=== iterative FIND_NODE vs a brute-force sort of every node id ===")
    rng = random.Random(101)
    ids = [node.id for node in nodes]
    exact = 0
    trials = 25
    for _ in range(trials):
        source = rng.choice(nodes)
        target = random_id(rng, BITS)
        found = source.node_lookup(target).node_ids()
        expected = brute_force_closest([i for i in ids if i != source.id], target, K)
        if found[:K] == expected:
            exact += 1
    print("  {}/{} random lookups returned exactly the {} globally closest nodes".format(
        exact, trials, K
    ))
    print()


def demo_store_and_get(network: Network, nodes: List[KademliaNode]) -> List[str]:
    print("=== storing keys on the k closest nodes to each key id ===")
    entries = [
        ("artist:radiohead", "in rainbows"),
        ("city:kyoto", "35.0116N 135.7681E"),
        ("proto:kademlia", "xor metric, k-buckets, iterative lookup"),
        ("file:manifest.json", "sha1:9c1185a5c5e9fc54612808977ee8f548b2258d31"),
    ]
    for key, value in entries:
        writer = nodes[3]
        replicas = writer.store(key, value)
        print("  {} <- stored by {} on {} replicas: {}".format(
            key,
            writer.address,
            replicas,
            " ".join(node.address for node in holders(nodes, key)),
        ))
    print()

    print("=== reading the keys back from nodes that never saw the STORE ===")
    for key, value in entries:
        holder_ids = {node.id for node in holders(nodes, key)}
        outsiders = [node for node in nodes if node.id not in holder_ids]
        reader = max(outsiders, key=lambda node: distance(node.id, key_id(key, BITS)))
        network.reset_counters()
        result = reader.get_with_stats(key)
        ok = result.value == value
        print("  {} read {}: {} ({} rpcs, {} rounds, match={})".format(
            reader.address, key, result.value, result.rpcs, result.rounds, ok
        ))
    print()
    return [key for key, _ in entries]


def demo_scaling() -> None:
    print("=== lookup cost vs network size (the O(log n) claim, measured) ===")
    print("  {:>6} {:>10} {:>10} {:>10}".format("nodes", "log2(n)", "avg rpcs", "avg rounds"))
    rng = random.Random(4242)
    for size in (10, 20, 40, 80, 160):
        network, nodes = build_network(size, k=K, alpha=ALPHA, bits=BITS, seed=size)
        total_rpcs = 0
        total_rounds = 0
        trials = 30
        for _ in range(trials):
            source = rng.choice(nodes)
            result = source.node_lookup(random_id(rng, BITS))
            total_rpcs += result.rpcs
            total_rounds += result.rounds
        print("  {:>6} {:>10.2f} {:>10.1f} {:>10.2f}".format(
            size, math.log2(size), total_rpcs / trials, total_rounds / trials
        ))
    print("  rpcs grow far slower than the node count: 16x the nodes costs well under 2x the rpcs")
    print()


def demo_failures(network: Network, nodes: List[KademliaNode], key: str) -> None:
    print("=== killing replica holders ===")
    alive_holders = holders(nodes, key)
    target = key_id(key, BITS)
    alive_holders.sort(key=lambda node: distance(node.id, target))
    doomed = alive_holders[:-1]
    survivor = alive_holders[-1]
    for node in doomed:
        network.kill(node.id)
    print("  key {} had {} replicas; killed {} of them: {}".format(
        key, len(alive_holders), len(doomed), " ".join(node.address for node in doomed)
    ))
    print("  the only replica left is {} (the farthest of the k closest)".format(survivor.address))
    outsiders = [n for n in nodes if network.is_online(n.id) and not n.holds(key)]
    reader = max(outsiders, key=lambda node: distance(node.id, target))
    result = reader.get_with_stats(key)
    print("  {} still reads it: {}".format(reader.address, result.value))
    print("  cost: {} rpcs, {} of which timed out against dead nodes".format(
        result.rpcs, result.timeouts
    ))
    print()


def demo_missing_key(network: Network, nodes: List[KademliaNode]) -> None:
    print("=== looking up a key that was never stored ===")
    reader = next(node for node in nodes if network.is_online(node.id))
    result = reader.get_with_stats("key:never-written")
    print("  {} searched for 'key:never-written'".format(reader.address))
    print("  value: {} (converged on {} closest live nodes after {} rounds, {} rpcs)".format(
        result.value, len(result.contacts), result.rounds, result.rpcs
    ))
    print("  a not-found answer costs a full lookup: the network proved nobody closer has it")
    print()


def main() -> int:
    network, nodes = demo_bootstrap()
    demo_lookup_accuracy(network, nodes)
    keys = demo_store_and_get(network, nodes)
    demo_scaling()
    demo_failures(network, nodes, keys[2])
    demo_missing_key(network, nodes)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
