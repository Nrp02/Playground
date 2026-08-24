import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from raft.cluster import Cluster
from raft.node import Role


def elect_leader(cluster: Cluster, max_ticks: int = 2000):
    for _ in range(max_ticks):
        cluster.step()
        if cluster.leader() is not None:
            return cluster.leader()
    raise AssertionError("no leader elected")


class TestElection(unittest.TestCase):
    def test_single_leader_elected(self):
        cluster = Cluster([1, 2, 3, 4, 5], seed=1)
        leader = elect_leader(cluster)
        followers = [n for n in cluster.nodes.values() if n.id != leader.id]
        self.assertTrue(all(f.role != Role.LEADER for f in followers))
        self.assertEqual(len(cluster.leaders()), 1)

    def test_election_safety_at_most_one_leader_per_term(self):
        cluster = Cluster([1, 2, 3], seed=2)
        cluster.run(3000)
        terms_seen = {}
        for node in cluster.nodes.values():
            if node.role == Role.LEADER:
                terms_seen.setdefault(node.current_term, []).append(node.id)
        for term, leaders in terms_seen.items():
            self.assertEqual(len(leaders), 1, f"multiple leaders in term {term}: {leaders}")


class TestReplication(unittest.TestCase):
    def test_committed_entries_converge_across_nodes(self):
        cluster = Cluster([1, 2, 3, 4, 5], seed=3)
        elect_leader(cluster)

        for command in ["a", "b", "c"]:
            self.assertTrue(cluster.submit(command))
        cluster.run(400)

        committed_logs = [cluster.committed_log(node_id) for node_id in cluster.nodes]
        for log in committed_logs:
            self.assertEqual(log, ["a", "b", "c"])

    def test_submit_fails_without_leader(self):
        cluster = Cluster([1, 2, 3], seed=4)
        self.assertFalse(cluster.submit("too-early"))


class TestPartitionAndReelection(unittest.TestCase):
    def test_majority_partition_elects_new_leader(self):
        cluster = Cluster([1, 2, 3, 4, 5], seed=5)
        original_leader = elect_leader(cluster)

        minority = {original_leader.id}
        majority = set(cluster.nodes) - minority
        cluster.partition([minority, majority])
        cluster.run(2500)

        majority_leaders = [cluster.nodes[n] for n in majority if cluster.nodes[n].role == Role.LEADER]
        self.assertEqual(len(majority_leaders), 1)
        new_leader = majority_leaders[0]
        self.assertGreater(new_leader.current_term, original_leader.current_term)

    def test_log_converges_after_partition_heals(self):
        cluster = Cluster([1, 2, 3, 4, 5], seed=6)
        original_leader = elect_leader(cluster)

        minority = {original_leader.id}
        majority = set(cluster.nodes) - minority
        cluster.partition([minority, majority])
        cluster.run(600)

        majority_leaders = [cluster.nodes[n] for n in majority if cluster.nodes[n].role == Role.LEADER]
        self.assertEqual(len(majority_leaders), 1)
        self.assertTrue(majority_leaders[0].submit("during-partition"))
        cluster.run(600)

        cluster.heal_partition()
        cluster.run(600)

        self.assertEqual(len(cluster.leaders()), 1)
        committed_logs = [cluster.committed_log(node_id) for node_id in cluster.nodes]
        for log in committed_logs[1:]:
            self.assertEqual(log, committed_logs[0])
        self.assertIn("during-partition", committed_logs[0])


if __name__ == "__main__":
    unittest.main()
