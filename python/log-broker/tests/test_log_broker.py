import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from broker.broker import Broker


class TestPartitionAssignment(unittest.TestCase):
    def test_same_key_always_maps_to_same_partition(self):
        broker = Broker()
        broker.create_topic("t", num_partitions=4)
        first = broker.produce("t", "v1", key="alice")
        second = broker.produce("t", "v2", key="alice")
        self.assertEqual(first[0], second[0])

    def test_different_keys_can_land_on_different_partitions(self):
        broker = Broker()
        broker.create_topic("t", num_partitions=8)
        partitions_used = {broker.produce("t", f"v{key}", key=key)[0] for key in ["alice", "bob", "carol", "dave"]}
        self.assertGreater(len(partitions_used), 1)

    def test_round_robin_without_key_spreads_across_partitions(self):
        broker = Broker()
        broker.create_topic("t", num_partitions=3)
        partitions_used = [broker.produce("t", f"v{i}")[0] for i in range(6)]
        self.assertEqual(partitions_used, [0, 1, 2, 0, 1, 2])


class TestOrderingWithinPartition(unittest.TestCase):
    def test_messages_within_partition_are_ordered_by_offset(self):
        broker = Broker()
        broker.create_topic("t", num_partitions=1)
        for i in range(5):
            broker.produce("t", f"v{i}", key="same-key")
        topic = broker.get_topic("t")
        messages = topic.partitions[0].read_from(0)
        self.assertEqual([m.value for m in messages], [f"v{i}" for i in range(5)])
        self.assertEqual([m.offset for m in messages], [0, 1, 2, 3, 4])

    def test_ordering_holds_within_each_partition_across_multiple_keys(self):
        broker = Broker()
        broker.create_topic("t", num_partitions=2)
        for i in range(10):
            broker.produce("t", f"v{i}", key="alice")
        topic = broker.get_topic("t")
        partition_index = broker.produce("t", "vfinal", key="alice")[0]
        messages = topic.partitions[partition_index].read_from(0)
        offsets = [m.offset for m in messages]
        self.assertEqual(offsets, sorted(offsets))


class TestConsumerGroupIndependence(unittest.TestCase):
    def test_two_groups_track_independent_offsets(self):
        broker = Broker()
        broker.create_topic("t", num_partitions=1)
        for i in range(5):
            broker.produce("t", f"v{i}")

        group_a = broker.create_consumer("t", "a")
        group_b = broker.create_consumer("t", "b")

        first = group_a.poll(0, max_messages=2)
        group_a.commit(0)
        self.assertEqual([m.value for m in first], ["v0", "v1"])

        second = group_b.poll(0, max_messages=5)
        group_b.commit(0)
        self.assertEqual([m.value for m in second], ["v0", "v1", "v2", "v3", "v4"])

        remaining_a = group_a.poll(0, max_messages=10)
        self.assertEqual([m.value for m in remaining_a], ["v2", "v3", "v4"])

    def test_group_offsets_are_tracked_per_partition(self):
        broker = Broker()
        broker.create_topic("t", num_partitions=2)
        used_partition = None
        for i in range(4):
            used_partition, _ = broker.produce("t", f"v{i}", key="alice")
        other_partition = 1 - used_partition

        consumer = broker.create_consumer("t", "g")
        consumer.poll(used_partition, max_messages=2)
        consumer.commit(used_partition)

        self.assertEqual(consumer._group.committed(used_partition), 2)
        self.assertEqual(consumer._group.committed(other_partition), 0)


class TestRestartRedelivery(unittest.TestCase):
    def test_uncommitted_messages_are_redelivered_after_restart(self):
        broker = Broker()
        broker.create_topic("t", num_partitions=1)
        for i in range(3):
            broker.produce("t", f"v{i}")

        consumer = broker.create_consumer("t", "g")
        first_pass = consumer.poll(0, max_messages=10)
        self.assertEqual([m.value for m in first_pass], ["v0", "v1", "v2"])

        restarted = broker.create_consumer("t", "g")
        redelivered = restarted.poll(0, max_messages=10)
        self.assertEqual([m.value for m in redelivered], ["v0", "v1", "v2"])

    def test_committed_offset_prevents_redelivery(self):
        broker = Broker()
        broker.create_topic("t", num_partitions=1)
        for i in range(3):
            broker.produce("t", f"v{i}")

        consumer = broker.create_consumer("t", "g")
        consumer.poll(0, max_messages=2)
        consumer.commit(0)

        restarted = broker.create_consumer("t", "g")
        redelivered = restarted.poll(0, max_messages=10)
        self.assertEqual([m.value for m in redelivered], ["v2"])

    def test_partial_commit_only_redelivers_the_uncommitted_tail(self):
        broker = Broker()
        broker.create_topic("t", num_partitions=1)
        for i in range(6):
            broker.produce("t", f"v{i}")

        consumer = broker.create_consumer("t", "g")
        consumer.poll(0, max_messages=3)
        consumer.commit(0)
        consumer.poll(0, max_messages=3)

        restarted = broker.create_consumer("t", "g")
        redelivered = restarted.poll(0, max_messages=10)
        self.assertEqual([m.value for m in redelivered], ["v3", "v4", "v5"])


if __name__ == "__main__":
    unittest.main()
