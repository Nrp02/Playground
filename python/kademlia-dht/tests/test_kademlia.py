from __future__ import annotations

import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from kademlia import (
    Contact,
    KBucket,
    KademliaNode,
    Network,
    RoutingTable,
    Timeout,
    bucket_index,
    brute_force_closest,
    build_network,
    distance,
    id_in_bucket,
    key_id,
    random_id,
    shared_prefix_len,
)

BITS = 160
K = 8
ALPHA = 3


def always_alive(contact: Contact) -> bool:
    return True


def always_dead(contact: Contact) -> bool:
    return False


class TestDistance(unittest.TestCase):
    def test_identity_and_symmetry(self):
        rng = random.Random(1)
        for _ in range(200):
            a = random_id(rng, BITS)
            b = random_id(rng, BITS)
            self.assertEqual(distance(a, a), 0)
            self.assertEqual(distance(a, b), distance(b, a))

    def test_zero_only_for_equal_ids(self):
        rng = random.Random(2)
        for _ in range(200):
            a = random_id(rng, BITS)
            b = random_id(rng, BITS)
            if a != b:
                self.assertGreater(distance(a, b), 0)

    def test_triangle_inequality_under_xor(self):
        rng = random.Random(3)
        for _ in range(200):
            a = random_id(rng, BITS)
            b = random_id(rng, BITS)
            c = random_id(rng, BITS)
            self.assertLessEqual(distance(a, c), distance(a, b) + distance(b, c))

    def test_unidirectionality(self):
        rng = random.Random(4)
        target = random_id(rng, BITS)
        seen = {}
        for _ in range(500):
            node = random_id(rng, BITS)
            d = distance(node, target)
            if d in seen:
                self.assertEqual(seen[d], node)
            seen[d] = node

    def test_ordering_is_total_and_consistent(self):
        rng = random.Random(5)
        target = random_id(rng, BITS)
        ids = [random_id(rng, BITS) for _ in range(50)]
        ordered = sorted(ids, key=lambda n: distance(n, target))
        for left, right in zip(ordered, ordered[1:]):
            self.assertLess(distance(left, target), distance(right, target))

    def test_key_id_is_deterministic_and_in_range(self):
        for bits in (16, 32, 160):
            first = key_id("proto:kademlia", bits)
            second = key_id("proto:kademlia", bits)
            self.assertEqual(first, second)
            self.assertLess(first, 1 << bits)
        self.assertNotEqual(key_id("a", BITS), key_id("b", BITS))


class TestBucketIndex(unittest.TestCase):
    def test_index_matches_shared_prefix(self):
        rng = random.Random(11)
        for _ in range(300):
            owner = random_id(rng, BITS)
            other = random_id(rng, BITS)
            if owner == other:
                continue
            index = bucket_index(owner, other, BITS)
            prefix = shared_prefix_len(owner, other, BITS)
            self.assertEqual(index, BITS - prefix - 1)

    def test_index_brackets_the_distance(self):
        rng = random.Random(12)
        owner = random_id(rng, BITS)
        for _ in range(300):
            other = random_id(rng, BITS)
            if other == owner:
                continue
            index = bucket_index(owner, other, BITS)
            d = distance(owner, other)
            self.assertLessEqual(1 << index, d)
            self.assertLess(d, 1 << (index + 1))

    def test_small_key_space_indices(self):
        self.assertEqual(bucket_index(0b0000, 0b0001, 4), 0)
        self.assertEqual(bucket_index(0b0000, 0b0010, 4), 1)
        self.assertEqual(bucket_index(0b0000, 0b0011, 4), 1)
        self.assertEqual(bucket_index(0b0000, 0b1000, 4), 3)
        self.assertEqual(bucket_index(0b1010, 0b1011, 4), 0)

    def test_own_id_has_no_bucket(self):
        with self.assertRaises(ValueError):
            bucket_index(42, 42, BITS)

    def test_id_in_bucket_lands_in_that_bucket(self):
        rng = random.Random(13)
        owner = random_id(rng, BITS)
        for index in range(0, BITS, 7):
            generated = id_in_bucket(owner, index, rng)
            self.assertEqual(bucket_index(owner, generated, BITS), index)


class TestKBucket(unittest.TestCase):
    def test_append_and_find(self):
        bucket = KBucket(3)
        bucket.append(Contact(1, "a"))
        bucket.append(Contact(2, "b"))
        self.assertEqual(len(bucket), 2)
        self.assertFalse(bucket.is_full())
        self.assertIsNotNone(bucket.find(1))
        self.assertIsNone(bucket.find(9))

    def test_promote_moves_to_tail(self):
        bucket = KBucket(3)
        for node_id in (1, 2, 3):
            bucket.append(Contact(node_id, "n{}".format(node_id)))
        self.assertEqual(bucket.least_recently_seen().node_id, 1)
        bucket.promote(bucket.find(1), 10)
        self.assertEqual(bucket.least_recently_seen().node_id, 2)
        self.assertEqual(bucket.most_recently_seen().node_id, 1)

    def test_remove(self):
        bucket = KBucket(3)
        bucket.append(Contact(1, "a"))
        self.assertTrue(bucket.remove(1))
        self.assertFalse(bucket.remove(1))


class TestRoutingTable(unittest.TestCase):
    def make_table(self, ping):
        return RoutingTable(0, k=2, bits=8, ping=ping)

    def test_ignores_own_id(self):
        table = self.make_table(always_alive)
        self.assertFalse(table.update(Contact(0, "self"), 1))
        self.assertEqual(table.size(), 0)

    def test_contacts_land_in_the_right_bucket(self):
        table = RoutingTable(0, k=4, bits=8, ping=always_alive)
        table.update(Contact(0b0001, "a"), 1)
        table.update(Contact(0b0110, "b"), 2)
        table.update(Contact(0b1000, "c"), 3)
        self.assertEqual(sorted(table.buckets), [0, 2, 3])
        self.assertEqual(len(table.buckets[0]), 1)
        self.assertEqual(len(table.buckets[2]), 1)

    def test_reinsert_promotes_without_growing(self):
        table = RoutingTable(0, k=4, bits=8, ping=always_alive)
        table.update(Contact(4, "a"), 1)
        table.update(Contact(5, "b"), 2)
        table.update(Contact(4, "a"), 3)
        bucket = table.buckets[2]
        self.assertEqual(len(bucket), 2)
        self.assertEqual(bucket.most_recently_seen().node_id, 4)

    def test_full_bucket_keeps_responsive_least_recently_seen(self):
        table = self.make_table(always_alive)
        table.update(Contact(4, "a"), 1)
        table.update(Contact(5, "b"), 2)
        bucket = table.buckets[2]
        self.assertTrue(bucket.is_full())
        self.assertFalse(table.update(Contact(6, "c"), 3))
        self.assertFalse(table.contains(6))
        self.assertTrue(table.contains(4))
        self.assertTrue(table.contains(5))
        self.assertEqual(bucket.most_recently_seen().node_id, 4)
        self.assertEqual(bucket.least_recently_seen().node_id, 5)

    def test_full_bucket_evicts_unresponsive_least_recently_seen(self):
        table = self.make_table(always_dead)
        table.update(Contact(4, "a"), 1)
        table.update(Contact(5, "b"), 2)
        self.assertTrue(table.update(Contact(6, "c"), 3))
        self.assertFalse(table.contains(4))
        self.assertTrue(table.contains(5))
        self.assertTrue(table.contains(6))
        self.assertEqual(table.buckets[2].most_recently_seen().node_id, 6)

    def test_probe_order_follows_least_recently_seen(self):
        probed = []

        def record(contact: Contact) -> bool:
            probed.append(contact.node_id)
            return True

        table = RoutingTable(0, k=2, bits=8, ping=record)
        table.update(Contact(4, "a"), 1)
        table.update(Contact(5, "b"), 2)
        table.update(Contact(4, "a"), 3)
        table.update(Contact(6, "c"), 4)
        self.assertEqual(probed, [5])

    def test_no_ping_callback_keeps_the_incumbent(self):
        table = RoutingTable(0, k=1, bits=8, ping=None)
        table.update(Contact(4, "a"), 1)
        self.assertFalse(table.update(Contact(5, "b"), 2))
        self.assertTrue(table.contains(4))

    def test_closest_returns_sorted_copies(self):
        table = RoutingTable(0, k=8, bits=8, ping=always_alive)
        for node_id in (1, 2, 3, 12, 200):
            table.update(Contact(node_id, "n{}".format(node_id)), node_id)
        closest = table.closest(3, 3)
        self.assertEqual([c.node_id for c in closest], [3, 2, 1])
        closest[0].last_seen = 999
        self.assertNotEqual(table.buckets[bucket_index(0, 3, 8)].find(3).last_seen, 999)

    def test_closest_honours_exclude(self):
        table = RoutingTable(0, k=8, bits=8, ping=always_alive)
        for node_id in (1, 2, 3):
            table.update(Contact(node_id, "n"), node_id)
        self.assertEqual([c.node_id for c in table.closest(3, 3, exclude=[3])], [2, 1])

    def test_remove_and_occupancy(self):
        table = RoutingTable(0, k=4, bits=8, ping=always_alive)
        table.update(Contact(1, "a"), 1)
        table.update(Contact(8, "b"), 2)
        self.assertEqual(table.occupancy(), [(0, 1), (3, 1)])
        self.assertTrue(table.remove(8))
        self.assertEqual(table.occupancy(), [(0, 1)])
        self.assertFalse(table.remove(8))
        self.assertFalse(table.remove(0))


class TestRpcs(unittest.TestCase):
    def setUp(self):
        self.network = Network()
        self.a = KademliaNode(0b0001, self.network, k=4, alpha=2, bits=8, address="a")
        self.b = KademliaNode(0b1000, self.network, k=4, alpha=2, bits=8, address="b")
        self.network.register(self.a)
        self.network.register(self.b)

    def test_ping_marks_the_sender_known(self):
        self.assertTrue(self.a.ping(self.b.contact))
        self.assertTrue(self.b.routing.contains(self.a.id))

    def test_ping_to_dead_node_times_out_and_prunes(self):
        self.a.routing.update(self.b.contact, 1)
        self.network.kill(self.b.id)
        self.assertFalse(self.a.ping(self.b.contact))
        self.assertFalse(self.a.routing.contains(self.b.id))
        self.assertEqual(self.network.timeouts, 1)

    def test_store_and_find_value(self):
        key = key_id("k", 8)
        self.assertTrue(self.b.rpc_store(self.a.contact, key, "v"))
        value, peers = self.b.rpc_find_value(self.a.contact, key)
        self.assertEqual(value, "v")
        self.assertEqual(peers, [])

    def test_find_value_falls_back_to_contacts(self):
        self.b.routing.update(Contact(0b1100, "c"), 1)
        value, peers = self.b.rpc_find_value(self.a.contact, key_id("absent", 8))
        self.assertIsNone(value)
        self.assertTrue(all(c.node_id != self.a.id for c in peers))

    def test_find_node_excludes_the_sender(self):
        self.b.routing.update(self.a.contact, 1)
        self.b.routing.update(Contact(0b1100, "c"), 2)
        peers = self.b.rpc_find_node(self.a.contact, 0)
        self.assertEqual([c.node_id for c in peers], [0b1100])

    def test_unknown_rpc_rejected(self):
        with self.assertRaises(ValueError):
            self.network.call(self.a.contact, self.b.id, "GOSSIP")

    def test_call_to_unregistered_node_times_out(self):
        with self.assertRaises(Timeout):
            self.network.call(self.a.contact, 0b0111, "PING")

    def test_duplicate_registration_rejected(self):
        with self.assertRaises(ValueError):
            self.network.register(self.a)


class TestNodeEviction(unittest.TestCase):
    def test_dead_least_recently_seen_contact_is_replaced(self):
        network = Network()
        owner = KademliaNode(0, network, k=2, alpha=2, bits=8, address="owner")
        network.register(owner)
        peers = []
        for node_id in (4, 5, 6):
            peer = KademliaNode(node_id, network, k=2, alpha=2, bits=8, address="p{}".format(node_id))
            network.register(peer)
            peers.append(peer)
        owner.routing.update(peers[0].contact, 1)
        owner.routing.update(peers[1].contact, 2)
        bucket = owner.routing.buckets[2]
        self.assertFalse(owner.routing.update(peers[2].contact, 3))
        self.assertEqual(bucket.least_recently_seen().node_id, 5)
        network.kill(5)
        self.assertTrue(owner.routing.update(peers[2].contact, 4))
        self.assertFalse(owner.routing.contains(5))
        self.assertTrue(owner.routing.contains(4))
        self.assertTrue(owner.routing.contains(6))


class TestBootstrap(unittest.TestCase):
    def test_join_links_both_directions(self):
        network = Network()
        seed = KademliaNode(random_id(random.Random(1), BITS), network, k=K, alpha=ALPHA, bits=BITS)
        joiner = KademliaNode(random_id(random.Random(2), BITS), network, k=K, alpha=ALPHA, bits=BITS)
        network.register(seed)
        network.register(joiner)
        self.assertTrue(joiner.join(seed.contact))
        self.assertTrue(joiner.routing.contains(seed.id))
        self.assertTrue(seed.routing.contains(joiner.id))

    def test_join_against_a_dead_bootstrap_fails(self):
        network = Network()
        seed = KademliaNode(1 << 100, network, k=K, alpha=ALPHA, bits=BITS)
        joiner = KademliaNode(1 << 90, network, k=K, alpha=ALPHA, bits=BITS)
        network.register(seed)
        network.register(joiner)
        network.kill(seed.id)
        self.assertFalse(joiner.join(seed.contact))

    def test_join_refuses_self_bootstrap(self):
        network = Network()
        seed = KademliaNode(7, network, k=K, alpha=ALPHA, bits=BITS)
        network.register(seed)
        self.assertFalse(seed.join(seed.contact))

    def test_bootstrap_populates_the_whole_routing_table(self):
        network, nodes = build_network(30, k=K, alpha=ALPHA, bits=BITS, seed=21)
        latest = nodes[-1]
        self.assertGreaterEqual(latest.routing.size(), K)
        self.assertGreaterEqual(len(latest.routing.occupancy()), 2)
        for node in nodes:
            self.assertGreater(node.routing.size(), 0)

    def test_joiner_learns_its_closest_neighbour(self):
        network, nodes = build_network(30, k=K, alpha=ALPHA, bits=BITS, seed=22)
        latest = nodes[-1]
        nearest = brute_force_closest([n.id for n in nodes if n.id != latest.id], latest.id, 1)[0]
        self.assertTrue(latest.routing.contains(nearest))


class TestIterativeLookup(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.network, cls.nodes = build_network(40, k=K, alpha=ALPHA, bits=BITS, seed=7)
        cls.ids = [node.id for node in cls.nodes]

    def test_find_node_matches_brute_force(self):
        rng = random.Random(31)
        for _ in range(40):
            source = rng.choice(self.nodes)
            target = random_id(rng, BITS)
            found = source.node_lookup(target).node_ids()
            expected = brute_force_closest([i for i in self.ids if i != source.id], target, K)
            self.assertEqual(found, expected)

    def test_lookup_for_an_existing_node_id_finds_it(self):
        rng = random.Random(32)
        for _ in range(10):
            source = rng.choice(self.nodes)
            other = rng.choice([n for n in self.nodes if n.id != source.id])
            self.assertIn(other.id, source.node_lookup(other.id).node_ids())

    def test_lookup_cost_is_far_below_the_network_size(self):
        rng = random.Random(33)
        for _ in range(20):
            source = rng.choice(self.nodes)
            result = source.node_lookup(random_id(rng, BITS))
            self.assertLess(result.rpcs, len(self.nodes))
            self.assertGreater(result.rpcs, 0)
            self.assertGreater(result.rounds, 0)

    def test_lookup_never_returns_the_searcher_or_duplicates(self):
        rng = random.Random(34)
        for _ in range(20):
            source = rng.choice(self.nodes)
            ids = source.node_lookup(random_id(rng, BITS)).node_ids()
            self.assertNotIn(source.id, ids)
            self.assertEqual(len(ids), len(set(ids)))

    def test_lookup_terminates_when_every_peer_is_dead(self):
        network, nodes = build_network(20, k=K, alpha=ALPHA, bits=BITS, seed=35)
        survivor = nodes[5]
        for node in nodes:
            if node.id != survivor.id:
                network.kill(node.id)
        before = survivor.routing.size()
        result = survivor.node_lookup(random_id(random.Random(36), BITS))
        self.assertEqual(result.contacts, [])
        self.assertEqual(result.rpcs, result.timeouts)
        self.assertGreater(result.rpcs, 0)
        self.assertLess(survivor.routing.size(), before)
        for _ in range(3):
            survivor.node_lookup(random_id(random.Random(37), BITS))
        self.assertEqual(survivor.routing.size(), 0)

    def test_isolated_node_lookup_returns_nothing(self):
        network = Network()
        lonely = KademliaNode(12345, network, k=K, alpha=ALPHA, bits=BITS)
        network.register(lonely)
        result = lonely.node_lookup(999)
        self.assertEqual(result.contacts, [])
        self.assertEqual(result.rpcs, 0)


class TestStorageAndRetrieval(unittest.TestCase):
    def setUp(self):
        self.network, self.nodes = build_network(35, k=K, alpha=ALPHA, bits=BITS, seed=13)

    def holders(self, key):
        return [node for node in self.nodes if node.holds(key)]

    def test_store_replicates_to_the_k_closest_nodes(self):
        key = "proto:kademlia"
        replicas = self.nodes[0].store(key, "xor")
        self.assertEqual(replicas, K)
        target = key_id(key, BITS)
        expected = set(brute_force_closest([n.id for n in self.nodes], target, K))
        self.assertEqual({node.id for node in self.holders(key)}, expected)

    def test_get_from_every_other_node(self):
        key, value = "city:kyoto", "35.0116N"
        self.nodes[0].store(key, value)
        holder_ids = {node.id for node in self.holders(key)}
        readers = [node for node in self.nodes if node.id not in holder_ids]
        self.assertTrue(readers)
        for reader in readers:
            self.assertEqual(reader.get(key), value)

    def test_get_reports_rpc_cost_and_hits_a_holder(self):
        key, value = "artist:radiohead", "in rainbows"
        self.nodes[2].store(key, value)
        holder_ids = {node.id for node in self.holders(key)}
        reader = next(node for node in self.nodes if node.id not in holder_ids)
        result = reader.get_with_stats(key)
        self.assertTrue(result.found)
        self.assertEqual(result.value, value)
        self.assertGreaterEqual(result.rpcs, 1)
        self.assertEqual(len(result.contacts), 1)
        self.assertIn(result.contacts[0].node_id, holder_ids)

    def test_local_hit_costs_no_rpcs(self):
        key, value = "local", "v"
        writer = self.nodes[4]
        writer.store(key, value)
        holder = self.holders(key)[0]
        result = holder.get_with_stats(key)
        self.assertTrue(result.found)
        self.assertEqual(result.rpcs, 0)

    def test_absent_key_returns_none_after_a_full_lookup(self):
        result = self.nodes[9].get_with_stats("key:never-written")
        self.assertIsNone(result.value)
        self.assertFalse(result.found)
        self.assertEqual(len(result.contacts), K)
        self.assertGreater(result.rounds, 0)

    def test_absent_key_from_every_node(self):
        for node in self.nodes:
            self.assertIsNone(node.get("nothing-here"))

    def test_value_survives_killing_all_but_one_replica(self):
        key, value = "durable", "still-here"
        self.nodes[1].store(key, value)
        target = key_id(key, BITS)
        replicas = sorted(self.holders(key), key=lambda n: distance(n.id, target))
        self.assertEqual(len(replicas), K)
        survivor = replicas[-1]
        for node in replicas[:-1]:
            self.network.kill(node.id)
        readers = [
            node
            for node in self.nodes
            if self.network.is_online(node.id) and not node.holds(key)
        ]
        self.assertTrue(readers)
        for reader in readers:
            result = reader.get_with_stats(key)
            self.assertEqual(result.value, value)
            self.assertEqual(result.contacts[0].node_id, survivor.id)

    def test_value_is_lost_only_when_every_replica_dies(self):
        key, value = "fragile", "gone"
        self.nodes[1].store(key, value)
        for node in self.holders(key):
            self.network.kill(node.id)
        reader = next(node for node in self.nodes if self.network.is_online(node.id))
        self.assertIsNone(reader.get(key))

    def test_lookup_still_converges_after_a_third_of_the_network_dies(self):
        key, value = "resilient", "v"
        self.nodes[1].store(key, value)
        holder_ids = {node.id for node in self.holders(key)}
        rng = random.Random(41)
        victims = [n for n in self.nodes if n.id not in holder_ids]
        rng.shuffle(victims)
        for node in victims[: len(self.nodes) // 3]:
            self.network.kill(node.id)
        survivors = [
            node
            for node in self.nodes
            if self.network.is_online(node.id) and node.id not in holder_ids
        ]
        self.assertTrue(survivors)
        for reader in survivors:
            self.assertEqual(reader.get(key), value)

    def test_store_from_a_node_that_is_itself_closest(self):
        key = "self-store"
        target = key_id(key, BITS)
        closest_id = brute_force_closest([n.id for n in self.nodes], target, 1)[0]
        writer = next(node for node in self.nodes if node.id == closest_id)
        writer.store(key, "mine")
        self.assertTrue(writer.holds(key))
        self.assertEqual(len(self.holders(key)), K)

    def test_revived_node_is_reachable_again(self):
        key, value = "revive", "v"
        self.nodes[1].store(key, value)
        holder = self.holders(key)[0]
        self.network.kill(holder.id)
        self.assertFalse(self.network.is_online(holder.id))
        self.network.revive(holder.id)
        self.assertTrue(self.network.is_online(holder.id))
        self.assertTrue(self.nodes[0].ping(holder.contact))


if __name__ == "__main__":
    unittest.main()
