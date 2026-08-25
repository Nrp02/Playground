import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from wal_replication.cluster import ReplicationCluster
from wal_replication.node import CorruptionDetected, Role
from wal_replication.wal import (
    ChecksumMismatchError,
    NonContiguousRecordError,
    WALRecord,
    WriteAheadLog,
)


class TestWALAppendAndChecksum(unittest.TestCase):
    def test_append_assigns_sequential_lsn(self) -> None:
        wal = WriteAheadLog()
        r0 = wal.append("SET", "a", "1")
        r1 = wal.append("SET", "b", "2")
        self.assertEqual(r0.lsn, 0)
        self.assertEqual(r1.lsn, 1)
        self.assertEqual(wal.next_offset(), 2)

    def test_appended_record_has_valid_checksum(self) -> None:
        wal = WriteAheadLog()
        record = wal.append("SET", "a", "1")
        self.assertTrue(record.is_valid())

    def test_from_offset_returns_tail_slice(self) -> None:
        wal = WriteAheadLog()
        for i in range(5):
            wal.append("SET", f"k{i}", str(i))
        tail = wal.from_offset(3)
        self.assertEqual([r.lsn for r in tail], [3, 4])

    def test_append_record_rejects_non_contiguous_lsn(self) -> None:
        wal = WriteAheadLog()
        wal.append("SET", "a", "1")
        bad = WALRecord(lsn=5, op="SET", key="b", value="2", checksum="0" * 64)
        with self.assertRaises(NonContiguousRecordError):
            wal.append_record(bad)

    def test_append_record_rejects_checksum_mismatch(self) -> None:
        wal = WriteAheadLog()
        record = wal.append("SET", "a", "1")
        wal.records.pop()
        record.value = "tampered"
        with self.assertRaises(ChecksumMismatchError):
            wal.append_record(record)


class TestCorruptionDetection(unittest.TestCase):
    def test_corrupted_record_fails_validity_check(self) -> None:
        wal = WriteAheadLog()
        record = wal.append("SET", "a", "1")
        self.assertTrue(record.is_valid())
        wal.corrupt(0, "tampered")
        self.assertFalse(wal.get(0).is_valid())

    def test_follower_detects_corrupted_record_and_goes_offline(self) -> None:
        cluster = ReplicationCluster([1, 2], leader_id=1)
        cluster.submit("SET", "k", "v")
        cluster.nodes[1].wal.get(0).value = "tampered"
        cluster.run(30)
        follower = cluster.nodes[2]
        self.assertFalse(follower.online)
        self.assertEqual(follower.corrupted_offsets, [0])

    def test_apply_new_record_raises_corruption_detected_directly(self) -> None:
        cluster = ReplicationCluster([1, 2], leader_id=1)
        record = cluster.nodes[1].wal.append("SET", "k", "v")
        record.value = "tampered"
        follower = cluster.nodes[2]
        with self.assertRaises(CorruptionDetected):
            follower._apply_new_record(record)


class TestTruncationDetection(unittest.TestCase):
    def test_truncated_wal_rejects_out_of_sequence_append(self) -> None:
        wal = WriteAheadLog()
        wal.append("SET", "a", "1")
        wal.append("SET", "b", "2")
        wal.append("SET", "c", "3")
        wal.truncate(2)
        self.assertEqual(wal.next_offset(), 2)
        missing = WALRecord(lsn=3, op="SET", key="d", value="4", checksum="0" * 64)
        with self.assertRaises(NonContiguousRecordError):
            wal.append_record(missing)


class TestFollowerCatchUp(unittest.TestCase):
    def test_offline_follower_replays_missed_records_on_reconnect(self) -> None:
        cluster = ReplicationCluster([1, 2, 3], leader_id=1)
        cluster.run(10)
        cluster.take_offline(3)

        cluster.submit("SET", "a", "1")
        cluster.submit("SET", "b", "2")
        cluster.run(30)
        self.assertNotEqual(
            cluster.nodes[3].state_machine.data, cluster.nodes[1].state_machine.data
        )

        cluster.bring_online(3)
        cluster.run(30)

        self.assertEqual(
            cluster.nodes[3].state_machine.data, cluster.nodes[1].state_machine.data
        )
        self.assertEqual(cluster.nodes[3].wal.next_offset(), cluster.nodes[1].wal.next_offset())

    def test_far_behind_follower_catches_up_via_explicit_catchup_request(self) -> None:
        cluster = ReplicationCluster([1, 2, 3], leader_id=1)
        cluster.take_offline(3)
        for i in range(4):
            cluster.submit("SET", f"k{i}", str(i))
        cluster.run(60)

        cluster.take_offline(1)
        new_leader = cluster.promote(2)
        cluster.submit("SET", "post-promotion", "x")

        cluster.bring_online(3)
        cluster.run(60)

        follower3 = cluster.nodes[3]
        self.assertEqual(follower3.state_machine.data, new_leader.state_machine.data)
        self.assertIn("post-promotion", follower3.state_machine.data)


class TestLeaderFailover(unittest.TestCase):
    def test_follower_promoted_to_leader_continues_wal_from_last_offset(self) -> None:
        cluster = ReplicationCluster([1, 2, 3], leader_id=1)
        cluster.submit("SET", "a", "1")
        cluster.submit("SET", "b", "2")
        cluster.run(30)

        old_leader_offset = cluster.nodes[1].wal.next_offset()
        cluster.take_offline(1)
        new_leader = cluster.promote(2)

        self.assertEqual(new_leader.role, Role.LEADER)
        self.assertEqual(new_leader.wal.next_offset(), old_leader_offset)

        record = cluster.submit("SET", "c", "3")
        self.assertIsNotNone(record)
        self.assertEqual(record.lsn, old_leader_offset)

        cluster.run(30)
        self.assertEqual(cluster.leader().id, 2)
        self.assertEqual(cluster.nodes[3].state_machine.data.get("c"), "3")

    def test_submit_fails_without_a_leader(self) -> None:
        cluster = ReplicationCluster([1, 2, 3], leader_id=1)
        cluster.take_offline(1)
        result = cluster.submit("SET", "x", "1")
        self.assertIsNone(result)


if __name__ == "__main__":
    unittest.main()
