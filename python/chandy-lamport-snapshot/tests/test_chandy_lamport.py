import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from chandy_lamport.bus import MessageBus
from chandy_lamport.cluster import Cluster
from chandy_lamport.messages import AppMessage, Marker


def run_until(cluster: Cluster, snapshot_id: int, max_ticks: int) -> bool:
    for _ in range(max_ticks):
        cluster.step()
        if cluster.snapshot_complete(snapshot_id):
            return True
    return False


class TestTokenConservation(unittest.TestCase):
    def test_conservation_across_many_seeds_with_heavy_traffic(self):
        for seed in range(30):
            cluster = Cluster([1, 2, 3, 4, 5], initial_balance=80, seed=seed, min_delay=1, max_delay=5)
            cluster.run(25)
            snapshot_id = cluster.initiate_snapshot((seed % 5) + 1)
            completed = run_until(cluster, snapshot_id, 500)
            self.assertTrue(completed, f"snapshot did not complete for seed {seed}")
            result = cluster.collect_snapshot(snapshot_id)
            self.assertEqual(result.total(), cluster.invariant, f"seed {seed}")

    def test_conservation_with_traffic_continuing_after_snapshot(self):
        cluster = Cluster([1, 2, 3], initial_balance=60, seed=5, min_delay=1, max_delay=3)
        cluster.run(10)
        snapshot_id = cluster.initiate_snapshot(1)
        run_until(cluster, snapshot_id, 300)
        result = cluster.collect_snapshot(snapshot_id)
        self.assertEqual(result.total(), cluster.invariant)
        cluster.run(50)
        self.assertEqual(cluster.current_balance_total() + cluster.in_flight_total(), cluster.invariant)


class TestNaiveSnapshotIsWrong(unittest.TestCase):
    def test_naive_balance_sum_misses_in_flight_tokens(self):
        found_mismatch = False
        for seed in range(20):
            cluster = Cluster([1, 2, 3, 4], initial_balance=100, seed=seed, min_delay=1, max_delay=6)
            cluster.run(15)
            if cluster.in_flight_total() > 0:
                naive_total = cluster.current_balance_total()
                self.assertEqual(naive_total + cluster.in_flight_total(), cluster.invariant)
                if naive_total != cluster.invariant:
                    found_mismatch = True
        self.assertTrue(found_mismatch, "expected at least one seed with in-flight tokens making the naive sum wrong")


class TestMarkerDiscipline(unittest.TestCase):
    def test_marker_sent_exactly_once_per_outgoing_channel_per_snapshot(self):
        cluster = Cluster([1, 2, 3, 4], initial_balance=40, seed=3, min_delay=1, max_delay=3)
        cluster.run(10)

        marker_counts = {}
        original_send = cluster.bus.send

        def counting_send(src, dest, message, now):
            if isinstance(message, Marker):
                key = (src, dest, message.snapshot_id)
                marker_counts[key] = marker_counts.get(key, 0) + 1
            original_send(src, dest, message, now)

        cluster.bus.send = counting_send

        snapshot_id = cluster.initiate_snapshot(1)
        completed = run_until(cluster, snapshot_id, 300)
        self.assertTrue(completed)

        node_ids = list(cluster.nodes)
        for src in node_ids:
            for dest in node_ids:
                if src == dest:
                    continue
                self.assertEqual(marker_counts.get((src, dest, snapshot_id), 0), 1)

    def test_every_process_terminates_once_all_incoming_markers_arrive(self):
        cluster = Cluster([1, 2, 3, 4, 5], initial_balance=70, seed=8, min_delay=1, max_delay=4)
        cluster.run(20)
        snapshot_id = cluster.initiate_snapshot(3)
        completed = run_until(cluster, snapshot_id, 400)
        self.assertTrue(completed)
        for node in cluster.nodes.values():
            self.assertTrue(node.is_done(snapshot_id))
            state = node.snapshots[snapshot_id]
            self.assertEqual(state.pending_channels, set())


class TestFifoChannels(unittest.TestCase):
    def test_bus_preserves_per_channel_order_despite_random_delay(self):
        bus = MessageBus(min_delay=1, max_delay=5, seed=123)
        sent_order = list(range(30))
        for tag in sent_order:
            bus.send(1, 2, AppMessage(1, 2, tag), now=0)

        received_order = []
        for now in range(1, 60):
            for src, dest, message in bus.deliver_ready(now):
                received_order.append(message.amount)

        self.assertEqual(received_order, sent_order)

    def test_snapshot_correct_under_heavy_delay_variance(self):
        cluster = Cluster([1, 2, 3, 4], initial_balance=90, seed=17, min_delay=1, max_delay=8)
        cluster.run(30)
        snapshot_id = cluster.initiate_snapshot(2)
        completed = run_until(cluster, snapshot_id, 800)
        self.assertTrue(completed)
        result = cluster.collect_snapshot(snapshot_id)
        self.assertEqual(result.total(), cluster.invariant)


class TestConcurrentInitiators(unittest.TestCase):
    def test_two_concurrent_snapshots_are_independent_and_valid(self):
        cluster = Cluster([1, 2, 3, 4, 5], initial_balance=60, seed=21, min_delay=1, max_delay=4)
        cluster.run(15)
        sid_a = cluster.initiate_snapshot(1)
        sid_b = cluster.initiate_snapshot(4)
        self.assertNotEqual(sid_a, sid_b)

        for _ in range(400):
            cluster.step()
            if cluster.snapshot_complete(sid_a) and cluster.snapshot_complete(sid_b):
                break

        self.assertTrue(cluster.snapshot_complete(sid_a))
        self.assertTrue(cluster.snapshot_complete(sid_b))

        result_a = cluster.collect_snapshot(sid_a)
        result_b = cluster.collect_snapshot(sid_b)
        self.assertEqual(result_a.total(), cluster.invariant)
        self.assertEqual(result_b.total(), cluster.invariant)


class TestEdgeCases(unittest.TestCase):
    def test_single_process_snapshot(self):
        cluster = Cluster([1], initial_balance=42, seed=1)
        snapshot_id = cluster.initiate_snapshot(1)
        self.assertTrue(cluster.snapshot_complete(snapshot_id))
        result = cluster.collect_snapshot(snapshot_id)
        self.assertEqual(result.local_states, {1: 42})
        self.assertEqual(result.channel_states, {})
        self.assertEqual(result.total(), cluster.invariant)

    def test_no_traffic_snapshot(self):
        cluster = Cluster([1, 2, 3], initial_balance=0, seed=2)
        cluster.run(20)
        snapshot_id = cluster.initiate_snapshot(2)
        completed = run_until(cluster, snapshot_id, 100)
        self.assertTrue(completed)
        result = cluster.collect_snapshot(snapshot_id)
        self.assertEqual(result.channel_states, {})
        self.assertEqual(result.total(), 0)
        self.assertEqual(cluster.invariant, 0)


if __name__ == "__main__":
    unittest.main()
