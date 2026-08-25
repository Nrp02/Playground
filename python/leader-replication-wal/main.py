from wal_replication.cluster import ReplicationCluster
from wal_replication.wal import WALRecord, WriteAheadLog


def describe(cluster: ReplicationCluster) -> None:
    for node_id in sorted(cluster.nodes):
        node = cluster.nodes[node_id]
        print(
            f"  node {node.id}: role={node.role.name} online={node.online} "
            f"wal_len={len(node.wal.records)} applied={node.applied_offset} data={node.state_machine.data}"
        )


def main() -> int:
    node_ids = [1, 2, 3]
    cluster = ReplicationCluster(node_ids, leader_id=1)

    print("=== leader accepts writes, followers replicate and stay in sync ===")
    cluster.submit("SET", "x", "1")
    cluster.submit("SET", "y", "2")
    cluster.run(30)
    describe(cluster)
    assert cluster.nodes[2].state_machine.data == cluster.nodes[1].state_machine.data
    assert cluster.nodes[3].state_machine.data == cluster.nodes[1].state_machine.data

    print("\n=== follower 3 goes offline and misses subsequent writes ===")
    cluster.take_offline(3)
    cluster.submit("SET", "z", "3")
    cluster.submit("SET", "w", "4")
    cluster.run(30)
    describe(cluster)
    assert "z" not in cluster.nodes[3].state_machine.data

    print("\n=== follower 3 comes back online and catches up by replaying missed WAL records ===")
    cluster.bring_online(3)
    cluster.run(30)
    describe(cluster)
    assert cluster.nodes[3].state_machine.data == cluster.nodes[1].state_machine.data
    assert cluster.nodes[3].wal.next_offset() == cluster.nodes[1].wal.next_offset()

    print("\n=== leader fails, a follower is promoted and continues the WAL ===")
    cluster.take_offline(1)
    old_next_offset = cluster.nodes[2].wal.next_offset()
    new_leader = cluster.promote(2)
    print(f"  node {new_leader.id} promoted to leader, continuing WAL from offset {new_leader.wal.next_offset()}")
    assert new_leader.wal.next_offset() == old_next_offset
    cluster.submit("SET", "after-failover", "5")
    cluster.run(30)
    describe(cluster)
    assert cluster.leader() is not None and cluster.leader().id == 2
    assert cluster.nodes[2].state_machine.data.get("after-failover") == "5"
    assert cluster.nodes[3].state_machine.data.get("after-failover") == "5"

    print("\n=== a corrupted WAL record is detected by a follower via checksum mismatch ===")
    corruption_cluster = ReplicationCluster([10, 11], leader_id=10)
    corruption_cluster.submit("SET", "k", "v")
    tampered_record = corruption_cluster.nodes[10].wal.get(0)
    tampered_record.value = "tampered-value"
    corruption_cluster.run(30)
    follower = corruption_cluster.nodes[11]
    print(f"  follower {follower.id}: online={follower.online} corrupted_offsets={follower.corrupted_offsets}")
    assert follower.online is False
    assert follower.corrupted_offsets == [0]

    print("\n=== a truncated WAL is detected as a non-contiguous gap on the next append ===")
    truncated_wal = WriteAheadLog()
    truncated_wal.append("SET", "a", "1")
    truncated_wal.append("SET", "b", "2")
    truncated_wal.append("SET", "c", "3")
    truncated_wal.truncate(2)
    print(f"  wal truncated: records now {[r.lsn for r in truncated_wal.records]}")
    missing_next = WALRecord(lsn=3, op="SET", key="d", value="4", checksum="0" * 64)
    try:
        truncated_wal.append_record(missing_next)
        raise AssertionError("expected rejection of out-of-sequence record")
    except Exception as exc:
        print(f"  truncated WAL correctly rejected out-of-sequence record: {exc}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
