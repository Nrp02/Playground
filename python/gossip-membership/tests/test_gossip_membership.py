import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from gossip.cluster import Cluster
from gossip.node import MemberState


def run_until(cluster: Cluster, max_ticks: int, predicate) -> bool:
    for _ in range(max_ticks):
        cluster.step()
        if predicate():
            return True
    return False


class TestDirectProbe(unittest.TestCase):
    def test_direct_probe_keeps_healthy_node_alive(self):
        cluster = Cluster([1, 2, 3], seed=1)
        cluster.run(500)
        for observer in (1, 2, 3):
            for target in (1, 2, 3):
                if observer == target:
                    continue
                self.assertEqual(cluster.state_of(observer, target), MemberState.ALIVE)

    def test_direct_probe_never_marks_suspect_without_cause(self):
        cluster = Cluster([1, 2, 3, 4], seed=2)
        cluster.run(300)
        for observer in cluster.nodes:
            for target in cluster.nodes:
                if observer == target:
                    continue
                self.assertEqual(cluster.state_of(observer, target), MemberState.ALIVE)


class TestIndirectProbe(unittest.TestCase):
    def test_indirect_probe_saves_node_with_one_bad_link(self):
        cluster = Cluster([1, 2, 3, 4, 5], seed=3)
        cluster.run(50)
        cluster.break_link(1, 2)
        cluster.run(400)
        self.assertEqual(cluster.state_of(1, 2), MemberState.ALIVE)
        self.assertEqual(cluster.state_of(2, 1), MemberState.ALIVE)
        for observer in (3, 4, 5):
            self.assertEqual(cluster.state_of(observer, 1), MemberState.ALIVE)
            self.assertEqual(cluster.state_of(observer, 2), MemberState.ALIVE)

    def test_fully_isolated_pair_still_detected_as_failed_via_direct_timeout(self):
        cluster = Cluster([1, 2], seed=4)
        cluster.silence(2)
        found = run_until(cluster, 2000, lambda: cluster.state_of(1, 2) == MemberState.FAILED)
        self.assertTrue(found)


class TestFailureDetection(unittest.TestCase):
    def test_eventual_failure_detection(self):
        cluster = Cluster([1, 2, 3, 4, 5], seed=5)
        cluster.run(100)
        cluster.silence(5)
        converged = run_until(cluster, 2000, lambda: cluster.all_agree(5, MemberState.FAILED))
        self.assertTrue(converged)
        for observer in (1, 2, 3, 4):
            self.assertEqual(cluster.state_of(observer, 5), MemberState.FAILED)

    def test_node_rejoins_and_is_marked_alive_again(self):
        cluster = Cluster([1, 2, 3, 4, 5], seed=6)
        cluster.run(100)
        cluster.silence(5)
        self.assertTrue(run_until(cluster, 2000, lambda: cluster.all_agree(5, MemberState.FAILED)))

        cluster.rejoin(5)
        rejoined = run_until(
            cluster,
            2000,
            lambda: all(cluster.state_of(n, 5) == MemberState.ALIVE for n in (1, 2, 3, 4)),
        )
        self.assertTrue(rejoined)
        self.assertEqual(cluster.state_of(5, 1), MemberState.ALIVE)


class TestGossipConvergence(unittest.TestCase):
    def test_all_nodes_agree_on_membership_within_n_ticks(self):
        cluster = Cluster([1, 2, 3, 4, 5, 6], seed=7)
        converged = run_until(cluster, 500, cluster.fully_converged)
        self.assertTrue(converged)
        self.assertTrue(cluster.fully_converged())

    def test_convergence_survives_failure_and_recovery_cycle(self):
        cluster = Cluster([1, 2, 3, 4, 5], seed=8)
        self.assertTrue(run_until(cluster, 500, cluster.fully_converged))

        cluster.silence(3)
        self.assertTrue(run_until(cluster, 2000, lambda: cluster.all_agree(3, MemberState.FAILED)))

        cluster.rejoin(3)
        self.assertTrue(
            run_until(
                cluster,
                2000,
                lambda: all(cluster.state_of(n, 3) == MemberState.ALIVE for n in (1, 2, 4, 5)),
            )
        )
        self.assertTrue(cluster.fully_converged())


if __name__ == "__main__":
    unittest.main()
