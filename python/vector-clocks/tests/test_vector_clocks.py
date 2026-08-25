import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from causal_broadcast import CausalNode
from vector_clock import VectorClock, compare_clocks


class TestVectorClockMechanics(unittest.TestCase):
    def test_increment_own_entry(self):
        clock = VectorClock(["A", "B"])
        clock.increment("A")
        self.assertEqual(clock.snapshot(), {"A": 1, "B": 0})
        clock.increment("A")
        self.assertEqual(clock.snapshot(), {"A": 2, "B": 0})

    def test_increment_unknown_node_raises(self):
        clock = VectorClock(["A", "B"])
        with self.assertRaises(KeyError):
            clock.increment("Z")

    def test_merge_is_elementwise_max(self):
        clock = VectorClock(["A", "B", "C"])
        clock.increment("A")
        clock.increment("A")
        clock.merge({"A": 1, "B": 3, "C": 0})
        self.assertEqual(clock.snapshot(), {"A": 2, "B": 3, "C": 0})

    def test_merge_does_not_lower_existing_counters(self):
        clock = VectorClock(["A", "B"])
        clock.increment("A")
        clock.increment("A")
        clock.increment("A")
        clock.merge({"A": 1, "B": 0})
        self.assertEqual(clock.snapshot()["A"], 3)

    def test_snapshot_is_a_copy(self):
        clock = VectorClock(["A"])
        snapshot = clock.snapshot()
        snapshot["A"] = 99
        self.assertEqual(clock.snapshot()["A"], 0)


class TestClockComparison(unittest.TestCase):
    def test_equal_clocks(self):
        left = {"A": 1, "B": 2}
        right = {"A": 1, "B": 2}
        self.assertEqual(compare_clocks(left, right), "equal")

    def test_happens_before(self):
        left = {"A": 1, "B": 0}
        right = {"A": 1, "B": 1}
        self.assertEqual(compare_clocks(left, right), "happens-before")

    def test_happens_after(self):
        left = {"A": 2, "B": 3}
        right = {"A": 1, "B": 3}
        self.assertEqual(compare_clocks(left, right), "happens-after")

    def test_concurrent(self):
        left = {"A": 2, "B": 0}
        right = {"A": 0, "B": 2}
        self.assertEqual(compare_clocks(left, right), "concurrent")

    def test_concurrent_with_missing_keys_defaults_to_zero(self):
        left = {"A": 1}
        right = {"B": 1}
        self.assertEqual(compare_clocks(left, right), "concurrent")

    def test_zero_clocks_are_equal(self):
        left = {"A": 0, "B": 0}
        right = {"A": 0, "B": 0}
        self.assertEqual(compare_clocks(left, right), "equal")


class TestCausalDelivery(unittest.TestCase):
    def make_nodes(self):
        node_ids = ["A", "B", "C"]
        return {node_id: CausalNode(node_id, node_ids) for node_id in node_ids}

    def test_direct_message_delivers_immediately(self):
        nodes = self.make_nodes()
        a, b = nodes["A"], nodes["B"]
        msg = a.send("hello")
        b.receive(msg)
        self.assertEqual(b.delivered_payloads, ["hello"])
        self.assertEqual(b.pending_count(), 0)

    def test_out_of_order_message_is_buffered_then_released(self):
        nodes = self.make_nodes()
        a, b, c = nodes["A"], nodes["B"], nodes["C"]

        msg1 = a.send("m1")
        b.receive(msg1)
        msg2 = b.send("m2")

        c.receive(msg2)
        self.assertEqual(c.delivered_payloads, [])
        self.assertEqual(c.pending_count(), 1)

        c.receive(msg1)
        self.assertEqual(c.delivered_payloads, ["m1", "m2"])
        self.assertEqual(c.pending_count(), 0)

    def test_causal_chain_of_three_delivered_in_order_despite_reversed_arrival(self):
        nodes = self.make_nodes()
        a, b, c = nodes["A"], nodes["B"], nodes["C"]

        msg1 = a.send("m1")
        b.receive(msg1)
        msg2 = b.send("m2")
        b.receive(msg2)
        msg3 = b.send("m3")

        c.receive(msg3)
        self.assertEqual(c.pending_count(), 1)
        self.assertEqual(c.delivered_payloads, [])

        c.receive(msg2)
        self.assertEqual(c.pending_count(), 2)
        self.assertEqual(c.delivered_payloads, [])

        c.receive(msg1)
        self.assertEqual(c.delivered_payloads, ["m1", "m2", "m3"])
        self.assertEqual(c.pending_count(), 0)

    def test_independent_messages_deliver_without_waiting_on_each_other(self):
        nodes = self.make_nodes()
        a, b, c = nodes["A"], nodes["B"], nodes["C"]

        msg_a = a.send("from A")
        msg_b = b.send("from B")

        c.receive(msg_b)
        self.assertEqual(c.delivered_payloads, ["from B"])
        c.receive(msg_a)
        self.assertEqual(c.delivered_payloads, ["from B", "from A"])

    def test_duplicate_prior_delivery_condition_not_reevaluated_twice(self):
        nodes = self.make_nodes()
        a, b = nodes["A"], nodes["B"]

        msg1 = a.send("first")
        b.receive(msg1)
        msg2 = a.send("second")
        b.receive(msg2)

        self.assertEqual(b.delivered_payloads, ["first", "second"])


if __name__ == "__main__":
    unittest.main()
