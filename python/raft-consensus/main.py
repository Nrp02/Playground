import sys

from raft.cluster import Cluster
from raft.node import Role


def describe(cluster: Cluster) -> None:
    for node_id in sorted(cluster.nodes):
        node = cluster.nodes[node_id]
        print(f"  node {node_id}: role={node.role.name} term={node.current_term} log_len={len(node.log)} commit={node.commit_index}")


def wait_for_leader(cluster: Cluster, max_ticks: int) -> None:
    for _ in range(max_ticks):
        cluster.step()
        if cluster.leader() is not None:
            return
    raise RuntimeError("no leader elected in time")


def main() -> int:
    node_ids = [1, 2, 3, 4, 5]
    cluster = Cluster(node_ids, seed=7)

    print("=== electing initial leader ===")
    wait_for_leader(cluster, 2000)
    leader = cluster.leader()
    print(f"leader elected: node {leader.id} (term {leader.current_term})")
    describe(cluster)

    print("\n=== replicating commands ===")
    for command in ["SET x=1", "SET y=2", "SET z=3"]:
        assert cluster.submit(command)
    cluster.run(300)
    describe(cluster)
    print("committed on node 1:", cluster.committed_log(1))

    print("\n=== partitioning the leader away from the majority ===")
    minority = {leader.id}
    majority = set(node_ids) - minority
    cluster.partition([minority, majority])
    cluster.run(2000)
    majority_leaders = [cluster.nodes[n] for n in majority if cluster.nodes[n].role == Role.LEADER]
    new_leader = majority_leaders[0] if majority_leaders else None
    print(f"new leader in majority partition: node {new_leader.id if new_leader else None}")
    describe(cluster)

    print("\n=== healing the partition ===")
    cluster.heal_partition()
    cluster.run(500)
    describe(cluster)
    leaders = cluster.leaders()
    print(f"leaders after healing: {[l.id for l in leaders]}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
